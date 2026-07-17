#include "MacroValueHandler.hpp"

#include <cmath>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <config/TimeCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/time/Time.hpp>
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "handler/macro/MacroAutomationTiming.hpp"
#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "midi/MidiUtils.hpp"
#include "state/macro/MacroAutomationDomain.hpp"

namespace core::handler {

namespace input_utils = core::handler::sequencer::input_utils;

namespace {

constexpr uint32_t POST_RECORD_INPUT_GUARD_MS = 120;

float quantizedMidi7(float value) {
    return core::midi::fromCC(core::midi::toCC(
        core::state::macro::macroAutomationClamp01(value)
    ));
}

}  // namespace

FLASHMEM MacroValueHandler::MacroValueHandler(StateRefs state,
                                     MacroPerformanceDomainServices services,
                                     oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                     oc::api::EncoderAPI& encoders,
                                     oc::api::ButtonAPI& buttons,
                                     MacroMidiCcRuntimeAdapter& midiRuntime,
                                     oc::type::ScopeID scopeId)
    : macro_ui_(state.macroUi)
    , active_view_(state.activeView)
    , macro_edit_(state.macroEdit)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , midi_runtime_(midiRuntime)
    , scope_id_(scopeId) {
    setupBindings();
}

FLASHMEM void MacroValueHandler::setupBindings() {
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope_id_)
            .when([this]() { return shouldHandleTurns(); })
            .then([this, i](float value) {
                if (!macro_ui_.clutchActive.get() ||
                    macro_ui_.activeProperty.get() == core::state::macro::MacroPerformanceProperty::VALUE) {
                    handleValueChange(i, value);
                    return;
                }
                handleConfigChange(i, value);
            });

        buttons_.button(Config::MACRO_BUTTONS[i])
            .press()
            .scope(scope_id_)
            .when([this]() {
                return shouldHandleAutomationRecordPress() ||
                       shouldHandleAutomationRestorePress();
            })
            .then([this, i]() {
                // These modes are mutually exclusive (Clutch off records;
                // Clutch + Automation restores). A single binding preserves
                // one press owner and avoids duplicating eight registry rows.
                if (shouldHandleAutomationRecordPress()) {
                    if (!ensureActiveSlot(i)) return;
                    macro_button_held_[i] = true;
                    return;
                }
                restoreAutomation(i);
            });

        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .then([this, i]() {
                macro_button_held_[i] = false;
                if (!macro_ui_.automationRecording.active ||
                    macro_ui_.automationRecording.address.macro != i) {
                    return;
                }
                const uint32_t nowMs = core::time_compat::millis();
                (void)services_.recordAutomationPoint(
                    i,
                    nowMs,
                    quantizedMidi7(services_.absoluteBaseValue(i))
                );
                services_.commitAutomationRecording(nowMs);
                record_sample_clock_active_ = false;
                post_record_guard_active_[i] = true;
                post_record_guard_until_ms_[i] = nowMs + POST_RECORD_INPUT_GUARD_MS;
            });
    }
}

bool MacroValueHandler::shouldHandleTurns() const {
    return active_view_.get() == core::ui::ViewType::MACRO &&
           !overlays_.hasVisible() &&
           !macro_edit_.visible.get();
}

bool MacroValueHandler::shouldHandleAutomationRecordPress() const {
    return shouldHandleTurns() && !macro_ui_.clutchActive.get();
}

bool MacroValueHandler::shouldHandleAutomationRestorePress() const {
    return shouldHandleTurns() &&
           macro_ui_.clutchActive.get() &&
           macro_ui_.activeProperty.get() ==
               core::state::macro::MacroPerformanceProperty::AUTOMATION;
}

bool MacroValueHandler::shouldIgnorePostRecordTurn(uint8_t index, uint32_t nowMs) {
    if (index >= post_record_guard_until_ms_.size() || !post_record_guard_active_[index]) {
        return false;
    }
    if (!oc::time::deadlineReachedMs(nowMs, post_record_guard_until_ms_[index])) return true;
    post_record_guard_active_[index] = false;
    return false;
}

bool MacroValueHandler::shouldStartAutomationRecording(uint8_t index) const {
    return index < macro_button_held_.size() &&
           macro_button_held_[index] &&
           !macro_ui_.clutchActive.get() &&
           !macro_ui_.automationRecording.active;
}

bool MacroValueHandler::ensureActiveSlot(uint8_t index) {
    return services_.isMacroSlotActive(index);
}

void MacroValueHandler::handleValueChange(uint8_t index, float value) {
    OC_PERF_SCOPE(perfValueChange, "macro.value-change");
    const uint32_t nowMs = core::time_compat::millis();
    if (!ensureActiveSlot(index)) return;
    if (shouldIgnorePostRecordTurn(index, nowMs)) return;

    const float sanitized = core::state::macro::macroAutomationClamp01(value);
    const uint8_t cc_value = core::midi::toCC(sanitized);
    const float quantized = core::midi::fromCC(cc_value);

    if (std::abs(services_.absoluteBaseValue(index) - quantized) < 0.0005f) return;

    // A hold alone is inert. Recording starts only after a value movement has
    // crossed the same quantized threshold used for output.
    if (shouldStartAutomationRecording(index)) {
        if (services_.beginAutomationRecording(index, nowMs)) {
            last_record_sample_ms_ = nowMs;
            record_sample_clock_active_ = true;
        }
    }
    const bool recordingActive = services_.automationRecordingActiveFor(index);

    if (recordingActive) {
        services_.recordAutomationPoint(index, nowMs, quantized);
    } else if (services_.automationPlaybackActiveFor(index)) {
        if (!services_.takeManualControl(index, quantized)) return;
    } else {
        // Manual movement always authors the durable absolute base. A running
        // Modulation lane remains audible around that base.
        services_.setManualValue(index, quantized);
    }

    const auto resolved = services_.resolveManualValue(index, quantized);
    services_.setResolvedValue(index, resolved);

    if (!services_.isActivePageEnabled()) return;

    (void)midi_runtime_.publishLiveManual(
        index,
        core::midi::toCC(resolved.resolved)
    );
}

void MacroValueHandler::handleConfigChange(uint8_t index, float value) {
    if (!ensureActiveSlot(index)) return;
    const float normalized = core::state::macro::macroAutomationClamp01(value);
    const auto current = services_.activeConfig(index);

    switch (macro_ui_.activeProperty.get()) {
        case core::state::macro::MacroPerformanceProperty::CC: {
            const uint8_t cc = input_utils::normalizedToMidi7(normalized);
            services_.setConfig(index, current.channel, cc);
            return;
        }
        case core::state::macro::MacroPerformanceProperty::AUTOMATION: {
            (void)normalized;
            return;
        }
        case core::state::macro::MacroPerformanceProperty::VALUE:
        default:
            handleValueChange(index, normalized);
            return;
    }
}

void MacroValueHandler::restoreAutomation(uint8_t index) {
    if (!ensureActiveSlot(index)) return;
    (void)services_.resumeComputedSources(index);
}

void MacroValueHandler::update(uint32_t nowMs) {
    const auto& recording = macro_ui_.automationRecording;
    if (!recording.active) {
        record_sample_clock_active_ = false;
        return;
    }
    if (!record_sample_clock_active_) {
        last_record_sample_ms_ = recording.startedAtMs;
        record_sample_clock_active_ = true;
    }
    if ((nowMs - last_record_sample_ms_) <
        macro::MACRO_AUTOMATION_UPDATE_PERIOD_MS) {
        return;
    }

    const uint8_t index = recording.address.macro;
    (void)services_.recordAutomationPoint(
        index,
        nowMs,
        quantizedMidi7(services_.absoluteBaseValue(index))
    );
    // Even a saturated/reduced temporary lane remains bounded to one attempt
    // per cadence; never retry at the 1920 Hz app-loop rate.
    last_record_sample_ms_ = nowMs;
}

}  // namespace core::handler

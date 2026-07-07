#include "MacroValueHandler.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <config/TimeCompat.hpp>
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "midi/MidiUtils.hpp"

namespace core::handler {

namespace input_utils = core::handler::sequencer::input_utils;

namespace {

constexpr uint32_t POST_RECORD_INPUT_GUARD_MS = 120;

#if defined(PERF_LOG)
struct MacroValueProfiling {
    uint32_t window_start_ms = 0;
    uint32_t call_count = 0;
    uint32_t total_us = 0;
    uint32_t max_us = 0;

    void record(uint32_t elapsed_us) {
        const uint32_t now = core::time_compat::millis();
        if (window_start_ms == 0) {
            window_start_ms = now;
        }

        call_count += 1;
        total_us += elapsed_us;
        max_us = std::max(max_us, elapsed_us);

        if ((now - window_start_ms) < 500) return;

        const uint32_t avg_us = call_count > 0 ? (total_us / call_count) : 0;
        if (max_us >= 1000 || avg_us >= 500) {
            OC_LOG_INFO("[Perf][MacroValue] calls={} avg={}us max={}us",
                        call_count,
                        avg_us,
                        max_us);
        }

        window_start_ms = now;
        call_count = 0;
        total_us = 0;
        max_us = 0;
    }
};

MacroValueProfiling g_macro_value_profiling;
#endif

inline uint32_t startMacroValueProfiling() {
#if defined(PERF_LOG)
    return core::time_compat::micros();
#else
    return 0;
#endif
}

inline void finishMacroValueProfiling(uint32_t start_us) {
#if defined(PERF_LOG)
    g_macro_value_profiling.record(core::time_compat::micros() - start_us);
#else
    (void)start_us;
#endif
}

}  // namespace

MacroValueHandler::MacroValueHandler(StateRefs state,
                                     MacroPerformanceDomainServices services,
                                     oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                     oc::api::EncoderAPI& encoders,
                                     oc::api::ButtonAPI& buttons,
                                     oc::api::MidiAPI& midi,
                                     oc::type::ScopeID scopeId)
    : macro_ui_(state.macroUi)
    , active_view_(state.activeView)
    , macro_edit_(state.macroEdit)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , midi_(midi)
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
            .when([this]() { return shouldHandleAutomationRecordPress(); })
            .then([this, i]() {
                if (!ensureActiveSlot(i)) return;
                macro_button_held_[i] = true;
                macro_button_pressed_at_ms_[i] = core::time_compat::millis();
            });

        buttons_.button(Config::MACRO_BUTTONS[i])
            .press()
            .scope(scope_id_)
            .when([this]() { return shouldHandleAutomationRestorePress(); })
            .then([this, i]() { restoreAutomation(i); });

        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .then([this, i]() {
                macro_button_held_[i] = false;
                macro_button_pressed_at_ms_[i] = 0;
                if (!services_.automationRecordingActiveFor(i)) return;
                const uint32_t nowMs = core::time_compat::millis();
                services_.commitAutomationRecording(nowMs);
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

bool MacroValueHandler::shouldIgnorePostRecordTurn(uint8_t index, uint32_t nowMs) const {
    return index < post_record_guard_until_ms_.size() &&
           post_record_guard_until_ms_[index] != 0 &&
           nowMs < post_record_guard_until_ms_[index];
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
    const uint32_t start_us = startMacroValueProfiling();
    const uint32_t nowMs = core::time_compat::millis();
    if (!ensureActiveSlot(index)) {
        finishMacroValueProfiling(start_us);
        return;
    }
    if (shouldIgnorePostRecordTurn(index, nowMs)) {
        finishMacroValueProfiling(start_us);
        return;
    }
    if (shouldStartAutomationRecording(index)) {
        services_.beginAutomationRecording(index, nowMs);
    }
    const bool recordingActive = services_.automationRecordingActiveFor(index);

    const float clamped = std::clamp(value, 0.0f, 1.0f);
    const uint8_t cc_value = core::midi::toCC(clamped);
    const float quantized = core::midi::fromCC(cc_value);

    if (std::abs(services_.runtimeValue(index) - quantized) < 0.0005f) {
        finishMacroValueProfiling(start_us);
        return;
    }

    if (!recordingActive && services_.automationActiveFor(index)) {
        services_.setAutomationManualOverride(index, true);
    }

    // Update state (triggers UI update, marks dirty for persistence)
    services_.setRuntimeValue(index, quantized);
    if (recordingActive) {
        services_.recordAutomationPoint(index, nowMs, quantized);
    }

    if (!services_.isActivePageEnabled()) {
        finishMacroValueProfiling(start_us);
        return;
    }

    // Send MIDI CC
    const auto& config = services_.activeConfig(index);
    midi_.sendCC(config.channel, config.cc, cc_value);

    // Signal CC MIDI OUT activity
    services_.pulseCcOut();

    finishMacroValueProfiling(start_us);
}

void MacroValueHandler::handleConfigChange(uint8_t index, float value) {
    if (!ensureActiveSlot(index)) return;
    const float normalized = std::clamp(value, 0.0f, 1.0f);
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
    if (!services_.automationActiveFor(index)) return;
    services_.setAutomationManualOverride(index, false);
}

}  // namespace core::handler

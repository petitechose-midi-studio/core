#include "MacroValueHandler.hpp"

#include <cmath>

#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include "handler/macro/MacroAutomationTiming.hpp"
#include "handler/macro/MacroAutomationTakeInputWorkflow.hpp"
#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "midi/MidiUtils.hpp"
#include "state/macro/MacroAutomationDomain.hpp"

namespace core::handler {

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
            .then([this, i](float value) { handleValueChange(i, value); });
    }
}

bool MacroValueHandler::shouldHandleTurns() const {
    return active_view_.get() == core::ui::ViewType::MACRO &&
           !overlays_.hasVisible() &&
           !macro_edit_.visible.get();
}

FLASHMEM bool MacroValueHandler::ensureActiveSlot(uint8_t index) {
    return services_.isMacroSlotActive(index);
}

void MacroValueHandler::handleValueChange(uint8_t index, float value) {
    OC_PERF_SCOPE(perfValueChange, "macro.value-change");
    OC_PERF_UNITS(perfValueChange, index, 0U);
    const uint32_t nowMs = core::time_compat::millis();
    if (!ensureActiveSlot(index)) return;
    if (buttons_.isPressed(Config::MACRO_BUTTONS[index])) return;

    const bool takeRequested = services_.automationTakeArmed() ||
                               services_.automationTakeRecording();
    if (!takeRequested && macro_ui_.blocksPostTakeInput(index, nowMs)) return;

    const float sanitized = core::state::macro::macroAutomationClamp01(value);
    const uint8_t cc_value = core::midi::toCC(sanitized);
    const float quantized = core::midi::fromCC(cc_value);

    if (std::abs(services_.absoluteBaseValue(index) - quantized) < 0.0005f) return;

    bool takeCaptured = false;
    if (takeRequested) {
        takeCaptured = MacroAutomationTakeInputWorkflow::recordAndPublish(
            services_,
            midi_runtime_,
            index,
            nowMs,
            quantized
        );
        if (!takeCaptured) return;
        last_record_sample_ms_ = nowMs;
        record_sample_clock_active_ = true;
    }

    if (takeCaptured) {
        // The take owns Base authoring. Modulation remains a live relative
        // projection and is deliberately absent from the recorded column. The
        // canonical take-input workflow already published UI and MIDI.
        return;
    } else {
        if (services_.manualOverrideActiveFor(index) ||
            services_.automationPlaybackActiveFor(index)) {
            if (!services_.takeManualControl(index, quantized)) return;
        } else {
            // Manual movement always authors the durable absolute base. A running
            // Modulation lane remains audible around that base.
            services_.setManualValue(index, quantized);
        }
    }

    core::state::macro::MacroResolvedValue resolved{};
    resolved = services_.resolveManualValue(index, quantized);
    services_.setResolvedValue(index, resolved);

    if (!services_.isActivePageEnabled()) return;

    (void)midi_runtime_.publishLiveManual(
        index,
        core::midi::toCC(resolved.resolved)
    );
}

FLASHMEM void MacroValueHandler::update(uint32_t nowMs) {
    if (!services_.automationTakeRecording()) {
        record_sample_clock_active_ = false;
        return;
    }
    if (!record_sample_clock_active_) {
        last_record_sample_ms_ = macro_ui_.automationTake.startedAtMs;
        record_sample_clock_active_ = true;
    }
    if ((nowMs - last_record_sample_ms_) <
        macro::MACRO_AUTOMATION_UPDATE_PERIOD_MS) {
        return;
    }

    (void)services_.updateAutomationTake(nowMs);
    // One shared sample per cadence; never retry at the 1920 Hz app-loop rate.
    last_record_sample_ms_ = nowMs;
}

}  // namespace core::handler

#include "handler/macro/MacroAutomationPlaybackService.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/time/Time.hpp>
#include "handler/macro/MacroAutomationTiming.hpp"
#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "midi/MidiUtils.hpp"

namespace core::handler {

namespace {

constexpr uint8_t INVALID_CC_VALUE = 0xFF;

}  // namespace

FLASHMEM MacroAutomationPlaybackService::MacroAutomationPlaybackService(
    StateRefs state,
    MacroPerformanceDomainServices services,
    MacroMidiCcRuntimeAdapter& midiRuntime
)
    : pages_(state.pages)
    , macro_ui_(state.macroUi)
    , status_bar_(state.statusBar)
    , runtime_owner_revision_(state.runtimeOwnerRevision)
    , services_(services)
    , midi_runtime_(&midiRuntime) {
    reset();
}

FLASHMEM MacroAutomationPlaybackService::MacroAutomationPlaybackService(
    StateRefs state,
    MacroPerformanceDomainServices services,
    oc::api::MidiAPI& midi
)
    : pages_(state.pages)
    , macro_ui_(state.macroUi)
    , status_bar_(state.statusBar)
    , runtime_owner_revision_(state.runtimeOwnerRevision)
    , services_(services)
    , direct_midi_fallback_(&midi) {
    reset();
}

void MacroAutomationPlaybackService::reset() {
    was_playing_ = false;
    update_scheduled_ = false;
    last_update_ms_ = 0;
    next_due_ms_ = 0;
    consumed_runtime_owner_revision_ =
        runtime_owner_revision_ != nullptr ? runtime_owner_revision_->get() : 0U;
    playback_beat_ = 0.0f;
    cached_track_ = 0xFF;
    cached_page_ = 0xFF;
    invalidateComputedRuntime_();
}

void MacroAutomationPlaybackService::invalidateSentCache_() {
    sent_cc_values_.fill(INVALID_CC_VALUE);
}

void MacroAutomationPlaybackService::invalidateComputedRuntime_() {
    invalidateSentCache_();
    if (midi_runtime_ != nullptr) {
        midi_runtime_->clearComputedValues();
    }
}

void MacroAutomationPlaybackService::syncActivePageRuntimeProjection_(uint8_t track,
                                                                       uint8_t page) {
    macro_ui_.refreshManualOverrideMask(track, page);
    macro_ui_.clearRuntimeProjections();
}

void MacroAutomationPlaybackService::consumeRuntimeOwnerActivation_(uint32_t nowMs) {
    if (runtime_owner_revision_ == nullptr) return;

    const uint32_t revision = runtime_owner_revision_->get();
    if (revision == consumed_runtime_owner_revision_) return;

    consumed_runtime_owner_revision_ = revision;
    playback_beat_ = 0.0f;
    last_update_ms_ = nowMs;
    was_playing_ = status_bar_.playing.get();
    cached_track_ = pages_.currentActiveTrack();
    cached_page_ = pages_.currentActivePage();
    syncActivePageRuntimeProjection_(cached_track_, cached_page_);
    invalidateComputedRuntime_();
}

void MacroAutomationPlaybackService::updatePlaybackBeat_(uint32_t nowMs) {
    const bool playing = status_bar_.playing.get();
    if (!playing) {
        if (was_playing_) {
            invalidateComputedRuntime_();
        }
        was_playing_ = false;
        last_update_ms_ = nowMs;
        return;
    }

    if (!was_playing_) {
        was_playing_ = true;
        last_update_ms_ = nowMs;
        invalidateComputedRuntime_();
        return;
    }

    playback_beat_ += core::state::macro::macroAutomationElapsedBeats(
        last_update_ms_,
        nowMs,
        status_bar_.tempo.get()
    );
    last_update_ms_ = nowMs;
}

void MacroAutomationPlaybackService::update(uint32_t nowMs) {
    if (update_scheduled_ && !oc::time::deadlineReachedMs(nowMs, next_due_ms_)) return;
    update_scheduled_ = true;
    next_due_ms_ = nowMs + macro::MACRO_AUTOMATION_UPDATE_PERIOD_MS;
    OC_PERF_SCOPE(perfUpdate, "macro.automation-playback");

    consumeRuntimeOwnerActivation_(nowMs);
    updatePlaybackBeat_(nowMs);
    const uint8_t track = pages_.currentActiveTrack();
    const uint8_t page = pages_.currentActivePage();
    if (track != cached_track_ || page != cached_page_) {
        cached_track_ = track;
        cached_page_ = page;
        syncActivePageRuntimeProjection_(track, page);
        invalidateComputedRuntime_();
    }
    if (!status_bar_.playing.get()) return;

    const auto& pageData = pages_.activePageData();
    if (midi_runtime_ != nullptr) {
        midi_runtime_->beginComputedFrame();
    }
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        if (!pageData.isMacroActive(i)) {
            sent_cc_values_[i] = INVALID_CC_VALUE;
            continue;
        }
        const bool recording = macro_ui_.automationRecording.active &&
            macro_ui_.automationRecording.address.track == track &&
            macro_ui_.automationRecording.address.page == page &&
            macro_ui_.automationRecording.address.macro == i;
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            .track = track,
            .page = page,
            .macro = i,
        };
        const auto* slot = core::state::macro::macroAutomationFindSlot(pages_.automation, address);
        if (slot == nullptr && !recording) {
            sent_cc_values_[i] = INVALID_CC_VALUE;
            continue;
        }

        const core::state::macro::MacroAutomationSlotState emptySlot{};
        const auto& resolvedSlot = slot != nullptr ? *slot : emptySlot;
        float absoluteBase = std::clamp(pageData.values[i], 0.0f, 1.0f);
        float manualValue = 0.0f;
        const bool manualOverride = services_.manualOverrideValueFor(i, manualValue);
        const bool hasActiveSource = slot != nullptr &&
            (core::state::macro::macroCurvePlaybackActive(slot->automation) ||
             core::state::macro::macroCurvePlaybackActive(slot->modulation));
        if (!recording && !manualOverride && !hasActiveSource) {
            sent_cc_values_[i] = INVALID_CC_VALUE;
            continue;
        }
        if (recording || manualOverride) {
            absoluteBase = services_.absoluteBaseValue(i);
        }
        const auto resolved = core::state::macro::macroResolveValue(
            absoluteBase,
            resolvedSlot,
            pages_.automation.pointPool,
            playback_beat_,
            !recording && !manualOverride
        );
        const uint8_t ccValue = core::midi::toCC(resolved.resolved);
        services_.setResolvedValue(i, resolved);
        if (midi_runtime_ != nullptr) {
            (void)midi_runtime_->setComputedValue(i, ccValue);
            continue;
        }

        if (sent_cc_values_[i] == ccValue) continue;
        if (direct_midi_fallback_ == nullptr) continue;

        const auto& config = services_.activeConfig(i);
        direct_midi_fallback_->sendCC(config.channel, config.cc, ccValue);
        services_.pulseCcOut();
        sent_cc_values_[i] = ccValue;
    }

    if (midi_runtime_ != nullptr) {
        (void)midi_runtime_->publishComputedFrame();
    }
}

}  // namespace core::handler

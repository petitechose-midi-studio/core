#include "handler/macro/MacroAutomationPlaybackService.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/time/Time.hpp>
#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "midi/MidiUtils.hpp"

namespace core::handler {

namespace {

constexpr uint32_t UPDATE_PERIOD_MS = 16;
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
    , services_(services)
    , direct_midi_fallback_(&midi) {
    reset();
}

void MacroAutomationPlaybackService::reset() {
    was_playing_ = false;
    update_scheduled_ = false;
    last_update_ms_ = 0;
    next_due_ms_ = 0;
    playback_beat_ = 0.0f;
    cached_track_ = 0xFF;
    cached_page_ = 0xFF;
    invalidateSentCache_();
    if (midi_runtime_ != nullptr) {
        midi_runtime_->clearComputedValues();
    }
}

void MacroAutomationPlaybackService::invalidateSentCache_() {
    sent_cc_values_.fill(INVALID_CC_VALUE);
}

void MacroAutomationPlaybackService::updatePlaybackBeat_(uint32_t nowMs) {
    const bool playing = status_bar_.playing.get();
    if (!playing) {
        was_playing_ = false;
        playback_beat_ = 0.0f;
        last_update_ms_ = nowMs;
        invalidateSentCache_();
        if (midi_runtime_ != nullptr) {
            midi_runtime_->clearComputedValues();
        }
        return;
    }

    if (!was_playing_) {
        was_playing_ = true;
        playback_beat_ = 0.0f;
        last_update_ms_ = nowMs;
        invalidateSentCache_();
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
    next_due_ms_ = nowMs + UPDATE_PERIOD_MS;
    OC_PERF_SCOPE(perfUpdate, "macro.automation-playback");

    updatePlaybackBeat_(nowMs);
    if (!status_bar_.playing.get()) return;

    const uint8_t track = pages_.currentActiveTrack();
    const uint8_t page = pages_.currentActivePage();
    if (track != cached_track_ || page != cached_page_) {
        cached_track_ = track;
        cached_page_ = page;
        macro_ui_.automationManualOverrideMask.set(0);
        invalidateSentCache_();
        if (midi_runtime_ != nullptr) {
            midi_runtime_->clearComputedValues();
        }
    }

    const auto& pageData = pages_.activePageData();
    const uint16_t manualOverrideMask = macro_ui_.automationManualOverrideMask.get();
    if (midi_runtime_ != nullptr) {
        midi_runtime_->beginComputedFrame();
    }
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        if (!pageData.isMacroActive(i)) {
            sent_cc_values_[i] = INVALID_CC_VALUE;
            continue;
        }
        if (macro_ui_.automationRecording.active &&
            macro_ui_.automationRecording.address.track == track &&
            macro_ui_.automationRecording.address.page == page &&
            macro_ui_.automationRecording.address.macro == i) {
            sent_cc_values_[i] = INVALID_CC_VALUE;
            continue;
        }
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            .track = track,
            .page = page,
            .macro = i,
        };
        const auto* slot = core::state::macro::macroAutomationFindSlot(pages_.automation, address);
        if (slot == nullptr || !slot->automation.active) {
            sent_cc_values_[i] = INVALID_CC_VALUE;
            continue;
        }

        const auto resolved = core::state::macro::macroResolveValue(
            std::clamp(pageData.values[i], 0.0f, 1.0f),
            *slot,
            pages_.automation.pointPool,
            playback_beat_
        );
        const uint8_t ccValue = core::midi::toCC(resolved.resolved);
        if (midi_runtime_ != nullptr) {
            (void)midi_runtime_->setComputedValue(i, ccValue);
            const uint16_t overrideBit = static_cast<uint16_t>(1U << i);
            if ((manualOverrideMask & overrideBit) == 0) {
                services_.setResolvedValue(i, core::midi::fromCC(ccValue));
            }
            continue;
        }

        const uint16_t overrideBit = static_cast<uint16_t>(1U << i);
        if ((manualOverrideMask & overrideBit) != 0) {
            sent_cc_values_[i] = INVALID_CC_VALUE;
            continue;
        }
        if (sent_cc_values_[i] == ccValue) continue;
        if (direct_midi_fallback_ == nullptr) continue;

        services_.setResolvedValue(i, core::midi::fromCC(ccValue));
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

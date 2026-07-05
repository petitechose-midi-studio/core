#include "handler/macro/MacroAutomationPlaybackService.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include "midi/MidiUtils.hpp"

namespace core::handler {

namespace {

constexpr uint32_t UPDATE_PERIOD_MS = 16;
constexpr uint8_t INVALID_CC_VALUE = 0xFF;

float elapsedBeats(uint32_t previousMs, uint32_t nowMs, float tempoBpm) {
    if (nowMs <= previousMs) return 0.0f;
    const float tempo = tempoBpm > 0.0f ? tempoBpm : 120.0f;
    return (static_cast<float>(nowMs - previousMs) * tempo) / 60000.0f;
}

}  // namespace

MacroAutomationPlaybackService::MacroAutomationPlaybackService(
    StateRefs state,
    MacroPerformanceDomainServices services,
    oc::api::MidiAPI& midi
)
    : pages_(state.pages)
    , macro_ui_(state.macroUi)
    , status_bar_(state.statusBar)
    , services_(services)
    , midi_(midi) {
    reset();
}

void MacroAutomationPlaybackService::reset() {
    was_playing_ = false;
    last_update_ms_ = 0;
    next_due_ms_ = 0;
    playback_beat_ = 0.0f;
    cached_track_ = 0xFF;
    cached_page_ = 0xFF;
    invalidateSentCache_();
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
        return;
    }

    if (!was_playing_) {
        was_playing_ = true;
        playback_beat_ = 0.0f;
        last_update_ms_ = nowMs;
        invalidateSentCache_();
        return;
    }

    playback_beat_ += elapsedBeats(last_update_ms_, nowMs, status_bar_.tempo.get());
    last_update_ms_ = nowMs;
}

void MacroAutomationPlaybackService::update(uint32_t nowMs) {
    if (nowMs < next_due_ms_) return;
    next_due_ms_ = nowMs + UPDATE_PERIOD_MS;

    updatePlaybackBeat_(nowMs);
    if (!status_bar_.playing.get()) return;

    const uint8_t track = pages_.currentActiveTrack();
    const uint8_t page = pages_.currentActivePage();
    if (track != cached_track_ || page != cached_page_) {
        cached_track_ = track;
        cached_page_ = page;
        macro_ui_.automationManualOverrideMask.set(0);
        invalidateSentCache_();
    }

    const auto& pageData = pages_.activePageData();
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        if (!pageData.isMacroActive(i)) continue;
        if (macro_ui_.automationRecording.active &&
            macro_ui_.automationRecording.address.track == track &&
            macro_ui_.automationRecording.address.page == page &&
            macro_ui_.automationRecording.address.macro == i) {
            sent_cc_values_[i] = INVALID_CC_VALUE;
            continue;
        }
        const uint16_t overrideBit = static_cast<uint16_t>(1U << i);
        if ((macro_ui_.automationManualOverrideMask.get() & overrideBit) != 0) continue;
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            .track = track,
            .page = page,
            .macro = i,
        };
        const auto* slot = core::state::macro::macroAutomationFindSlot(pages_.automation, address);
        if (slot == nullptr || !slot->automation.active) continue;

        const auto resolved = core::state::macro::macroResolveValue(
            std::clamp(pageData.values[i], 0.0f, 1.0f),
            *slot,
            pages_.automation.pointPool,
            playback_beat_
        );
        const uint8_t ccValue = core::midi::toCC(resolved.resolved);
        services_.setRuntimeValue(i, core::midi::fromCC(ccValue));
        if (sent_cc_values_[i] == ccValue) continue;

        const auto& config = services_.activeConfig(i);
        midi_.sendCC(config.channel, config.cc, ccValue);
        services_.pulseCcOut();
        sent_cc_values_[i] = ccValue;
    }
}

}  // namespace core::handler

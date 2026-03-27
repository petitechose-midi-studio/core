#include "MidiClockSyncService.hpp"

#include <algorithm>
#include <cmath>

namespace core::sequencer {

namespace {
constexpr uint16_t MIN_AUTO_FALLBACK_MS = 100;
constexpr uint16_t MAX_AUTO_FALLBACK_MS = 5000;
constexpr uint8_t MIN_AUTO_LOCK_CLOCKS = 1;
constexpr uint8_t MAX_AUTO_LOCK_CLOCKS = 96;
constexpr uint32_t MAX_CLOCK_BURST_PER_UPDATE = 96;
constexpr uint32_t MIN_CLOCK_INTERVAL_US = 2500;
constexpr uint32_t MAX_CLOCK_INTERVAL_US = 250000;
constexpr uint8_t MIN_INTERVAL_SAMPLES = 6;
constexpr float MIN_BPM = 20.0f;
constexpr float MAX_BPM = 666.0f;
constexpr float TEMPO_ALPHA_STABLE = 0.18f;
constexpr float TEMPO_ALPHA_MEDIUM = 0.35f;
constexpr float TEMPO_ALPHA_FAST = 0.55f;

constexpr uint32_t DISPLAY_PUBLISH_MIN_MS = 150;
constexpr float DISPLAY_DEADBAND_BPM = 0.2f;
constexpr float DISPLAY_FORCE_STEP_BPM = 1.5f;
constexpr float DISPLAY_ALPHA_STABLE = 0.12f;
constexpr float DISPLAY_ALPHA_MEDIUM = 0.26f;
constexpr float DISPLAY_ALPHA_FAST = 0.48f;
}  // namespace

MidiClockSyncService::MidiClockSyncService(core::state::MidiSyncState& syncState,
                                           core::state::StatusBarState& statusBar,
                                           oc::api::MidiAPI& midi)
    : sync_state_(syncState)
    , status_bar_(statusBar)
    , midi_(midi) {
    const float tempo = status_bar_.tempo.get();
    status_bar_.tempoDisplay.set(tempo);
    display_tempo_filtered_ = tempo;
    display_tempo_published_ = tempo;
    status_bar_.syncExternalSource.set(false);
    status_bar_.syncInputPulse.set(false);
    status_bar_.tempoLocked.set(false);
    status_bar_.transportLocked.set(false);
}

void MidiClockSyncService::update(uint32_t nowMs) {
    updateSourceSelection_(nowMs);
    pushSyncIndicators_();

    if (using_external_source_) {
        current_tick_ = external_tick_;
        if (sync_state_.followTransport.get()) {
            current_playing_ = external_transport_seen_
                                   ? external_playing_
                                   : hasExternalClockSignal_(nowMs);
            status_bar_.playing.set(current_playing_);
        } else {
            current_playing_ = status_bar_.playing.get();
        }
    } else {
        current_playing_ = status_bar_.playing.get();
        internal_clock_.setBpm(status_bar_.tempo.get());
        internal_clock_.setPlaying(current_playing_);
        internal_clock_.update(nowMs);
        current_tick_ = internal_clock_.tick();
        updateMasterClockOutput_();
    }

    if (using_external_source_) {
        last_master_playing_ = false;
        last_master_tick_sent_ = current_tick_;
    }

    updateDisplayedTempo_(nowMs);
}

void MidiClockSyncService::onClock(uint64_t timestampUs, uint32_t hostNowMs) {
    if (sync_state_.mode.get() == core::state::MidiSyncMode::MASTER) {
        return;
    }

    if (last_external_clock_us_ > 0 && timestampUs > last_external_clock_us_) {
        const uint64_t deltaUs64 = timestampUs - last_external_clock_us_;
        if (deltaUs64 >= MIN_CLOCK_INTERVAL_US && deltaUs64 <= MAX_CLOCK_INTERVAL_US) {
            pushClockIntervalUs_(static_cast<uint32_t>(deltaUs64));

            const float sampleBpm = estimateTempoFromIntervals_();
            if (sampleBpm >= MIN_BPM && sampleBpm <= MAX_BPM) {
                if (!external_bpm_valid_) {
                    external_bpm_estimate_ = sampleBpm;
                    external_bpm_valid_ = true;
                } else {
                    const float error = std::fabs(sampleBpm - external_bpm_estimate_);
                    float alpha = TEMPO_ALPHA_STABLE;
                    if (error > 8.0f) {
                        alpha = TEMPO_ALPHA_FAST;
                    } else if (error > 2.5f) {
                        alpha = TEMPO_ALPHA_MEDIUM;
                    }

                    external_bpm_estimate_ =
                        external_bpm_estimate_ * (1.0f - alpha) +
                        sampleBpm * alpha;
                }
            }
        }
    }

    last_external_clock_us_ = timestampUs;
    external_tick_ += 1;
    last_external_clock_ms_ = hostNowMs;

    if (external_clock_streak_ < 255) {
        external_clock_streak_ += 1;
    }

    const uint8_t lockNeeded = std::clamp(sync_state_.autoLockClockCount.get(),
                                          MIN_AUTO_LOCK_CLOCKS,
                                          MAX_AUTO_LOCK_CLOCKS);

    if (sync_state_.mode.get() == core::state::MidiSyncMode::SLAVE || external_clock_streak_ >= lockNeeded) {
        external_locked_ = true;
    }

    status_bar_.pulseSyncInput();
}

void MidiClockSyncService::resetExternalTempoEstimator_() {
    last_external_clock_us_ = 0;
    clock_interval_count_ = 0;
    clock_interval_write_idx_ = 0;
    external_bpm_valid_ = false;
    external_transport_seen_ = false;
}

void MidiClockSyncService::pushClockIntervalUs_(uint32_t intervalUs) {
    clock_interval_us_[clock_interval_write_idx_] = intervalUs;

    const uint8_t next = static_cast<uint8_t>(clock_interval_write_idx_ + 1);
    clock_interval_write_idx_ = static_cast<uint8_t>(next % clock_interval_us_.size());

    if (clock_interval_count_ < clock_interval_us_.size()) {
        clock_interval_count_ += 1;
    }
}

float MidiClockSyncService::estimateTempoFromIntervals_() const {
    if (clock_interval_count_ < MIN_INTERVAL_SAMPLES) {
        return 0.0f;
    }

    std::array<uint32_t, 24> sorted{};
    for (uint8_t i = 0; i < clock_interval_count_; ++i) {
        sorted[i] = clock_interval_us_[i];
    }

    std::sort(sorted.begin(), sorted.begin() + clock_interval_count_);

    uint8_t trim = 0;
    if (clock_interval_count_ >= 10) {
        trim = static_cast<uint8_t>(clock_interval_count_ / 5);  // 20% each side
    }

    if (trim * 2 >= clock_interval_count_) {
        trim = 0;
    }

    const uint8_t start = trim;
    const uint8_t end = static_cast<uint8_t>(clock_interval_count_ - trim);
    if (end <= start) {
        return 0.0f;
    }

    uint64_t sumUs = 0;
    for (uint8_t i = start; i < end; ++i) {
        sumUs += sorted[i];
    }

    const uint8_t used = static_cast<uint8_t>(end - start);
    if (used == 0) {
        return 0.0f;
    }

    const float meanUs = static_cast<float>(sumUs) / static_cast<float>(used);
    if (meanUs <= 0.0f) {
        return 0.0f;
    }

    return 60000000.0f / (meanUs * 24.0f);
}

void MidiClockSyncService::onStart() {
    if (sync_state_.mode.get() == core::state::MidiSyncMode::MASTER) return;

    external_tick_ = 0;
    external_transport_seen_ = true;
    external_playing_ = true;

    if (!allowExternalTransport_()) return;

    resync_requested_ = true;

    if (sync_state_.followTransport.get()) {
        status_bar_.playing.set(true);
    }
}

void MidiClockSyncService::onContinue() {
    if (sync_state_.mode.get() == core::state::MidiSyncMode::MASTER) return;

    external_transport_seen_ = true;
    external_playing_ = true;

    if (!allowExternalTransport_()) return;

    if (sync_state_.followTransport.get()) {
        status_bar_.playing.set(true);
    }
}

void MidiClockSyncService::onStop() {
    if (sync_state_.mode.get() == core::state::MidiSyncMode::MASTER) return;

    external_transport_seen_ = true;
    external_playing_ = false;

    if (!allowExternalTransport_()) return;

    resync_requested_ = true;

    if (sync_state_.followTransport.get()) {
        status_bar_.playing.set(false);
    }
}

bool MidiClockSyncService::consumeResyncRequest() {
    const bool requested = resync_requested_;
    resync_requested_ = false;
    return requested;
}

void MidiClockSyncService::updateSourceSelection_(uint32_t nowMs) {
    const auto mode = sync_state_.mode.get();

    if (mode == core::state::MidiSyncMode::MASTER) {
        external_locked_ = false;
        external_clock_streak_ = 0;
        resetExternalTempoEstimator_();
    } else if (mode == core::state::MidiSyncMode::AUTO) {
        const uint16_t fallbackMs = std::clamp(sync_state_.autoFallbackMs.get(),
                                               MIN_AUTO_FALLBACK_MS,
                                               MAX_AUTO_FALLBACK_MS);
        const uint32_t elapsed = nowMs - last_external_clock_ms_;

        if (external_locked_ && elapsed > fallbackMs) {
            external_locked_ = false;
            external_clock_streak_ = 0;
            resetExternalTempoEstimator_();
        }
    } else {
        external_locked_ = true;
    }

    const bool externalSignal = hasExternalClockSignal_(nowMs);
    sync_state_.externalClockPresent.set(externalSignal);

    if (!externalSignal) {
        resetExternalTempoEstimator_();
    }

    const bool useExternal =
        (mode == core::state::MidiSyncMode::SLAVE) ||
        (mode == core::state::MidiSyncMode::AUTO && external_locked_);

    if (useExternal != using_external_source_) {
        using_external_source_ = useExternal;
        resync_requested_ = true;

        if (using_external_source_) {
            // Keep continuity on source switch when transport follow is enabled
            // but no explicit external START has been observed yet.
            if (!external_transport_seen_ && !external_playing_) {
                external_playing_ = status_bar_.playing.get();
            }

            if (sync_state_.followTransport.get()) {
                status_bar_.playing.set(external_transport_seen_
                                            ? external_playing_
                                            : hasExternalClockSignal_(nowMs));
            }
        } else {
            internal_clock_.reset();
            internal_clock_.setPlaying(status_bar_.playing.get());
            internal_clock_.setBpm(status_bar_.tempo.get());
            internal_clock_.update(nowMs);
            last_master_tick_sent_ = internal_clock_.tick();
            last_master_playing_ = status_bar_.playing.get();
        }
    }

    sync_state_.activeSource.set(using_external_source_
                                     ? core::state::ClockSourceActive::EXTERNAL
                                     : core::state::ClockSourceActive::INTERNAL);
}

void MidiClockSyncService::updateDisplayedTempo_(uint32_t nowMs) {
    const bool externalMode = using_external_source_ && external_bpm_valid_;
    const float targetTempo = externalMode ? external_bpm_estimate_ : status_bar_.tempo.get();

    if (!display_filter_initialized_ || externalMode != display_filter_external_mode_) {
        display_filter_initialized_ = true;
        display_filter_external_mode_ = externalMode;
        display_tempo_filtered_ = targetTempo;
        display_tempo_published_ = targetTempo;
        last_display_publish_ms_ = nowMs;
        status_bar_.tempoDisplay.set(targetTempo);
        return;
    }

    if (!externalMode) {
        display_tempo_filtered_ = targetTempo;
        display_tempo_published_ = targetTempo;
        last_display_publish_ms_ = nowMs;
        status_bar_.tempoDisplay.set(targetTempo);
        return;
    }

    const float error = std::fabs(targetTempo - display_tempo_filtered_);
    float alpha = DISPLAY_ALPHA_STABLE;
    if (error > 6.0f) {
        alpha = DISPLAY_ALPHA_FAST;
    } else if (error > 1.5f) {
        alpha = DISPLAY_ALPHA_MEDIUM;
    }

    display_tempo_filtered_ =
        display_tempo_filtered_ * (1.0f - alpha) +
        targetTempo * alpha;

    const bool publishDue = (nowMs - last_display_publish_ms_) >= DISPLAY_PUBLISH_MIN_MS;
    const bool forcePublish = std::fabs(targetTempo - display_tempo_published_) >= DISPLAY_FORCE_STEP_BPM;

    if (!publishDue && !forcePublish) {
        return;
    }

    const float quantized = std::round(display_tempo_filtered_ * 10.0f) / 10.0f;
    if (!forcePublish && std::fabs(quantized - display_tempo_published_) < DISPLAY_DEADBAND_BPM) {
        last_display_publish_ms_ = nowMs;
        return;
    }

    status_bar_.tempoDisplay.set(quantized);
    display_tempo_published_ = quantized;
    last_display_publish_ms_ = nowMs;
}

void MidiClockSyncService::pushSyncIndicators_() {
    status_bar_.syncExternalSource.set(using_external_source_);
    status_bar_.tempoLocked.set(using_external_source_);
    status_bar_.transportLocked.set(using_external_source_ && sync_state_.followTransport.get());
}

void MidiClockSyncService::updateMasterClockOutput_() {
    if (current_playing_ && !last_master_playing_) {
        midi_.sendStart();
    } else if (!current_playing_ && last_master_playing_) {
        midi_.sendStop();
    }

    last_master_playing_ = current_playing_;

    if (!current_playing_) {
        last_master_tick_sent_ = current_tick_;
        return;
    }

    if (current_tick_ < last_master_tick_sent_) {
        last_master_tick_sent_ = current_tick_;
        return;
    }

    uint32_t pending = current_tick_ - last_master_tick_sent_;
    if (pending > MAX_CLOCK_BURST_PER_UPDATE) {
        pending = MAX_CLOCK_BURST_PER_UPDATE;
    }

    for (uint32_t i = 0; i < pending; ++i) {
        midi_.sendClock();
        last_master_tick_sent_ += 1;
    }
}

bool MidiClockSyncService::allowExternalTransport_() const {
    const auto mode = sync_state_.mode.get();
    if (mode == core::state::MidiSyncMode::MASTER) {
        return false;
    }
    if (mode == core::state::MidiSyncMode::SLAVE) {
        return true;
    }

    // AUTO: accept transport only when external source is currently trusted.
    return external_locked_;
}

bool MidiClockSyncService::hasExternalClockSignal_(uint32_t nowMs) const {
    if (last_external_clock_ms_ == 0) return false;

    const uint16_t fallbackMs = std::clamp(sync_state_.autoFallbackMs.get(),
                                           MIN_AUTO_FALLBACK_MS,
                                           MAX_AUTO_FALLBACK_MS);
    return (nowMs - last_external_clock_ms_) <= fallbackMs;
}

}  // namespace core::sequencer

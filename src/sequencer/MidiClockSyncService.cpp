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

FLASHMEM MidiClockSyncService::MidiClockSyncService(oc::api::MidiAPI& midi)
    : midi_(midi) {}

FLASHMEM void MidiClockSyncService::queuePlayingProjection_(bool playing) {
    if ((projected_ui_dirty_mask_ & UiProjectionSnapshot::PLAYING) == 0 &&
        published_ui_state_.playing == playing) {
        return;
    }

    projected_ui_state_.playing = playing;
    projected_ui_dirty_mask_ |= UiProjectionSnapshot::PLAYING;
}

FLASHMEM void MidiClockSyncService::queueTempoDisplayProjection_(float tempo) {
    if ((projected_ui_dirty_mask_ & UiProjectionSnapshot::TEMPO_DISPLAY) == 0 &&
        published_ui_state_.tempoDisplay == tempo) {
        return;
    }

    projected_ui_state_.tempoDisplay = tempo;
    projected_ui_dirty_mask_ |= UiProjectionSnapshot::TEMPO_DISPLAY;
}

FLASHMEM void MidiClockSyncService::queueSyncExternalSourceProjection_(bool active) {
    if ((projected_ui_dirty_mask_ & UiProjectionSnapshot::SYNC_EXTERNAL_SOURCE) == 0 &&
        published_ui_state_.syncExternalSource == active) {
        return;
    }

    projected_ui_state_.syncExternalSource = active;
    projected_ui_dirty_mask_ |= UiProjectionSnapshot::SYNC_EXTERNAL_SOURCE;
}

FLASHMEM void MidiClockSyncService::queueTempoLockedProjection_(bool locked) {
    if ((projected_ui_dirty_mask_ & UiProjectionSnapshot::TEMPO_LOCKED) == 0 &&
        published_ui_state_.tempoLocked == locked) {
        return;
    }

    projected_ui_state_.tempoLocked = locked;
    projected_ui_dirty_mask_ |= UiProjectionSnapshot::TEMPO_LOCKED;
}

FLASHMEM void MidiClockSyncService::queueTransportLockedProjection_(bool locked) {
    if ((projected_ui_dirty_mask_ & UiProjectionSnapshot::TRANSPORT_LOCKED) == 0 &&
        published_ui_state_.transportLocked == locked) {
        return;
    }

    projected_ui_state_.transportLocked = locked;
    projected_ui_dirty_mask_ |= UiProjectionSnapshot::TRANSPORT_LOCKED;
}

FLASHMEM void MidiClockSyncService::queueActiveSourceProjection_(core::state::ClockSourceActive source) {
    if ((projected_ui_dirty_mask_ & UiProjectionSnapshot::ACTIVE_SOURCE) == 0 &&
        published_ui_state_.activeSource == source) {
        return;
    }

    projected_ui_state_.activeSource = source;
    projected_ui_dirty_mask_ |= UiProjectionSnapshot::ACTIVE_SOURCE;
}

FLASHMEM void MidiClockSyncService::queueExternalClockPresentProjection_(bool present) {
    if ((projected_ui_dirty_mask_ & UiProjectionSnapshot::EXTERNAL_CLOCK_PRESENT) == 0 &&
        published_ui_state_.externalClockPresent == present) {
        return;
    }

    projected_ui_state_.externalClockPresent = present;
    projected_ui_dirty_mask_ |= UiProjectionSnapshot::EXTERNAL_CLOCK_PRESENT;
}

FLASHMEM MidiClockSyncService::UiProjectionSnapshot MidiClockSyncService::takeUiProjectionSnapshot() {
    UiProjectionSnapshot snapshot{
        .dirtyMask = projected_ui_dirty_mask_,
        .playing = projected_ui_state_.playing,
        .tempoDisplay = projected_ui_state_.tempoDisplay,
        .syncExternalSource = projected_ui_state_.syncExternalSource,
        .tempoLocked = projected_ui_state_.tempoLocked,
        .transportLocked = projected_ui_state_.transportLocked,
        .activeSource = projected_ui_state_.activeSource,
        .externalClockPresent = projected_ui_state_.externalClockPresent,
        .syncInputPulse = pending_sync_input_pulse_,
    };

    if ((snapshot.dirtyMask & UiProjectionSnapshot::PLAYING) != 0) {
        published_ui_state_.playing = snapshot.playing;
    }
    if ((snapshot.dirtyMask & UiProjectionSnapshot::TEMPO_DISPLAY) != 0) {
        published_ui_state_.tempoDisplay = snapshot.tempoDisplay;
    }
    if ((snapshot.dirtyMask & UiProjectionSnapshot::SYNC_EXTERNAL_SOURCE) != 0) {
        published_ui_state_.syncExternalSource = snapshot.syncExternalSource;
    }
    if ((snapshot.dirtyMask & UiProjectionSnapshot::TEMPO_LOCKED) != 0) {
        published_ui_state_.tempoLocked = snapshot.tempoLocked;
    }
    if ((snapshot.dirtyMask & UiProjectionSnapshot::TRANSPORT_LOCKED) != 0) {
        published_ui_state_.transportLocked = snapshot.transportLocked;
    }
    if ((snapshot.dirtyMask & UiProjectionSnapshot::ACTIVE_SOURCE) != 0) {
        published_ui_state_.activeSource = snapshot.activeSource;
    }
    if ((snapshot.dirtyMask & UiProjectionSnapshot::EXTERNAL_CLOCK_PRESENT) != 0) {
        published_ui_state_.externalClockPresent = snapshot.externalClockPresent;
    }

    projected_ui_dirty_mask_ = 0;
    pending_sync_input_pulse_ = false;

    return snapshot;
}

void MidiClockSyncService::update(const MidiClockSyncRuntimeConfig& config,
                                  uint32_t nowMs,
                                  bool driveTransport) {
    runtime_config_ = config;
    updateSourceSelection_(nowMs);
    pushSyncIndicators_();

    if (using_external_source_) {
        current_tick_ = external_tick_;
        if (runtime_config_.followTransport) {
            current_playing_ = external_transport_seen_
                                   ? external_playing_
                                   : hasExternalClockSignal_(nowMs);
            queuePlayingProjection_(current_playing_);
        } else {
            current_playing_ = runtime_config_.playing;
        }
    } else {
        current_playing_ = runtime_config_.playing;
        if (driveTransport) {
            internal_clock_.setBpm(runtime_config_.tempo);
            internal_clock_.setPlaying(current_playing_);
            internal_clock_.update(nowMs);
            current_tick_ = internal_clock_.tick();
            updateMasterClockOutput_();
        } else {
            current_tick_ = 0;
        }
    }

    if (using_external_source_ || !driveTransport) {
        last_master_playing_ = false;
        last_master_tick_sent_ = current_tick_;
    }

    updateDisplayedTempo_(nowMs);
}

void MidiClockSyncService::onClock(uint64_t timestampUs, uint32_t hostNowMs) {
    if (runtime_config_.mode == core::state::MidiSyncMode::MASTER) {
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

    const uint8_t lockNeeded = std::clamp(runtime_config_.autoLockClockCount,
                                          MIN_AUTO_LOCK_CLOCKS,
                                          MAX_AUTO_LOCK_CLOCKS);

    if (runtime_config_.mode == core::state::MidiSyncMode::SLAVE || external_clock_streak_ >= lockNeeded) {
        external_locked_ = true;
    }

    pending_sync_input_pulse_ = true;
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

FLASHMEM void MidiClockSyncService::onStart() {
    if (runtime_config_.mode == core::state::MidiSyncMode::MASTER) return;

    external_tick_ = 0;
    external_transport_seen_ = true;
    external_playing_ = true;

    if (!allowExternalTransport_()) return;

    resync_requested_ = true;

    if (runtime_config_.followTransport) {
        queuePlayingProjection_(true);
    }
}

FLASHMEM void MidiClockSyncService::onContinue() {
    if (runtime_config_.mode == core::state::MidiSyncMode::MASTER) return;

    external_transport_seen_ = true;
    external_playing_ = true;

    if (!allowExternalTransport_()) return;

    if (runtime_config_.followTransport) {
        queuePlayingProjection_(true);
    }
}

FLASHMEM void MidiClockSyncService::onStop() {
    if (runtime_config_.mode == core::state::MidiSyncMode::MASTER) return;

    external_transport_seen_ = true;
    external_playing_ = false;

    if (!allowExternalTransport_()) return;

    resync_requested_ = true;

    if (runtime_config_.followTransport) {
        queuePlayingProjection_(false);
    }
}

bool MidiClockSyncService::consumeResyncRequest() {
    const bool requested = resync_requested_;
    resync_requested_ = false;
    return requested;
}

void MidiClockSyncService::updateSourceSelection_(uint32_t nowMs) {
    const auto mode = runtime_config_.mode;

    if (mode == core::state::MidiSyncMode::MASTER) {
        external_locked_ = false;
        external_clock_streak_ = 0;
        resetExternalTempoEstimator_();
    } else if (mode == core::state::MidiSyncMode::AUTO) {
        const uint16_t fallbackMs = std::clamp(runtime_config_.autoFallbackMs,
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
    queueExternalClockPresentProjection_(externalSignal);

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
                external_playing_ = runtime_config_.playing;
            }

            if (runtime_config_.followTransport) {
                queuePlayingProjection_(external_transport_seen_
                                            ? external_playing_
                                            : hasExternalClockSignal_(nowMs));
            }
        } else {
            internal_clock_.reset();
            internal_clock_.setPlaying(runtime_config_.playing);
            internal_clock_.setBpm(runtime_config_.tempo);
            internal_clock_.update(nowMs);
            last_master_tick_sent_ = internal_clock_.tick();
            last_master_playing_ = runtime_config_.playing;
        }
    }

    queueActiveSourceProjection_(using_external_source_
                                     ? core::state::ClockSourceActive::EXTERNAL
                                     : core::state::ClockSourceActive::INTERNAL);
}

void MidiClockSyncService::updateDisplayedTempo_(uint32_t nowMs) {
    const bool externalMode = using_external_source_ && external_bpm_valid_;
    const float targetTempo = externalMode ? external_bpm_estimate_ : runtime_config_.tempo;

    if (!display_filter_initialized_ || externalMode != display_filter_external_mode_) {
        display_filter_initialized_ = true;
        display_filter_external_mode_ = externalMode;
        display_tempo_filtered_ = targetTempo;
        display_tempo_published_ = targetTempo;
        last_display_publish_ms_ = nowMs;
        queueTempoDisplayProjection_(targetTempo);
        return;
    }

    if (!externalMode) {
        display_tempo_filtered_ = targetTempo;
        display_tempo_published_ = targetTempo;
        last_display_publish_ms_ = nowMs;
        queueTempoDisplayProjection_(targetTempo);
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

    queueTempoDisplayProjection_(quantized);
    display_tempo_published_ = quantized;
    last_display_publish_ms_ = nowMs;
}

void MidiClockSyncService::pushSyncIndicators_() {
    queueSyncExternalSourceProjection_(using_external_source_);
    queueTempoLockedProjection_(using_external_source_);
    queueTransportLockedProjection_(using_external_source_ && runtime_config_.followTransport);
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
    const auto mode = runtime_config_.mode;
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

    const uint16_t fallbackMs = std::clamp(runtime_config_.autoFallbackMs,
                                           MIN_AUTO_FALLBACK_MS,
                                           MAX_AUTO_FALLBACK_MS);
    return (nowMs - last_external_clock_ms_) <= fallbackMs;
}

}  // namespace core::sequencer

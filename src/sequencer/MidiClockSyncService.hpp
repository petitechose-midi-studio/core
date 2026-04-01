#pragma once

#include <array>
#include <cstdint>

#include <oc/api/MidiAPI.hpp>

#include "sequencer/InternalTransportClock.hpp"
#include "state/MidiSyncState.hpp"
#include "state/StatusBarState.hpp"

namespace core::sequencer {

struct MidiClockSyncRuntimeConfig {
    core::state::MidiSyncMode mode = core::state::MidiSyncMode::AUTO;
    bool followTransport = true;
    uint16_t autoFallbackMs = 500;
    uint8_t autoLockClockCount = 6;
    float tempo = 120.0f;
    bool playing = false;
};

class MidiClockSyncService {
public:
    MidiClockSyncService(core::state::MidiSyncState& syncState,
                         core::state::StatusBarState& statusBar,
                         oc::api::MidiAPI& midi);

    void update(const MidiClockSyncRuntimeConfig& config, uint32_t nowMs, bool driveTransport = true);

    void onClock(uint64_t timestampUs, uint32_t hostNowMs);
    void onStart();
    void onContinue();
    void onStop();
    void publishUiState(uint32_t nowMs);

    uint32_t tick() const { return current_tick_; }
    bool playing() const { return current_playing_; }
    bool usingExternalSource() const { return using_external_source_; }

    bool consumeResyncRequest();

private:
    void queuePlayingProjection_(bool playing);
    void queueTempoDisplayProjection_(float tempo);
    void queueSyncExternalSourceProjection_(bool active);
    void queueTempoLockedProjection_(bool locked);
    void queueTransportLockedProjection_(bool locked);
    void queueActiveSourceProjection_(core::state::ClockSourceActive source);
    void queueExternalClockPresentProjection_(bool present);
    void updateSourceSelection_(uint32_t nowMs);
    void resetExternalTempoEstimator_();
    void pushClockIntervalUs_(uint32_t intervalUs);
    float estimateTempoFromIntervals_() const;
    void updateMasterClockOutput_();
    void updateDisplayedTempo_(uint32_t nowMs);
    void pushSyncIndicators_();
    bool allowExternalTransport_() const;
    bool hasExternalClockSignal_(uint32_t nowMs) const;

    core::state::MidiSyncState& sync_state_;
    core::state::StatusBarState& status_bar_;
    oc::api::MidiAPI& midi_;

    InternalTransportClock internal_clock_;

    uint32_t current_tick_ = 0;
    bool current_playing_ = false;

    uint32_t external_tick_ = 0;
    bool external_playing_ = false;
    uint32_t last_external_clock_ms_ = 0;
    uint64_t last_external_clock_us_ = 0;
    std::array<uint32_t, 24> clock_interval_us_{};
    uint8_t clock_interval_count_ = 0;
    uint8_t clock_interval_write_idx_ = 0;
    uint8_t external_clock_streak_ = 0;
    bool external_locked_ = false;
    bool external_transport_seen_ = false;
    float external_bpm_estimate_ = 120.0f;
    bool external_bpm_valid_ = false;

    bool using_external_source_ = false;
    bool resync_requested_ = false;

    bool last_master_playing_ = false;
    uint32_t last_master_tick_sent_ = 0;

    float display_tempo_filtered_ = 120.0f;
    float display_tempo_published_ = 120.0f;
    uint32_t last_display_publish_ms_ = 0;
    bool display_filter_initialized_ = false;
    bool display_filter_external_mode_ = false;
    MidiClockSyncRuntimeConfig runtime_config_{};
    bool published_playing_ = false;
    float published_tempo_display_ = 120.0f;
    bool published_sync_external_source_ = false;
    bool published_tempo_locked_ = false;
    bool published_transport_locked_ = false;
    core::state::ClockSourceActive published_active_source_ =
        core::state::ClockSourceActive::INTERNAL;
    bool published_external_clock_present_ = false;
    bool projected_playing_ = false;
    bool projected_playing_dirty_ = false;
    float projected_tempo_display_ = 120.0f;
    bool projected_tempo_display_dirty_ = false;
    bool projected_sync_external_source_ = false;
    bool projected_sync_external_source_dirty_ = false;
    bool projected_tempo_locked_ = false;
    bool projected_tempo_locked_dirty_ = false;
    bool projected_transport_locked_ = false;
    bool projected_transport_locked_dirty_ = false;
    core::state::ClockSourceActive projected_active_source_ =
        core::state::ClockSourceActive::INTERNAL;
    bool projected_active_source_dirty_ = false;
    bool projected_external_clock_present_ = false;
    bool projected_external_clock_present_dirty_ = false;
    bool pending_sync_input_pulse_ = false;
};

}  // namespace core::sequencer

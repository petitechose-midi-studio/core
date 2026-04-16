#pragma once

#include <array>
#include <cstdint>

#include <oc/api/MidiAPI.hpp>

#include "sequencer/InternalTransportClock.hpp"
#include "state/MidiSyncState.hpp"

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
    struct UiProjectionSnapshot {
        enum DirtyBits : uint8_t {
            PLAYING = 1U << 0,
            TEMPO_DISPLAY = 1U << 1,
            SYNC_EXTERNAL_SOURCE = 1U << 2,
            TEMPO_LOCKED = 1U << 3,
            TRANSPORT_LOCKED = 1U << 4,
            ACTIVE_SOURCE = 1U << 5,
            EXTERNAL_CLOCK_PRESENT = 1U << 6,
        };

        uint8_t dirtyMask = 0;
        bool playing = false;
        float tempoDisplay = 120.0f;
        bool syncExternalSource = false;
        bool tempoLocked = false;
        bool transportLocked = false;
        core::state::ClockSourceActive activeSource = core::state::ClockSourceActive::INTERNAL;
        bool externalClockPresent = false;
        bool syncInputPulse = false;
    };

    explicit MidiClockSyncService(oc::api::MidiAPI& midi);

    void update(const MidiClockSyncRuntimeConfig& config, uint32_t nowMs, bool driveTransport = true);

    void onClock(uint64_t timestampUs, uint32_t hostNowMs);
    void onStart();
    void onContinue();
    void onStop();
    UiProjectionSnapshot takeUiProjectionSnapshot();

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

    struct UiProjectionState {
        bool playing = false;
        float tempoDisplay = 120.0f;
        bool syncExternalSource = false;
        bool tempoLocked = false;
        bool transportLocked = false;
        core::state::ClockSourceActive activeSource = core::state::ClockSourceActive::INTERNAL;
        bool externalClockPresent = false;
    };

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
    UiProjectionState published_ui_state_{};
    UiProjectionState projected_ui_state_{};
    uint8_t projected_ui_dirty_mask_ = 0;
    bool pending_sync_input_pulse_ = false;
};

}  // namespace core::sequencer

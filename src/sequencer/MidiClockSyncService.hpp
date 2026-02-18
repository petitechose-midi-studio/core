#pragma once

#include <array>
#include <cstdint>

#include <oc/api/MidiAPI.hpp>
#include <oc/note/clock/InternalClock.hpp>

#include "state/MidiSyncState.hpp"
#include "state/StatusBarState.hpp"

namespace core::sequencer {

class MidiClockSyncService {
public:
    MidiClockSyncService(core::state::MidiSyncState& syncState,
                         core::state::StatusBarState& statusBar,
                         oc::api::MidiAPI& midi);

    void update(uint32_t nowMs);

    void onClock(uint64_t timestampUs, uint32_t hostNowMs);
    void onStart();
    void onContinue();
    void onStop();

    uint32_t tick() const { return current_tick_; }
    bool playing() const { return current_playing_; }

    bool consumeResyncRequest();

private:
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

    oc::note::clock::InternalClock internal_clock_;

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
};

}  // namespace core::sequencer

#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <oc/note/sequencer/StepSequencerEngine.hpp>
#include "sequencer/RealtimeMidiQueue.hpp"
#include "sequencer/SequencerMidiEventSink.hpp"
#include "sequencer/SequencerRuntimeGraphBank.hpp"
#include "sequencer/SequencerRuntimeStateSync.hpp"
#include "state/StatusBarState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::sequencer {

/**
 * Playback bridge from committed sequencer snapshots to note-engine runtime.
 *
 * The service owns per-track `StepSequencerEngine` instances and their event
 * sinks. It consumes immutable track-bank snapshots, queues MIDI through
 * `RealtimeMidiQueue`, and publishes only telemetry/UI projection back to state.
 */
class SequencerPlaybackService {
public:
    static constexpr uint8_t TRACK_COUNT = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    struct UiProjectionSnapshot {
        bool noteOutPulse = false;
        bool beatPulse = false;
        std::array<uint8_t, TRACK_COUNT> trackVelocity{};
    };

    SequencerPlaybackService(core::state::sequencer::SequencerState& sequencer,
                             core::state::StatusBarState& statusBar,
                             RealtimeMidiQueue& midiQueue,
                             const SequencerRuntimeGraphBank& runtimeGraphBank);

    void update(const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
                uint32_t tick,
                bool playing,
                uint32_t nowUs,
                uint32_t tickPeriodUs,
                bool publishRuntimeState = true);
    void stopTrack(uint8_t trackIndex);
    void completeStop();
    void publishUiProjection(const UiProjectionSnapshot& projection, uint32_t nowMs);
    UiProjectionSnapshot takeUiProjectionSnapshot();
    SequencerRuntimeTelemetrySnapshot copyActiveRuntimeTelemetry() const;
    void publishUiState(uint32_t nowMs);

private:
    const oc::note::sequencer::StepSequencerRuntimeState& activeRuntimeState_() const;

    struct PendingUiProjection {
        bool noteOutPulse = false;
        bool beatPulse = false;
        std::array<uint8_t, TRACK_COUNT> trackVelocity{};

        void reset();
        void recordNoteOn(uint8_t trackIndex, uint8_t velocity);
    };

    class PendingNoteActivityObserver final : public SequencerMidiEventSinkObserver {
    public:
        explicit PendingNoteActivityObserver(PendingUiProjection& pendingUiProjection);

        void onNoteOn(uint8_t trackIndex, uint8_t velocity) override;

    private:
        PendingUiProjection& pending_ui_projection_;
    };

    void handleActiveTrackSwitch_();
    void syncRuntimeStates_(
        const core::state::sequencer::SequencerTrackBankSnapshot& snapshot
    );

    core::state::sequencer::SequencerState& sequencer_;
    core::state::StatusBarState& status_bar_;
    const SequencerRuntimeGraphBank& runtime_graph_bank_;
    PendingUiProjection pending_ui_projection_{};
    PendingNoteActivityObserver note_activity_observer_{pending_ui_projection_};
    // SequencerRealtimeLane owns this service in one RAM2 allocation. Keeping
    // the fixed track topology inline avoids 33 secondary heap allocations and
    // leaves the 1 kHz engine state out of PSRAM.
    std::array<oc::note::sequencer::StepSequencerRuntimeState, TRACK_COUNT>
        track_runtime_states_{};
    std::array<SequencerRuntimeStateSignature, TRACK_COUNT> track_runtime_signatures_{};
    std::array<std::optional<SequencerMidiEventSink>, TRACK_COUNT> track_event_sinks_{};
    std::array<
        std::optional<oc::note::sequencer::StepSequencerEngine>,
        TRACK_COUNT> track_engines_{};
    uint8_t runtime_active_track_ = 0;
    uint16_t runtime_enabled_mask_ = 0x0001;
    uint16_t runtime_muted_mask_ = 0;

    int16_t last_playhead_ = -1;
    uint8_t last_active_track_ = 0;
};

}  // namespace core::sequencer

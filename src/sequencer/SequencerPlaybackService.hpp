#pragma once

#include <array>
#include <memory>

#include <cstdint>

#include <oc/note/sequencer/StepSequencerEngine.hpp>

#include "sequencer/RealtimeMidiQueue.hpp"
#include "sequencer/SequencerMidiEventSink.hpp"
#include "sequencer/SequencerPlaybackProfiler.hpp"
#include "sequencer/SequencerRuntimeStateSync.hpp"
#include "state/StatusBarState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::sequencer {

class SequencerPlaybackService {
public:
    static constexpr uint8_t TRACK_COUNT = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    struct UiProjectionSnapshot {
        bool noteOutPulse = false;
        bool beatPulse = false;
        std::array<uint8_t, TRACK_COUNT> trackVelocity{};
    };

    using ProfilingSnapshot = SequencerPlaybackProfiler::Snapshot;

    SequencerPlaybackService(core::state::sequencer::SequencerState& sequencer,
                             core::state::sequencer::SequencerTrackBankState& trackBank,
                             core::state::StatusBarState& statusBar,
                             RealtimeMidiQueue& midiQueue);

    void update(const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
                uint32_t tick,
                bool playing,
                uint32_t nowMs,
                uint32_t nowUs,
                uint32_t tickPeriodUs,
                bool publishRuntimeState = true,
                bool emitLogs = true);
    void stop();
    void publishUiProjection(const UiProjectionSnapshot& projection, uint32_t nowMs);
    UiProjectionSnapshot takeUiProjectionSnapshot();
    SequencerRuntimeTelemetrySnapshot copyActiveRuntimeTelemetry() const;
    bool takeProfilingSnapshot(uint32_t nowMs, ProfilingSnapshot& snapshot);
    void publishUiState(uint32_t nowMs);

private:
    const oc::note::sequencer::StepSequencerRuntimeState& activeRuntimeState_() const;

    struct PendingNoteActivity {
        bool noteOutActive = false;
        uint32_t noteOnCount = 0;
        uint32_t noteOffCount = 0;
        uint32_t panicNoteOffCount = 0;
        uint32_t queuedEventCount = 0;
        std::array<uint8_t, TRACK_COUNT> trackVelocity{};

        void reset();
        void recordNoteOn(uint8_t trackIndex, uint8_t velocity);
        void recordNoteOff();
        void recordPanicNoteOffs(uint32_t count);
        SequencerPlaybackActivitySnapshot snapshot() const;
    };

    class PendingNoteActivityObserver final : public SequencerMidiEventSinkObserver {
    public:
        explicit PendingNoteActivityObserver(PendingNoteActivity& pendingNoteActivity);

        void onNoteOn(uint8_t trackIndex, uint8_t velocity) override;
        void onNoteOff() override;
        void onPanicNoteOffs(uint32_t count) override;

    private:
        PendingNoteActivity& pending_note_activity_;
    };

    struct PendingUiProjection {
        bool noteOutPulse = false;
        bool beatPulse = false;
        std::array<uint8_t, TRACK_COUNT> trackVelocity{};

        void reset();
    };

    void handleActiveTrackSwitch_();
    void syncRuntimeStates_(const core::state::sequencer::SequencerTrackBankSnapshot& snapshot);
    void recordProfiling_(uint32_t tick, bool playing, uint32_t updateUs, uint32_t nowMs);
    void collectUiProjection_();

    core::state::sequencer::SequencerState& sequencer_;
    core::state::StatusBarState& status_bar_;
    PendingNoteActivity pending_note_activity_{};
    PendingUiProjection pending_ui_projection_{};
    PendingNoteActivityObserver note_activity_observer_{pending_note_activity_};
    SequencerPlaybackProfiler profiler_{};
    // Keep the per-track runtime bank on the regular heap so the timer/playback
    // engines do not read their hottest state back from the PSRAM-backed parent.
    std::unique_ptr<oc::note::sequencer::StepSequencerRuntimeState[]> track_runtime_states_{};
    std::array<SequencerRuntimeStateSignature, TRACK_COUNT> track_runtime_signatures_{};
    std::array<std::unique_ptr<SequencerMidiEventSink>, TRACK_COUNT> track_event_sinks_{};
    std::array<std::unique_ptr<oc::note::sequencer::StepSequencerEngine>, TRACK_COUNT> track_engines_{};
    uint8_t runtime_active_track_ = 0;
    uint16_t runtime_enabled_mask_ = 0x0001;

    int16_t last_playhead_ = -1;
    uint8_t last_active_track_ = 0;
};

}  // namespace core::sequencer

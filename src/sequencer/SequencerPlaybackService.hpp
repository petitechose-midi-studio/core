#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

#include <oc/note/sequencer/StepSequencerEngine.hpp>
#include "app/ExtmemAllocator.hpp"
#include "sequencer/DrumPlaybackEngine.hpp"
#include "sequencer/RealtimeMidiQueue.hpp"
#include "sequencer/SequencerMidiEventSink.hpp"
#include "sequencer/SequencerCcLaneRuntime.hpp"
#include "sequencer/SequencerRuntimeGraphBank.hpp"
#include "sequencer/SequencerRuntimeStateSync.hpp"
#include "state/StatusBarState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"

namespace core::sequencer {

struct SequencerCcLaneRuntimeProjectSnapshot;
struct SequencerDrumRuntimeProjectSnapshot;
struct ProjectTrackRuntimeSnapshot;
class MidiCcGlobalFrameCoordinator;

/** Fixed tick scratch; production ownership is one PSRAM allocation. */
struct SequencerCcTemporalRuntimeScratch {
    SequencerCcLaneRuntime::Inputs currentInputs{};
    SequencerCcLaneRuntime::Inputs predictiveInputs{};
    SequencerCcLaneRuntimeFrame currentFrame{};
    SequencerCcLaneRuntimeFrame temporalFrame{};
    SequencerCcLaneRuntimeFrame projectedFrame{};
};

static_assert(sizeof(SequencerCcTemporalRuntimeScratch) < 8U * 1024U);

/** One PSRAM cache for the currently inspected Drum viewport only. */
struct SequencerDrumResolvedProjectionCache {
    DrumResolvedPageSignature signature{};
    core::state::sequencer::DrumResolvedPageProjection projection{};
    bool valid = false;

    void invalidate() {
        signature = {};
        projection.reset();
        valid = false;
    }
};

static_assert(sizeof(SequencerDrumResolvedProjectionCache) < 640U);
}

namespace core::sequencer {

/**
 * Playback bridge from committed sequencer snapshots to note-engine runtime.
 *
 * The service owns one active melodic or Drum engine and one event sink per
 * Track. It consumes immutable track-bank snapshots, queues MIDI through
 * `RealtimeMidiQueue`, and publishes only telemetry/UI projection back to state.
 */
class SequencerPlaybackService {
public:
    static constexpr uint8_t TRACK_COUNT = core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    struct UiProjectionSnapshot {
        bool noteOutPulse = false;
        bool ccOutPulse = false;
        bool beatPulse = false;
        std::array<uint8_t, TRACK_COUNT> trackVelocity{};
        std::array<
            uint8_t,
            core::state::sequencer::DrumSequencerState::
                RUNTIME_LANE_CAPACITY> drumLaneSteps{};
        std::array<
            uint8_t,
            core::state::sequencer::DrumSequencerState::
                RUNTIME_LANE_CAPACITY> drumLanePhaseQ8{};
        std::array<
            uint8_t,
            core::state::sequencer::DrumSequencerState::
                RUNTIME_LANE_CAPACITY> drumLaneDecisionSteps{};
        uint16_t drumLaneValidMask = 0U;
        uint16_t drumLaneDecisionValidMask = 0U;
        uint16_t drumLaneDecisionPlayedMask = 0U;
        core::state::sequencer::DrumResolvedPageProjection
            drumResolvedPage{};
        bool drumPlaying = false;
    };

    SequencerPlaybackService(core::state::sequencer::SequencerState& sequencer,
                             core::state::StatusBarState& statusBar,
                             RealtimeMidiQueue& midiQueue,
                             const SequencerRuntimeGraphBank& runtimeGraphBank,
                             core::state::sequencer::SequencerTrackActivationQueue*
                                 trackActivations = nullptr,
                             SequencerCcLaneRuntime* ccLaneRuntime = nullptr,
                             MidiCcGlobalFrameCoordinator* ccCoordinator =
                                 nullptr,
                             SequencerCcLaneRuntime* ccPredictiveLaneRuntime = nullptr);

    /**
     * Advances playback against one coherent publication generation.
     *
     * `projectTracks` is the mandatory Project-owned routing/mix/timing
     * authority paired with `snapshot`; production has no Sequencer-mirror
     * fallback.
     */
    void update(const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
                uint32_t tick,
                bool playing,
                uint32_t nowUs,
                uint32_t tickPeriodUs,
                const ProjectTrackRuntimeSnapshot& projectTracks,
                bool publishRuntimeState = true,
                const SequencerCcLaneRuntimeProjectSnapshot* ccLaneSnapshot = nullptr,
                bool allowPredictiveLookahead = false,
                const SequencerDrumRuntimeProjectSnapshot* drumSnapshot = nullptr
                );
    void stopTrack(uint8_t trackIndex);
    void completeStop();
    void markCcTransportStopped();
    void resetCcProject();
    void publishUiProjection(const UiProjectionSnapshot& projection, uint32_t nowMs);
    UiProjectionSnapshot takeUiProjectionSnapshot();
    SequencerRuntimeTelemetrySnapshot copyActiveRuntimeTelemetry() const;
    void publishUiState(uint32_t nowMs);
    [[nodiscard]] bool ccTemporalScratchValid() const {
        return cc_temporal_scratch_ != nullptr;
    }

private:
    using TrackEngineSlot = std::variant<
        std::monostate,
        oc::note::sequencer::StepSequencerEngine,
        DrumPlaybackEngine>;

    const oc::note::sequencer::StepSequencerRuntimeState& activeRuntimeState_() const;
    oc::note::sequencer::StepSequencerEngine* melodicEngine_(uint8_t trackIndex);
    DrumPlaybackEngine* drumEngine_(uint8_t trackIndex);
    bool selectTrackEngine_(uint8_t trackIndex, bool drum);
    void resetTrackEngine_(uint8_t trackIndex);

    struct PendingUiProjection {
        bool noteOutPulse = false;
        bool ccOutPulse = false;
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
        const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
        const ProjectTrackRuntimeSnapshot& projectTracks,
        uint32_t tick,
        bool playing
    );
    void reconcileProjectTracks_(
        const ProjectTrackRuntimeSnapshot& projectTracks,
        uint32_t tick,
        bool playing,
        uint32_t nowUs,
        uint32_t tickPeriodUs,
        bool allowPredictiveLookahead
    );
    bool isLocalLoopBoundary_(uint8_t trackIndex, uint32_t tick) const;
    void syncRuntimeMasksForTrack_(
        const ProjectTrackRuntimeSnapshot& projectTracks,
        uint8_t trackIndex
    );
    void applyStagedTrack_(
        const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
        const ProjectTrackRuntimeSnapshot& projectTracks,
        uint8_t trackIndex,
        uint32_t generation,
        uint32_t tick,
        bool playing
    );
    void processCcRuntime_(
        const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
        const SequencerCcLaneRuntimeProjectSnapshot* laneSnapshot,
        const ProjectTrackRuntimeSnapshot& projectTracks,
        uint32_t tick,
        bool playing,
        uint32_t nowUs,
        uint32_t tickPeriodUs,
        bool allowPredictiveLookahead
    );
    static uint8_t ccTicksPerStep_(
        const core::state::sequencer::SequencerPatternSnapshot& pattern
    );

    core::state::sequencer::SequencerState& sequencer_;
    core::state::StatusBarState& status_bar_;
    RealtimeMidiQueue& midi_queue_;
    const SequencerRuntimeGraphBank& runtime_graph_bank_;
    core::state::sequencer::SequencerTrackActivationQueue* track_activations_ = nullptr;
    SequencerCcLaneRuntime* cc_lane_runtime_ = nullptr;
    SequencerCcLaneRuntime* cc_predictive_lane_runtime_ = nullptr;
    MidiCcGlobalFrameCoordinator* cc_coordinator_ = nullptr;
    core::app::ExtmemUniquePtr<SequencerCcTemporalRuntimeScratch>
        cc_temporal_scratch_;
    // Advanced-content previews are noncritical UI data. Keeping the single
    // active viewport cache in PSRAM avoids multiplying it across 16 hot Drum
    // engines in RAM2.
    core::app::ExtmemUniquePtr<SequencerDrumResolvedProjectionCache>
        drum_resolved_projection_cache_;
    PendingUiProjection pending_ui_projection_{};
    PendingNoteActivityObserver note_activity_observer_{pending_ui_projection_};
    // SequencerRealtimeLane owns this service in one RAM2 allocation. Keeping
    // the fixed track topology inline avoids 33 secondary heap allocations and
    // leaves the 1 kHz engine state out of PSRAM.
    std::array<oc::note::sequencer::StepSequencerRuntimeState, TRACK_COUNT>
        track_runtime_states_{};
    std::array<SequencerRuntimeStateSignature, TRACK_COUNT> track_runtime_signatures_{};
    std::array<std::optional<SequencerMidiEventSink>, TRACK_COUNT> track_event_sinks_{};
    // A Track is either melodic or Drum. Reusing one inline scheduler slot per
    // Track keeps the timer lane allocation-free without reserving both engine
    // types at once. The active engine remains in RAM2; authored payloads stay
    // in PSRAM.
    std::array<TrackEngineSlot, TRACK_COUNT> track_engines_{};
    std::array<
        oc::note::sequencer::StepSequencerPlaybackRegion,
        TRACK_COUNT> track_playback_regions_{};
    uint16_t runtime_drum_mask_ = 0U;
    uint8_t runtime_active_track_ = 0;
    uint16_t runtime_enabled_mask_ = 0x0001;
    uint16_t runtime_audible_mask_ = 0x0001;
    std::array<uint8_t, TRACK_COUNT> runtime_track_channels_{};
    std::array<int16_t, TRACK_COUNT> runtime_track_delays_ms_{};
    uint16_t runtime_resync_mask_ = 0U;
    bool runtime_project_tracks_initialized_ = false;
    uint32_t runtime_tick_period_us_ = 0U;
    uint32_t runtime_transport_tick_ = 0U;
    uint32_t runtime_tick_anchor_us_ = 0U;
    bool runtime_tick_anchor_valid_ = false;
    bool runtime_predictive_lookahead_ = false;

    int16_t last_playhead_ = -1;
    uint8_t last_active_track_ = 0;
    uint32_t last_cc_tick_ = 0;
    bool cc_transport_playing_ = false;
};

}  // namespace core::sequencer

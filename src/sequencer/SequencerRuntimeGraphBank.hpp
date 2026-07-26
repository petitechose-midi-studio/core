#pragma once

#include <array>
#include <cstdint>
#include <utility>

#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/realtime/InterruptGuard.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::sequencer {

/**
 * PSRAM-backed immutable graph generation consumed by realtime playback.
 *
 * prepare() clones every changed graph from the mutable editor state without
 * touching the active generation. publishPrepared() then swaps those pointers
 * under one interrupt guard and executes the companion publisher in the same
 * critical section. The runtime uses that companion to publish the matching
 * flat snapshot, so timer playback can never observe mixed generations.
 */
class SequencerRuntimeGraphBank {
public:
    static constexpr uint8_t TRACK_COUNT =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    bool prepare(const core::state::sequencer::SequencerState& sequencer,
                 const core::state::sequencer::SequencerTrackBankState& trackBank);

    template <typename CompanionPublisher>
    void publishPrepared(CompanionPublisher&& publishCompanion) {
        {
            oc::realtime::InterruptGuard lock;
            commitPrepared_();
            std::forward<CompanionPublisher>(publishCompanion)();
        }
        finishPublication_();
    }

    void publishPrepared() {
        publishPrepared([]() {});
    }

    /** Abandons an unpublished generation when its companion snapshot failed. */
    void discardPrepared() { discardPrepared_(); }

    const oc::note::sequencer::StepSequencerGraph* graphForTrack(
        uint8_t trackIndex
    ) const;

private:
    using Graph = oc::note::sequencer::StepSequencerGraph;
    using GraphPtr = core::app::ExtmemUniquePtr<Graph>;

    struct SourceSignature {
        const Graph* source = nullptr;
        uint32_t revision = 0;
        uint32_t draftRevision = 0;

        bool matches(const SourceSignature& other) const {
            return source == other.source && revision == other.revision &&
                   draftRevision == other.draftRevision;
        }
    };

    void commitPrepared_();
    void finishPublication_();
    void discardPrepared_();

    std::array<GraphPtr, TRACK_COUNT> active_graphs_{};
    std::array<SourceSignature, TRACK_COUNT> source_signatures_{};
    std::array<GraphPtr, TRACK_COUNT> prepared_graphs_{};
    std::array<SourceSignature, TRACK_COUNT> prepared_signatures_{};
    GraphPtr staging_graph_{};
    uint16_t prepared_mask_ = 0;
    bool allocation_failure_reported_ = false;
};

}  // namespace core::sequencer

#pragma once

#include <array>
#include <cstdint>
#include <utility>

#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/realtime/InterruptGuard.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerClipLaunchQueue.hpp"
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
                 const core::state::sequencer::SequencerTrackBankState& trackBank,
                 const core::state::sequencer::SequencerClipRuntimeSources& sources,
                 uint16_t retainActiveMask = 0U);
    bool prepare(
        const core::state::sequencer::SequencerState& sequencer,
        const core::state::sequencer::SequencerTrackBankState& trackBank
    ) {
        core::state::sequencer::SequencerClipRuntimeSources sources{};
        return prepare(sequencer, trackBank, sources);
    }

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
    const oc::note::sequencer::StepSequencerGraph* graphBeforeRetainedLaunch(
        uint8_t trackIndex
    ) const;
    void releaseRetired(uint16_t trackMask);
    void releaseAllRetired();

    /**
     * Restores the graph generation retained for a staged Clip/Scene launch.
     * The companion restores the matching flat snapshot in the same critical
     * section, so realtime playback never observes a mixed rollback.
     */
    template <typename CompanionPublisher>
    void rollbackRetained(
        uint16_t trackMask,
        CompanionPublisher&& publishCompanion
    ) {
        const uint16_t rollbackMask = static_cast<uint16_t>(
            trackMask & retired_valid_mask_);
        {
            oc::realtime::InterruptGuard lock;
            for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
                const uint16_t bit = static_cast<uint16_t>(1U << track);
                if ((rollbackMask & bit) == 0U) continue;
                active_graphs_[track].swap(retired_graphs_[track]);
                std::swap(
                    source_signatures_[track],
                    retired_signatures_[track]
                );
            }
            std::forward<CompanionPublisher>(publishCompanion)();
        }
        releaseRetired(rollbackMask);
    }

private:
    using Graph = oc::note::sequencer::StepSequencerGraph;
    using GraphPtr = core::app::ExtmemUniquePtr<Graph>;

    struct SourceSignature {
        const Graph* source = nullptr;
        uint32_t revision = 0;
        uint32_t draftRevision = 0;
        uint32_t clipGeneration = 0;
        uint8_t clipSlot =
            core::state::sequencer::SequencerClipGridState::INVALID_SLOT;

        bool matches(const SourceSignature& other) const {
            return source == other.source && revision == other.revision &&
                   draftRevision == other.draftRevision &&
                   clipGeneration == other.clipGeneration &&
                   clipSlot == other.clipSlot;
        }
    };

    void commitPrepared_();
    void finishPublication_();
    void discardPrepared_();

    std::array<GraphPtr, TRACK_COUNT> active_graphs_{};
    std::array<SourceSignature, TRACK_COUNT> source_signatures_{};
    std::array<GraphPtr, TRACK_COUNT> prepared_graphs_{};
    std::array<GraphPtr, TRACK_COUNT> retired_graphs_{};
    std::array<SourceSignature, TRACK_COUNT> prepared_signatures_{};
    std::array<SourceSignature, TRACK_COUNT> retired_signatures_{};
    GraphPtr staging_graph_{};
    uint16_t prepared_mask_ = 0;
    uint16_t retain_active_mask_ = 0;
    uint16_t retired_valid_mask_ = 0;
    bool allocation_failure_reported_ = false;
};

}  // namespace core::sequencer

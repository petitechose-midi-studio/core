#include "sequencer/SequencerRuntimeGraphBank.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::sequencer {

// Graph preparation allocates/copies a future generation from the main loop.
// Realtime playback only dereferences the already-published graph pointers.
FLASHMEM bool SequencerRuntimeGraphBank::prepare(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& trackBank
) {
    static_assert(TRACK_COUNT <= 16, "prepared graph mask capacity exceeded");
    discardPrepared_();

    const uint8_t activeTrack =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            trackBank.activeTrackIndex()
        );
    const auto* quickControlsPattern =
        sequencer.quickControlsDraft.previewPattern();

    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const bool active = track == activeTrack;
        const auto& sourceState = active
            ? (quickControlsPattern != nullptr
                ? *quickControlsPattern
                : sequencer.pattern)
            : trackBank.track(track);
        const auto* sourceGraph = core::state::sequencer::graphView(sourceState);
        const bool quickControlsPreview = active && quickControlsPattern != nullptr;
        const bool stepDraftProjection = active && !quickControlsPreview &&
            sequencer.stepContentDraft.active.get();
        const SourceSignature signature{
            .source = sourceGraph,
            .revision = sourceState.graphRevision.get(),
            .draftRevision = quickControlsPreview
                ? sequencer.patternQuickControls.previewRevision.get()
                : (stepDraftProjection
                    ? sequencer.stepContentDraft.revision.get()
                    : 0U),
        };
        if (source_signatures_[track].matches(signature)) continue;

        const uint16_t trackBit = static_cast<uint16_t>(1U << track);
        prepared_mask_ = static_cast<uint16_t>(prepared_mask_ | trackBit);
        prepared_signatures_[track] = signature;
        if (!sourceGraph && !stepDraftProjection) continue;

        if (staging_graph_) {
            prepared_graphs_[track] = std::move(staging_graph_);
        } else {
            prepared_graphs_[track] = core::app::makeExtmemUnique<Graph>();
        }

        if (prepared_graphs_[track]) {
            const bool captured = stepDraftProjection
                ? core::state::sequencer::captureStepContentDraftRuntimeGraph(
                      sequencer,
                      *prepared_graphs_[track]
                  )
                : ((*prepared_graphs_[track] = *sourceGraph), true);
            if (captured) continue;
        }

        discardPrepared_();
        if (!allocation_failure_reported_) {
            OC_LOG_ERROR("{}", "[SequencerRuntimeGraphBank] PSRAM allocation failed");
            allocation_failure_reported_ = true;
        }
        return false;
    }

    allocation_failure_reported_ = false;
    return true;
}

void SequencerRuntimeGraphBank::commitPrepared_() {
    if (prepared_mask_ == 0) return;
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const uint16_t trackBit = static_cast<uint16_t>(1U << track);
        if ((prepared_mask_ & trackBit) == 0) continue;
        active_graphs_[track].swap(prepared_graphs_[track]);
    }
}

FLASHMEM void SequencerRuntimeGraphBank::finishPublication_() {
    if (prepared_mask_ == 0) return;
    // Retain one old allocation as scratch for the common single-track edit.
    // Other obsolete generations are released outside the interrupt guard.
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const uint16_t trackBit = static_cast<uint16_t>(1U << track);
        if ((prepared_mask_ & trackBit) == 0) continue;

        source_signatures_[track] = prepared_signatures_[track];
        prepared_signatures_[track] = {};
        if (!staging_graph_ && prepared_graphs_[track]) {
            staging_graph_ = std::move(prepared_graphs_[track]);
        } else {
            prepared_graphs_[track].reset();
        }
    }
    prepared_mask_ = 0;
}

FLASHMEM void SequencerRuntimeGraphBank::discardPrepared_() {
    if (prepared_mask_ == 0) return;
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const uint16_t trackBit = static_cast<uint16_t>(1U << track);
        if ((prepared_mask_ & trackBit) == 0) continue;

        prepared_signatures_[track] = {};
        if (!staging_graph_ && prepared_graphs_[track]) {
            staging_graph_ = std::move(prepared_graphs_[track]);
        } else {
            prepared_graphs_[track].reset();
        }
    }
    prepared_mask_ = 0;
}

const oc::note::sequencer::StepSequencerGraph*
SequencerRuntimeGraphBank::graphForTrack(uint8_t trackIndex) const {
    if (trackIndex >= TRACK_COUNT) return nullptr;
    return active_graphs_[trackIndex].get();
}

}  // namespace core::sequencer

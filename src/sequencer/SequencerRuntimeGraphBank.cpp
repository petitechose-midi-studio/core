#include "sequencer/SequencerRuntimeGraphBank.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::sequencer {

// Graph preparation allocates/copies a future generation from the main loop.
// Realtime playback only dereferences the already-published graph pointers.
FLASHMEM bool SequencerRuntimeGraphBank::prepare(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& trackBank,
    const core::state::sequencer::SequencerClipRuntimeSources& sources,
    uint16_t retainActiveMask
) {
    static_assert(TRACK_COUNT <= 16, "prepared graph mask capacity exceeded");
    discardPrepared_();
    retain_active_mask_ = retainActiveMask;

    const uint8_t activeTrack =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            trackBank.activeTrackIndex()
        );
    const auto* quickControlsPattern =
        sequencer.quickControlsDraft.previewPattern();

    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const bool active = track == activeTrack;
        const auto& clipSource = sources[track];
        const bool inactiveClip = clipSource.document != nullptr;
        const auto& residentState = active && quickControlsPattern != nullptr
            ? *quickControlsPattern
            : trackBank.track(track);
        const auto* sourceGraph = inactiveClip
            ? clipSource.document->pattern.graph.get()
            : core::state::sequencer::graphView(residentState);
        const bool quickControlsPreview = !inactiveClip && active &&
            quickControlsPattern != nullptr;
        const bool stepDraftProjection = !inactiveClip && active &&
            !quickControlsPreview && sequencer.stepContentDraft.active.get();
        const SourceSignature signature{
            .source = sourceGraph,
            .revision = inactiveClip
                ? clipSource.document->pattern.graphRevision
                : residentState.graphRevision,
            .draftRevision = quickControlsPreview
                ? sequencer.patternQuickControls.previewRevision.get()
                : (stepDraftProjection
                    ? sequencer.stepContentDraft.revision.get()
                    : 0U),
            .clipGeneration = clipSource.generation,
            .clipSlot = clipSource.address.slot,
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
    if (prepared_mask_ == 0) {
        retain_active_mask_ = 0U;
        return;
    }
    // Retain one old allocation as scratch for the common single-track edit.
    // Other obsolete generations are released outside the interrupt guard.
    for (uint8_t track = 0; track < TRACK_COUNT; ++track) {
        const uint16_t trackBit = static_cast<uint16_t>(1U << track);
        if ((prepared_mask_ & trackBit) == 0) continue;

        if ((retain_active_mask_ & trackBit) != 0U) {
            retired_signatures_[track] = source_signatures_[track];
            retired_graphs_[track] = std::move(prepared_graphs_[track]);
            retired_valid_mask_ = static_cast<uint16_t>(
                retired_valid_mask_ | trackBit);
        } else if (!staging_graph_ && prepared_graphs_[track]) {
            staging_graph_ = std::move(prepared_graphs_[track]);
        } else {
            prepared_graphs_[track].reset();
        }
        source_signatures_[track] = prepared_signatures_[track];
        prepared_signatures_[track] = {};
    }
    prepared_mask_ = 0;
    retain_active_mask_ = 0U;
}

FLASHMEM void SequencerRuntimeGraphBank::discardPrepared_() {
    if (prepared_mask_ == 0) {
        retain_active_mask_ = 0U;
        return;
    }
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
    retain_active_mask_ = 0U;
}

const oc::note::sequencer::StepSequencerGraph*
SequencerRuntimeGraphBank::graphForTrack(uint8_t trackIndex) const {
    if (trackIndex >= TRACK_COUNT) return nullptr;
    return active_graphs_[trackIndex].get();
}

const oc::note::sequencer::StepSequencerGraph*
SequencerRuntimeGraphBank::graphBeforeRetainedLaunch(uint8_t trackIndex) const {
    if (trackIndex >= TRACK_COUNT) return nullptr;
    return retired_graphs_[trackIndex]
        ? retired_graphs_[trackIndex].get()
        : active_graphs_[trackIndex].get();
}

FLASHMEM void SequencerRuntimeGraphBank::releaseRetired(uint16_t trackMask) {
    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        if ((trackMask & bit) == 0U) continue;
        retired_graphs_[track].reset();
        retired_signatures_[track] = {};
        retired_valid_mask_ = static_cast<uint16_t>(
            retired_valid_mask_ & static_cast<uint16_t>(~bit));
    }
}

FLASHMEM void SequencerRuntimeGraphBank::releaseAllRetired() {
    for (auto& graph : retired_graphs_) graph.reset();
    retired_signatures_.fill({});
    retired_valid_mask_ = 0U;
}

}  // namespace core::sequencer

#include "state/sequencer/SequencerQuickControlsDraft.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

FLASHMEM bool SequencerQuickControlsDraftSession::begin(
    const SequencerPatternState& published,
    const SequencerPreparedGraphContentPath& openingPath,
    uint8_t openingPage,
    uint8_t openingFocusedStep
) {
    if (draft_ || !openingPath.valid ||
        openingPath.stackDepth > openingPath.frames.size()) {
        return false;
    }
    if (published.graph != nullptr) {
        const bool validGraph = published.graph->enabled
            ? validInitializedSequencerGraph(*published.graph)
            : isCanonicalDisabledSequencerGraph(*published.graph);
        if (!validGraph) return false;
    }
    if (published.ccLanes != nullptr &&
        !validSequencerCcLaneBank(*published.ccLanes)) {
        return false;
    }

    auto candidate =
        core::app::makeExtmemUniqueCold<SequencerQuickControlsDraft>();
    if (!candidate) return false;

    core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> graph;
    if (published.graph != nullptr) {
        graph = core::app::makeExtmemUnique<
            oc::note::sequencer::StepSequencerGraph>(*published.graph);
        if (!graph) return false;
    }

    SequencerCcLaneBankPtr ccLanes;
    if (published.ccLanes != nullptr) {
        ccLanes = core::app::makeExtmemUnique<SequencerCcLaneBank>(
            *published.ccLanes
        );
        if (!ccLanes) return false;
    }

    SequencerPatternSnapshot flat{};
    captureSnapshot(published, flat);
    applySnapshot(candidate->pattern, flat);
    candidate->pattern.graph = std::move(graph);
    candidate->pattern.graphRevision.set(published.graphRevision.get());
    candidate->pattern.ccLanes = std::move(ccLanes);
    copySequencerCcLaneRevision(candidate->pattern, published);

    auto& opening = candidate->openingView;
    opening.valid = true;
    opening.previewEnabled = true;
    opening.stackDepth = openingPath.stackDepth;
    opening.page = openingPage;
    opening.focusedStep = openingFocusedStep;
    for (uint8_t i = 0; i < opening.stackDepth; ++i) {
        opening.frames[i] = openingPath.frames[i];
    }

    draft_ = std::move(candidate);
    return true;
}

FLASHMEM bool SequencerQuickControlsDraftSession::active() const {
    return draft_ != nullptr && draft_->openingView.valid;
}

FLASHMEM SequencerPatternState* SequencerQuickControlsDraftSession::pattern() {
    return active() ? &draft_->pattern : nullptr;
}

FLASHMEM const SequencerPatternState*
SequencerQuickControlsDraftSession::pattern() const {
    return active() ? &draft_->pattern : nullptr;
}

FLASHMEM SequencerPatternState*
SequencerQuickControlsDraftSession::previewPattern() {
    return active() && draft_->openingView.previewEnabled
        ? &draft_->pattern
        : nullptr;
}

FLASHMEM const SequencerPatternState*
SequencerQuickControlsDraftSession::previewPattern() const {
    return active() && draft_->openingView.previewEnabled
        ? &draft_->pattern
        : nullptr;
}

FLASHMEM void SequencerQuickControlsDraftSession::suspendPreview() {
    if (active()) draft_->openingView.previewEnabled = false;
}

FLASHMEM void SequencerQuickControlsDraftSession::resumePreview() {
    if (active()) draft_->openingView.previewEnabled = true;
}

FLASHMEM SequencerQuickControlsNestedPublishOutcome
SequencerQuickControlsDraftSession::publishToDetachedParent(
    SequencerPatternState& parent
) {
    auto* candidate = pattern();
    if (candidate == nullptr || candidate == &parent) {
        return SequencerQuickControlsNestedPublishOutcome::Failed;
    }
    if (sameMusicalPatternState(parent, *candidate)) {
        return SequencerQuickControlsNestedPublishOutcome::NoChange;
    }

    SequencerPatternSnapshot flat{};
    captureSnapshot(*candidate, flat);
    std::swap(parent.graph, candidate->graph);
    std::swap(parent.ccLanes, candidate->ccLanes);
    applySnapshotPreservingGraph(parent, flat);
    copySequencerCcLaneRevision(parent, *candidate);
    return SequencerQuickControlsNestedPublishOutcome::Published;
}

FLASHMEM bool SequencerQuickControlsDraftSession::restoreOpeningView(
    SequencerState& sequencer
) const {
    if (!active()) return false;
    const auto& opening = draft_->openingView;
    SequencerPreparedGraphContentPath path{};
    path.valid = true;
    path.stackDepth = opening.stackDepth;
    for (uint8_t i = 0; i < path.stackDepth; ++i) {
        path.frames[i] = opening.frames[i];
    }
    publishPreparedSequencerGraphContentPath(sequencer, path);
    sequencer.page.set(opening.page);
    sequencer.focusedStep.set(opening.focusedStep);
    return true;
}

FLASHMEM void SequencerQuickControlsDraftSession::reset() {
    draft_.reset();
}

}  // namespace core::state::sequencer

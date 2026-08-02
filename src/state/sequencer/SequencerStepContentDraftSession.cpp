#include "state/sequencer/SequencerStepContentDraftSession.hpp"

#include <cstddef>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::state::sequencer {
namespace {

constexpr uint32_t FNV_OFFSET = 2166136261U;
constexpr uint32_t FNV_PRIME = 16777619U;

template <typename T>
FLASHMEM uint32_t hashObjectBytes(uint32_t hash, const T& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    for (size_t i = 0; i < sizeof(T); ++i) {
        hash = (hash ^ bytes[i]) * FNV_PRIME;
    }
    return hash;
}

FLASHMEM uint32_t graphFingerprint(const SequencerPatternState& pattern) {
    const auto* graph = graphView(pattern);
    if (graph == nullptr) return FNV_OFFSET;

    uint32_t hash = hashObjectBytes(FNV_OFFSET, graph->rootSequenceId);
    hash = hashObjectBytes(hash, graph->stepNodeCount);
    hash = hashObjectBytes(hash, graph->sequenceCount);
    hash = hashObjectBytes(hash, graph->cycleSetCount);
    for (uint16_t i = 0; i < graph->stepNodeCount; ++i) {
        hash = hashObjectBytes(hash, graph->stepNodes[i]);
    }
    for (uint8_t i = 0; i < graph->sequenceCount; ++i) {
        hash = hashObjectBytes(hash, graph->sequences[i]);
    }
    for (uint8_t i = 0; i < graph->cycleSetCount; ++i) {
        hash = hashObjectBytes(hash, graph->cycleSets[i]);
    }
    return hash;
}

FLASHMEM bool sameChordSpec(
    const oc::note::sequencer::StepSequencerChordSpec& lhs,
    const oc::note::sequencer::StepSequencerChordSpec& rhs
) {
    return oc::note::sequencer::chordSpecsEqual(lhs, rhs);
}

}  // namespace

FLASHMEM void SequencerStepContentDraftSession::bindRevisionSignal(
    RevisionSignal& signal
) {
    revisionSignal_ = &signal;
}

FLASHMEM void SequencerStepChordDraftState::reset() {
    *this = SequencerStepChordDraftState{};
}

FLASHMEM void SequencerStepChordDraftState::markPristine() {
    pristineModePresent = modePresent;
    pristineLocalPresent = localPresent;
    pristineMode = mode;
    pristineSpec = spec;
}

FLASHMEM bool SequencerStepChordDraftState::modified() const {
    return modePresent != pristineModePresent ||
           localPresent != pristineLocalPresent ||
           mode != pristineMode ||
           !sameChordSpec(spec, pristineSpec);
}

FLASHMEM bool SequencerStepContentDraftSession::begin(
    const SequencerPatternState& published,
    SequencerStepContentDraftKind nextKind,
    uint8_t nextOwnerStep,
    uint16_t nextOwnerNodeId
) {
    clearFailure();
    if (nextKind == SequencerStepContentDraftKind::NONE || active.get()) {
        return false;
    }

    ownerStep = nextOwnerStep;
    kind.set(nextKind);
    exitChoice.set(SequencerStepContentDraftExitChoice::SAVE);
    exitPromptVisible.set(false);

    if (nextKind == SequencerStepContentDraftKind::CHORD) {
        if (nextOwnerNodeId == SequencerStepChordDraftState::INVALID_NODE) {
            kind.set(SequencerStepContentDraftKind::NONE);
            noteFailure(SequencerStepContentDraftFailure::PUBLISH_FAILED);
            return false;
        }
        chord.reset();
        chord.ownerNodeId = nextOwnerNodeId;
        if (const auto* graph = graphView(published)) {
            if (const auto* node = graph->stepNode(nextOwnerNodeId)) {
                chord.modePresent = node->has(
                    oc::note::sequencer::STEP_NODE_CHORD_MODE
                );
                chord.localPresent = node->has(
                    oc::note::sequencer::STEP_NODE_CHORD_LOCAL
                );
                chord.mode = node->chordMode;
                chord.spec = node->chordSpec;
            }
        }
        chord.spec.clamp();
        chord.markPristine();
        active.set(true);
        touch();
        return true;
    }

    if (!scratch) {
        scratch = core::app::makeExtmemUnique<SequencerPatternState>();
        if (!scratch) {
            kind.set(SequencerStepContentDraftKind::NONE);
            noteFailure(SequencerStepContentDraftFailure::OUT_OF_MEMORY);
            return false;
        }
    }

    SequencerPatternSnapshot flat{};
    captureSnapshot(published, flat);
    const auto* publishedGraph = graphView(published);
    if (publishedGraph != nullptr) {
        if (!applySnapshotWithGraph(*scratch, flat, publishedGraph)) {
            kind.set(SequencerStepContentDraftKind::NONE);
            noteFailure(SequencerStepContentDraftFailure::OUT_OF_MEMORY);
            return false;
        }
    } else {
        // Keep the one bounded graph allocation warm between creation
        // sessions. Reset its payload, not its PSRAM ownership.
        applySnapshotPreservingGraph(*scratch, flat);
        if (scratch->graph) scratch->graph->reset();
        scratch->graphRevision.set(flat.graphRevision);
    }
    // Step-content drafts never author CC data. Do not retain a stale Lane
    // allocation left by any future scratch reuse path.
    scratch->ccLanes.reset();
    scratch->ccLaneRevision.set(0);

    pristineGraphRevision = scratch->graphRevision.get();
    pristineGraphFingerprint = graphFingerprint(*scratch);
    active.set(true);
    touch();
    return true;
}

FLASHMEM bool SequencerStepContentDraftSession::modified() const {
    if (!active.get()) return false;
    if (kind.get() == SequencerStepContentDraftKind::CHORD) {
        return chord.modified();
    }
    if (!scratch) return false;
    if (scratch->graphRevision.get() == pristineGraphRevision) return false;
    return graphFingerprint(*scratch) != pristineGraphFingerprint;
}

FLASHMEM SequencerPatternState* SequencerStepContentDraftSession::pattern() {
    return active.get() && kind.get() != SequencerStepContentDraftKind::CHORD
        ? scratch.get()
        : nullptr;
}

FLASHMEM const SequencerPatternState* SequencerStepContentDraftSession::pattern() const {
    return active.get() && kind.get() != SequencerStepContentDraftKind::CHORD
        ? scratch.get()
        : nullptr;
}

FLASHMEM void SequencerStepContentDraftSession::markPristine() {
    if (!active.get()) return;
    if (kind.get() == SequencerStepContentDraftKind::CHORD) {
        chord.markPristine();
        touch();
        return;
    }
    if (!scratch) return;
    pristineGraphRevision = scratch->graphRevision.get();
    pristineGraphFingerprint = graphFingerprint(*scratch);
    touch();
}

FLASHMEM void SequencerStepContentDraftSession::touch() {
    revision.set(revision.get() + 1U);
    if (revisionSignal_ != nullptr) {
        revisionSignal_->set(revisionSignal_->get() + 1U);
    }
}

FLASHMEM void SequencerStepContentDraftSession::showExitPrompt() {
    if (!active.get()) return;
    exitChoice.set(SequencerStepContentDraftExitChoice::SAVE);
    exitPromptVisible.set(true);
    touch();
}

FLASHMEM void SequencerStepContentDraftSession::hideExitPrompt() {
    if (!exitPromptVisible.get()) return;
    exitPromptVisible.set(false);
    touch();
}

FLASHMEM void SequencerStepContentDraftSession::clearFailure() {
    if (failure == SequencerStepContentDraftFailure::NONE &&
        blockedTransition == SequencerStepContentDraftBlockedTransition::NONE) {
        return;
    }
    failure = SequencerStepContentDraftFailure::NONE;
    blockedTransition = SequencerStepContentDraftBlockedTransition::NONE;
    touch();
}

FLASHMEM void SequencerStepContentDraftSession::noteFailure(
    SequencerStepContentDraftFailure nextFailure
) {
    failure = nextFailure;
    blockedTransition = SequencerStepContentDraftBlockedTransition::NONE;
    touch();
}

FLASHMEM void SequencerStepContentDraftSession::noteBlockedTransition(
    SequencerStepContentDraftBlockedTransition transition
) {
    if (failure == SequencerStepContentDraftFailure::TRANSITION_BLOCKED &&
        blockedTransition == transition) {
        return;
    }
    failure = SequencerStepContentDraftFailure::TRANSITION_BLOCKED;
    blockedTransition = transition;
    touch();
}

FLASHMEM bool SequencerStepContentDraftSession::rejectTransitionIfActive(
    SequencerStepContentDraftBlockedTransition transition
) {
    if (!active.get()) return false;
    noteBlockedTransition(transition);
    return true;
}

FLASHMEM void SequencerStepContentDraftSession::resetSession() {
    active.set(false);
    kind.set(SequencerStepContentDraftKind::NONE);
    exitPromptVisible.set(false);
    exitChoice.set(SequencerStepContentDraftExitChoice::SAVE);
    pristineGraphRevision = 0;
    pristineGraphFingerprint = 0;
    ownerStep = 0;
    chord.reset();
    failure = SequencerStepContentDraftFailure::NONE;
    blockedTransition = SequencerStepContentDraftBlockedTransition::NONE;
    touch();
}

}  // namespace core::state::sequencer

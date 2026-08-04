#include "state/sequencer/SequencerChordContextProjection.hpp"

#include <algorithm>
#include <array>

#include <config/PlatformCompat.hpp>
#include <oc/note/sequencer/StepSequencerChord.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerChordProjectionWorkspace.hpp"
#include "state/sequencer/SequencerScaleState.hpp"

namespace core::state::sequencer {
namespace {

using Graph = oc::note::sequencer::StepSequencerGraph;
using Limits = oc::note::sequencer::StepSequencerGraphLimits;
using Node = oc::note::sequencer::StepSequencerStepNode;
using ScaleSettings =
    oc::note::sequencer::StepSequencerScaleSettings;

struct ProjectionTraversal {
    SequencerPatternState& pattern;
    Graph& graph;
    ScaleSettings sourceScale;
    ScaleSettings targetScale;
    SequencerStepChordDraftState* chordDraft = nullptr;
    bool sourceUsesScaleDegrees = false;
    bool targetUsesScaleDegrees = false;
    SequencerChordContextProjectionStats stats{};
    std::array<uint8_t, Limits::MAX_STEP_NODES> visitState{};
    bool graphChanged = false;
    bool draftChanged = false;
    bool draftVisited = false;
};

FLASHMEM uint8_t offsetNote(
    uint8_t parent,
    const Node& node,
    ScaleSettings scale,
    bool usesScaleDegrees
) {
    if (!node.has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET) ||
        node.noteOffset == 0) {
        return parent;
    }
    scale.clamp();
    if (usesScaleDegrees) {
        return oc::note::sequencer::moveByScaleDegrees(
            parent,
            node.noteOffset,
            scale
        );
    }
    return static_cast<uint8_t>(std::clamp(
        static_cast<int>(parent) + static_cast<int>(node.noteOffset),
        0,
        127
    ));
}

FLASHMEM bool projectFormula(
    oc::note::sequencer::StepSequencerChordSpec& spec,
    ScaleSettings sourceScale,
    ScaleSettings targetScale,
    uint8_t sourceRoot,
    uint8_t targetRoot,
    bool sourceUsesScaleDegrees,
    bool targetUsesScaleDegrees,
    SequencerChordContextProjectionStats* stats
) {
    const auto projection = oc::note::sequencer::projectChordSpec(
        spec,
        sourceScale,
        targetScale,
        sourceRoot,
        targetRoot,
        sourceUsesScaleDegrees,
        targetUsesScaleDegrees,
        sharedSequencerChordProjectionWorkspace()
    );
    if (!projection.valid) {
        if (stats != nullptr) ++stats->failures;
        return false;
    }

    if (stats != nullptr) {
        ++stats->localChordsVisited;
        ++stats->projected;
        if (projection.exact) ++stats->exact;
        if (projection.adapted) ++stats->adapted;
        if (projection.directionLimited) {
            ++stats->directionLimited;
        }
        if (projection.rangeLimited) ++stats->rangeLimited;
        stats->droppedVoices = static_cast<uint16_t>(
            stats->droppedVoices + projection.droppedVoiceCount
        );
    }
    if (!projection.changed) return false;

    spec = projection.spec;
    if (stats != nullptr) ++stats->changed;
    return true;
}

FLASHMEM void noteProjection(
    ProjectionTraversal& traversal,
    uint16_t nodeId,
    Node& node,
    uint8_t sourceRoot,
    uint8_t targetRoot
) {
    const bool draftOwnsLocalFormula =
        traversal.chordDraft != nullptr &&
        traversal.chordDraft->ownerNodeId == nodeId &&
        traversal.chordDraft->localPresent;

    if (node.has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL)) {
        // An active draft represents this logical slot in feedback, while the
        // published formula is still projected so Discard remains coherent.
        traversal.graphChanged =
            projectFormula(
            node.chordSpec,
            traversal.sourceScale,
            traversal.targetScale,
            sourceRoot,
            targetRoot,
            traversal.sourceUsesScaleDegrees,
            traversal.targetUsesScaleDegrees,
            draftOwnsLocalFormula ? nullptr : &traversal.stats
        ) || traversal.graphChanged;
    }

    if (draftOwnsLocalFormula) {
        traversal.draftVisited = true;
        traversal.draftChanged =
            projectFormula(
            traversal.chordDraft->spec,
            traversal.sourceScale,
            traversal.targetScale,
            sourceRoot,
            targetRoot,
            traversal.sourceUsesScaleDegrees,
            traversal.targetUsesScaleDegrees,
            &traversal.stats
        ) || traversal.draftChanged;
    }
}

FLASHMEM SequencerChordContextProjectionStats projectDetachedChordDraft(
    SequencerState& sequencer,
    ScaleSettings sourceScale,
    ScaleSettings targetScale,
    SequencerPitchEditMode sourceMode,
    SequencerPitchEditMode targetMode
) {
    SequencerChordContextProjectionStats stats{};
    auto& session = sequencer.stepContentDraft;
    auto& draft = session.chord;
    if (!draft.localPresent ||
        session.ownerStep >= SequencerPatternState::MAX_STEPS) {
        return stats;
    }

    stats.patternsVisited = 1U;
    const uint8_t rootNote =
        sequencer.pattern.note[session.ownerStep];
    const bool changed = projectFormula(
        draft.spec,
        sourceScale,
        targetScale,
        rootNote,
        rootNote,
        pitchContextUsesScaleDegrees(sourceMode, sourceScale),
        pitchContextUsesScaleDegrees(targetMode, targetScale),
        &stats
    );
    if (changed) session.touch();
    return stats;
}

FLASHMEM void visitNode(
    ProjectionTraversal& traversal,
    uint16_t nodeId,
    uint8_t sourceParent,
    uint8_t targetParent,
    uint8_t depth
);

FLASHMEM void visitSequence(
    ProjectionTraversal& traversal,
    uint16_t sequenceId,
    uint8_t sourceParent,
    uint8_t targetParent,
    uint8_t depth
) {
    const auto* sequence = traversal.graph.sequence(sequenceId);
    if (sequence == nullptr || depth > Limits::MAX_DEPTH) return;
    for (uint8_t index = 0; index < sequence->length; ++index) {
        visitNode(
            traversal,
            static_cast<uint16_t>(sequence->firstStepNode + index),
            sourceParent,
            targetParent,
            depth
        );
    }
}

FLASHMEM void visitCycleSet(
    ProjectionTraversal& traversal,
    uint16_t cycleSetId,
    uint8_t sourceParent,
    uint8_t targetParent,
    uint8_t depth
) {
    const auto* cycleSet = traversal.graph.cycleSet(cycleSetId);
    if (cycleSet == nullptr || depth > Limits::MAX_DEPTH) return;
    for (uint8_t index = 0; index < cycleSet->length; ++index) {
        visitNode(
            traversal,
            static_cast<uint16_t>(cycleSet->firstStateNode + index),
            sourceParent,
            targetParent,
            depth
        );
    }
}

FLASHMEM void visitNode(
    ProjectionTraversal& traversal,
    uint16_t nodeId,
    uint8_t sourceParent,
    uint8_t targetParent,
    uint8_t depth
) {
    if (nodeId >= traversal.graph.stepNodeCount ||
        nodeId >= traversal.graph.stepNodes.size() ||
        depth > Limits::MAX_DEPTH) {
        return;
    }
    auto& visit = traversal.visitState[nodeId];
    if (visit != 0U) return;
    visit = 1U;

    auto& node = traversal.graph.stepNodes[nodeId];
    const uint8_t sourceRoot = offsetNote(
        sourceParent,
        node,
        traversal.sourceScale,
        traversal.sourceUsesScaleDegrees
    );
    const uint8_t targetRoot = offsetNote(
        targetParent,
        node,
        traversal.targetScale,
        traversal.targetUsesScaleDegrees
    );
    noteProjection(
        traversal,
        nodeId,
        node,
        sourceRoot,
        targetRoot
    );

    const uint8_t childDepth = static_cast<uint8_t>(depth + 1U);
    if (node.has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE)) {
        visitSequence(
            traversal,
            node.childSequenceId,
            sourceRoot,
            targetRoot,
            childDepth
        );
    }
    if (node.has(oc::note::sequencer::STEP_NODE_CYCLE_SET)) {
        visitCycleSet(
            traversal,
            node.cycleSetId,
            sourceRoot,
            targetRoot,
            childDepth
        );
    }
    visit = 2U;
}

FLASHMEM void visitRootSequence(ProjectionTraversal& traversal) {
    const auto* root = traversal.graph.sequence(
        traversal.graph.rootSequenceId
    );
    if (root == nullptr) return;

    const uint8_t count = std::min<uint8_t>(
        root->length,
        SequencerPatternState::MAX_STEPS
    );
    for (uint8_t step = 0; step < count; ++step) {
        const uint8_t rootNote = traversal.pattern.note[step];
        visitNode(
            traversal,
            static_cast<uint16_t>(root->firstStepNode + step),
            rootNote,
            rootNote,
            0U
        );
    }
}

FLASHMEM void visitOrphans(ProjectionTraversal& traversal) {
    const uint8_t patternLength = std::max<uint8_t>(
        traversal.pattern.length.get(),
        1U
    );
    for (uint16_t nodeId = 0;
         nodeId < traversal.graph.stepNodeCount &&
         nodeId < traversal.graph.stepNodes.size();
         ++nodeId) {
        if (traversal.visitState[nodeId] != 0U) continue;
        const uint8_t rootIndex = static_cast<uint8_t>(
            nodeId % patternLength
        );
        const uint8_t rootNote = traversal.pattern.note[rootIndex];
        visitNode(
            traversal,
            nodeId,
            rootNote,
            rootNote,
            0U
        );
    }
}

}  // namespace

FLASHMEM void SequencerChordContextProjectionStats::merge(
    const SequencerChordContextProjectionStats& other
) {
    patternsVisited = static_cast<uint16_t>(
        patternsVisited + other.patternsVisited
    );
    localChordsVisited = static_cast<uint16_t>(
        localChordsVisited + other.localChordsVisited
    );
    projected = static_cast<uint16_t>(projected + other.projected);
    changed = static_cast<uint16_t>(changed + other.changed);
    exact = static_cast<uint16_t>(exact + other.exact);
    adapted = static_cast<uint16_t>(adapted + other.adapted);
    directionLimited = static_cast<uint16_t>(
        directionLimited + other.directionLimited
    );
    rangeLimited = static_cast<uint16_t>(
        rangeLimited + other.rangeLimited
    );
    failures = static_cast<uint16_t>(
        failures + other.failures
    );
    droppedVoices = static_cast<uint16_t>(
        droppedVoices + other.droppedVoices
    );
}

FLASHMEM SequencerChordContextProjectionStats projectPatternChordContext(
    SequencerPatternState& pattern,
    ScaleSettings sourceScale,
    ScaleSettings targetScale
) {
    return projectPatternChordContext(
        pattern,
        sourceScale,
        targetScale,
        pattern.pitchEditMode,
        pattern.pitchEditMode
    );
}

FLASHMEM SequencerChordContextProjectionStats projectPatternChordContext(
    SequencerPatternState& pattern,
    ScaleSettings sourceScale,
    ScaleSettings targetScale,
    SequencerPitchEditMode sourceMode,
    SequencerPitchEditMode targetMode
) {
    SequencerChordContextProjectionStats empty{};
    sourceScale.clamp();
    targetScale.clamp();
    auto* graph = pattern.graph.get();
    if (graph == nullptr || !graph->enabled) return empty;

    ProjectionTraversal traversal{
        .pattern = pattern,
        .graph = *graph,
        .sourceScale = sourceScale,
        .targetScale = targetScale,
        .sourceUsesScaleDegrees =
            pitchContextUsesScaleDegrees(sourceMode, sourceScale),
        .targetUsesScaleDegrees =
            pitchContextUsesScaleDegrees(targetMode, targetScale),
    };
    traversal.stats.patternsVisited = 1U;
    visitRootSequence(traversal);
    visitOrphans(traversal);
    if (traversal.graphChanged) pattern.bumpGraphRevision();
    return traversal.stats;
}

FLASHMEM SequencerChordContextProjectionStats projectPatternChordContext(
    SequencerState& sequencer,
    ScaleSettings sourceScale,
    ScaleSettings targetScale,
    SequencerPitchEditMode sourceMode,
    SequencerPitchEditMode targetMode
) {
    sourceScale.clamp();
    targetScale.clamp();

    // A Micro/Cycle draft owns a complete graph scratch. Keep the published
    // Pattern coherent for immediate playback and project the authored graph
    // independently; only the authored stats are user-facing.
    if (auto* authored = sequencer.stepContentDraft.pattern()) {
        const auto published = projectPatternChordContext(
            sequencer.pattern,
            sourceScale,
            targetScale,
            sourceMode,
            targetMode
        );
        const auto draft = projectPatternChordContext(
            *authored,
            sourceScale,
            targetScale,
            sourceMode,
            targetMode
        );
        if (draft.hasChanges()) sequencer.stepContentDraft.touch();
        return draft.patternsVisited != 0U ? draft : published;
    }

    if (!sequencer.stepContentDraft.active.get() ||
        sequencer.stepContentDraft.kind.get() !=
            SequencerStepContentDraftKind::CHORD) {
        return projectPatternChordContext(
            sequencer.pattern,
            sourceScale,
            targetScale,
            sourceMode,
            targetMode
        );
    }

    auto* graph = sequencer.pattern.graph.get();
    if (graph == nullptr || !graph->enabled) {
        return projectDetachedChordDraft(
            sequencer,
            sourceScale,
            targetScale,
            sourceMode,
            targetMode
        );
    }

    ProjectionTraversal traversal{
        .pattern = sequencer.pattern,
        .graph = *graph,
        .sourceScale = sourceScale,
        .targetScale = targetScale,
        .chordDraft = &sequencer.stepContentDraft.chord,
        .sourceUsesScaleDegrees =
            pitchContextUsesScaleDegrees(sourceMode, sourceScale),
        .targetUsesScaleDegrees =
            pitchContextUsesScaleDegrees(targetMode, targetScale),
    };
    traversal.stats.patternsVisited = 1U;
    visitRootSequence(traversal);
    visitOrphans(traversal);
    if (!traversal.draftVisited &&
        sequencer.stepContentDraft.chord.localPresent) {
        auto detached = projectDetachedChordDraft(
            sequencer,
            sourceScale,
            targetScale,
            sourceMode,
            targetMode
        );
        detached.patternsVisited = 0U;
        traversal.stats.merge(detached);
    }
    if (traversal.graphChanged) sequencer.pattern.bumpGraphRevision();
    if (traversal.draftChanged) sequencer.stepContentDraft.touch();
    return traversal.stats;
}

FLASHMEM SequencerChordContextProjectionStats projectInheritedChordContexts(
    SequencerTrackBankState& bank,
    SequencerState& active,
    ScaleSettings sourceScale,
    ScaleSettings targetScale
) {
    SequencerChordContextProjectionStats total{};
    const uint8_t activeTrack = bank.activeTrackIndex();
    if (!isPatternScaleOverride(active.pattern.scalePolicy)) {
        total.merge(projectPatternChordContext(
            active,
            sourceScale,
            targetScale,
            active.pattern.pitchEditMode,
            active.pattern.pitchEditMode
        ));
    }

    for (uint8_t track = 0;
         track < SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        if (track == activeTrack) continue;
        auto& pattern = bank.track(track);
        if (isPatternScaleOverride(pattern.scalePolicy)) continue;
        total.merge(projectPatternChordContext(
            pattern,
            sourceScale,
            targetScale
        ));
    }
    return total;
}

}  // namespace core::state::sequencer

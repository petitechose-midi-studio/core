#include "state/sequencer/SequencerStepContentDraftOps.hpp"

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {
namespace {

using Graph = oc::note::sequencer::StepSequencerGraph;
using Node = oc::note::sequencer::StepSequencerStepNode;
using Sequence = oc::note::sequencer::StepSequencerSequence;
using SequenceKind = oc::note::sequencer::StepSequencerSequenceKind;

FLASHMEM bool sameChordSpec(
    const oc::note::sequencer::StepSequencerChordSpec& lhs,
    const oc::note::sequencer::StepSequencerChordSpec& rhs
) {
    return lhs.voiceCount == rhs.voiceCount &&
           lhs.harmonyData == rhs.harmonyData &&
           lhs.voicingData == rhs.voicingData &&
           lhs.inversionData == rhs.inversionData &&
           lhs.strum == rhs.strum &&
           lhs.velocityCurve == rhs.velocityCurve;
}

FLASHMEM oc::note::sequencer::StepSequencerChordMode sanitizeChordMode(
    oc::note::sequencer::StepSequencerChordMode mode
) {
    if (static_cast<uint8_t>(mode) >
        static_cast<uint8_t>(
            oc::note::sequencer::StepSequencerChordMode::Local
        )) {
        return oc::note::sequencer::StepSequencerChordMode::Single;
    }
    return mode;
}

FLASHMEM bool sameScaleSettings(
    oc::note::sequencer::StepSequencerScaleSettings lhs,
    oc::note::sequencer::StepSequencerScaleSettings rhs
) {
    lhs.clamp();
    rhs.clamp();
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

FLASHMEM bool sameVariationRanges(
    oc::note::sequencer::StepSequencerVariationRanges lhs,
    oc::note::sequencer::StepSequencerVariationRanges rhs
) {
    lhs.clamp();
    rhs.clamp();
    return lhs.pitchSemitones == rhs.pitchSemitones &&
           lhs.velocity == rhs.velocity &&
           lhs.gatePercent == rhs.gatePercent &&
           lhs.nudge == rhs.nudge;
}

FLASHMEM bool samePublishableFlatPattern(
    const SequencerPatternSnapshot& lhs,
    const SequencerPatternSnapshot& rhs
) {
    return lhs.length == rhs.length &&
           lhs.playStart == rhs.playStart &&
           lhs.loopStart == rhs.loopStart &&
           lhs.loopEnd == rhs.loopEnd &&
           lhs.stepsPerBeat == rhs.stepsPerBeat &&
           lhs.enabledMask == rhs.enabledMask &&
           lhs.swingOffsetPercent == rhs.swingOffsetPercent &&
           lhs.patternNudgePercent == rhs.patternNudgePercent &&
           sameVariationRanges(lhs.variationRanges, rhs.variationRanges) &&
           lhs.scalePolicy == rhs.scalePolicy &&
           sameScaleSettings(lhs.scaleOverride, rhs.scaleOverride) &&
           lhs.pitchEditMode == rhs.pitchEditMode &&
           lhs.note == rhs.note &&
           lhs.velocity == rhs.velocity &&
           lhs.gate == rhs.gate &&
           lhs.nudge == rhs.nudge &&
           lhs.probability == rhs.probability;
}

FLASHMEM void assignFlag(uint16_t& flags, uint16_t flag, bool enabled) {
    flags = enabled
        ? static_cast<uint16_t>(flags | flag)
        : static_cast<uint16_t>(flags & ~flag);
}

FLASHMEM void initializeRootGraph(
    Graph& graph,
    SequencerPitchEditMode pitchMode
) {
    graph.reset();
    graph.enabled = true;
    graph.rootSequenceId = 0;
    graph.sequenceCount = 1;
    graph.stepNodeCount = SequencerPatternState::MAX_STEPS;
    graph.sequences[0] = Sequence{
        .kind = SequenceKind::RootPattern,
        .firstStepNode = 0,
        .length = SequencerPatternState::MAX_STEPS,
        .offset = 0,
    };
    const bool chromatic = pitchMode == SequencerPitchEditMode::CHROMATIC;
    for (uint16_t i = 0; i < SequencerPatternState::MAX_STEPS; ++i) {
        assignFlag(
            graph.stepNodes[i].flags,
            oc::note::sequencer::STEP_NODE_PITCH_CHROMATIC,
            chromatic
        );
    }
}

FLASHMEM bool applyChordDraft(
    Graph& graph,
    const SequencerStepChordDraftState& chord,
    SequencerPitchEditMode pitchMode
) {
    if (chord.ownerNodeId >= graph.stepNodeCount ||
        chord.ownerNodeId >= graph.stepNodes.size()) {
        return false;
    }

    auto& node = graph.stepNodes[chord.ownerNodeId];
    assignFlag(
        node.flags,
        oc::note::sequencer::STEP_NODE_PITCH_CHROMATIC,
        pitchMode == SequencerPitchEditMode::CHROMATIC
    );
    node.chordMode = chord.modePresent
        ? chord.mode
        : oc::note::sequencer::StepSequencerChordMode::Single;
    node.chordSpec = chord.localPresent
        ? chord.spec
        : oc::note::sequencer::StepSequencerChordSpec{};
    node.chordSpec.clamp();
    assignFlag(
        node.flags,
        oc::note::sequencer::STEP_NODE_CHORD_MODE,
        chord.modePresent
    );
    assignFlag(
        node.flags,
        oc::note::sequencer::STEP_NODE_CHORD_LOCAL,
        chord.localPresent
    );
    return true;
}

FLASHMEM bool ownsChordDraftNode(
    const SequencerState& sequencer,
    uint16_t nodeId
) {
    return sequencer.stepContentDraft.active.get() &&
           sequencer.stepContentDraft.kind.get() ==
               SequencerStepContentDraftKind::CHORD &&
           sequencer.stepContentDraft.chord.ownerNodeId == nodeId;
}

FLASHMEM uint32_t publishedRevisionFor(const SequencerState& sequencer) {
    return sequencer.pattern.graphRevision.get() + 1U;
}

}  // namespace

FLASHMEM SequencerPatternState& authoringPattern(SequencerState& sequencer) {
    auto* draft = sequencer.stepContentDraft.pattern();
    return draft != nullptr ? *draft : sequencer.pattern;
}

FLASHMEM const SequencerPatternState& authoringPattern(
    const SequencerState& sequencer
) {
    const auto* draft = sequencer.stepContentDraft.pattern();
    return draft != nullptr ? *draft : sequencer.pattern;
}

FLASHMEM bool beginStepContentDraft(
    SequencerState& sequencer,
    SequencerStepContentDraftKind kind,
    uint8_t ownerStep,
    uint16_t ownerNodeId
) {
    return sequencer.stepContentDraft.begin(
        sequencer.pattern,
        kind,
        ownerStep,
        ownerNodeId
    );
}

FLASHMEM bool resolveStepContentDraftChord(
    const SequencerState& sequencer,
    uint16_t nodeId,
    bool& modePresent,
    bool& localPresent,
    oc::note::sequencer::StepSequencerChordMode& mode,
    oc::note::sequencer::StepSequencerChordSpec& spec
) {
    if (!ownsChordDraftNode(sequencer, nodeId)) return false;
    const auto& chord = sequencer.stepContentDraft.chord;
    modePresent = chord.modePresent;
    localPresent = chord.localPresent;
    mode = chord.mode;
    spec = chord.spec;
    return true;
}

FLASHMEM bool setAuthoringNodeChordMode(
    SequencerState& sequencer,
    uint16_t nodeId,
    oc::note::sequencer::StepSequencerChordMode mode
) {
    mode = sanitizeChordMode(mode);
    if (!ownsChordDraftNode(sequencer, nodeId)) {
        return setNodeChordMode(authoringPattern(sequencer), nodeId, mode);
    }
    auto& chord = sequencer.stepContentDraft.chord;
    const bool changed = !chord.modePresent || chord.mode != mode;
    chord.modePresent = true;
    chord.mode = mode;
    return changed;
}

FLASHMEM bool setAuthoringNodeChordSpec(
    SequencerState& sequencer,
    uint16_t nodeId,
    oc::note::sequencer::StepSequencerChordSpec spec
) {
    spec.clamp();
    if (!ownsChordDraftNode(sequencer, nodeId)) {
        return setNodeChordSpec(authoringPattern(sequencer), nodeId, spec);
    }
    auto& chord = sequencer.stepContentDraft.chord;
    const bool changed = !chord.modePresent || !chord.localPresent ||
                         chord.mode !=
                             oc::note::sequencer::StepSequencerChordMode::Local ||
                         !sameChordSpec(chord.spec, spec);
    chord.modePresent = true;
    chord.localPresent = true;
    chord.mode = oc::note::sequencer::StepSequencerChordMode::Local;
    chord.spec = spec;
    return changed;
}

FLASHMEM bool clearAuthoringNodeChordState(
    SequencerState& sequencer,
    uint16_t nodeId
) {
    if (!ownsChordDraftNode(sequencer, nodeId)) {
        return clearNodeChordState(authoringPattern(sequencer), nodeId);
    }
    auto& chord = sequencer.stepContentDraft.chord;
    const auto defaults = oc::note::sequencer::StepSequencerChordSpec{};
    const bool changed = chord.modePresent || chord.localPresent ||
                         chord.mode !=
                             oc::note::sequencer::StepSequencerChordMode::Single ||
                         !sameChordSpec(chord.spec, defaults);
    chord.modePresent = false;
    chord.localPresent = false;
    chord.mode = oc::note::sequencer::StepSequencerChordMode::Single;
    chord.spec = defaults;
    return changed;
}

FLASHMEM void markStepContentDraftPristine(SequencerState& sequencer) {
    sequencer.stepContentDraft.markPristine();
}

FLASHMEM void notifyStepContentDraftMutation(SequencerState& sequencer) {
    if (sequencer.stepContentDraft.active.get()) {
        sequencer.stepContentDraft.clearFailure();
        sequencer.stepContentDraft.touch();
    }
}

FLASHMEM bool stepContentDraftHasPublishableSubset(
    const SequencerState& sequencer
) {
    if (!sequencer.stepContentDraft.active.get()) return false;
    if (sequencer.stepContentDraft.kind.get() ==
        SequencerStepContentDraftKind::CHORD) {
        return true;
    }
    const auto* draft = sequencer.stepContentDraft.pattern();
    if (draft == nullptr) return false;

    SequencerPatternSnapshot published{};
    SequencerPatternSnapshot authored{};
    captureSnapshot(sequencer.pattern, published);
    captureSnapshot(*draft, authored);
    return samePublishableFlatPattern(published, authored);
}

FLASHMEM bool captureStepContentDraftAfterSnapshot(
    const SequencerState& sequencer,
    SequencerHistoryPatternSnapshot& out
) {
    if (!sequencer.stepContentDraft.active.get()) return false;

    captureSnapshot(sequencer.pattern, out.flat);
    out.flat.graphRevision = publishedRevisionFor(sequencer);
    out.focusedStep = sequencer.focusedStep.get();
    if (!reserveHistorySnapshotGraphStorage(out)) return false;

    if (sequencer.stepContentDraft.kind.get() ==
        SequencerStepContentDraftKind::CHORD) {
        const auto* published = graphView(sequencer.pattern);
        if (published != nullptr) {
            *out.graph = *published;
        } else {
            initializeRootGraph(*out.graph, sequencer.pattern.pitchEditMode);
        }
        return applyChordDraft(
            *out.graph,
            sequencer.stepContentDraft.chord,
            sequencer.pattern.pitchEditMode
        );
    }

    const auto* draft = sequencer.stepContentDraft.pattern();
    if (draft == nullptr) return false;
    const auto* graph = graphView(*draft);
    if (graph != nullptr) {
        *out.graph = *graph;
    } else {
        out.graph.reset();
    }
    return true;
}

FLASHMEM bool captureStepContentDraftRuntimeGraph(
    const SequencerState& sequencer,
    Graph& out
) {
    if (!sequencer.stepContentDraft.active.get()) return false;

    if (sequencer.stepContentDraft.kind.get() ==
        SequencerStepContentDraftKind::CHORD) {
        if (const auto* published = graphView(sequencer.pattern)) {
            out = *published;
        } else {
            initializeRootGraph(out, sequencer.pattern.pitchEditMode);
        }
        return applyChordDraft(
            out,
            sequencer.stepContentDraft.chord,
            sequencer.pattern.pitchEditMode
        );
    }

    const auto* draft = sequencer.stepContentDraft.pattern();
    if (draft == nullptr) return false;
    const auto* graph = graphView(*draft);
    if (graph == nullptr) {
        out.reset();
    } else {
        out = *graph;
    }
    return true;
}

FLASHMEM bool publishStepContentDraft(SequencerState& sequencer) {
    if (sequencer.stepContentDraft.active.get() &&
        sequencer.stepContentDraft.kind.get() ==
            SequencerStepContentDraftKind::CHORD) {
        const uint32_t revision = publishedRevisionFor(sequencer);
        core::app::ExtmemUniquePtr<Graph> prepared;
        Graph* destination = sequencer.pattern.graph.get();
        if (destination == nullptr) {
            prepared = core::app::makeExtmemUnique<Graph>();
            if (!prepared) {
                sequencer.stepContentDraft.noteFailure(
                    SequencerStepContentDraftFailure::OUT_OF_MEMORY
                );
                return false;
            }
            initializeRootGraph(*prepared, sequencer.pattern.pitchEditMode);
            destination = prepared.get();
        }
        if (!applyChordDraft(
                *destination,
                sequencer.stepContentDraft.chord,
                sequencer.pattern.pitchEditMode
            )) {
            sequencer.stepContentDraft.noteFailure(
                SequencerStepContentDraftFailure::PUBLISH_FAILED
            );
            return false;
        }
        if (prepared) sequencer.pattern.graph = std::move(prepared);
        sequencer.pattern.graphRevision.set(revision);
        sequencer.invalidateVariationTelemetry();
        sequencer.stepContentDraft.resetSession();
        return true;
    }

    const auto* draft = sequencer.stepContentDraft.pattern();
    if (draft == nullptr) {
        sequencer.stepContentDraft.noteFailure(
            SequencerStepContentDraftFailure::PUBLISH_FAILED
        );
        return false;
    }
    const auto* source = graphView(*draft);
    const uint32_t revision = publishedRevisionFor(sequencer);

    core::app::ExtmemUniquePtr<Graph> prepared;
    if (source != nullptr && !sequencer.pattern.graph) {
        prepared = core::app::makeExtmemUnique<Graph>(*source);
        if (!prepared) {
            sequencer.stepContentDraft.noteFailure(
                SequencerStepContentDraftFailure::OUT_OF_MEMORY
            );
            return false;
        }
    }

    if (source == nullptr) {
        sequencer.pattern.graph.reset();
    } else if (sequencer.pattern.graph) {
        *sequencer.pattern.graph = *source;
    } else {
        sequencer.pattern.graph = std::move(prepared);
    }
    sequencer.pattern.graphRevision.set(revision);
    sequencer.invalidateVariationTelemetry();
    sequencer.stepContentDraft.resetSession();
    return true;
}

FLASHMEM void abandonStepContentDraft(SequencerState& sequencer) {
    sequencer.stepContentDraft.resetSession();
}

}  // namespace core::state::sequencer

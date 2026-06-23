#include "state/sequencer/SequencerChordUiOps.hpp"

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

FLASHMEM SequencerStepChordUiState resolveStepChordUiState(
    const SequencerState& sequencer,
    uint8_t step
) {
    SequencerStepChordUiState state{};
    state.rootContext = isRootContentView(sequencer);
    state.mode = defaultChordModeForContentContext(state.rootContext);

    if (step >= activeContentLength(sequencer)) return state;
    state.valid = true;

    const auto* graph = graphView(sequencer.pattern);
    if (graph == nullptr) return state;

    const auto nodeId = activeContentStepNodeId(sequencer, step);
    const auto* node = graph->stepNode(nodeId);
    if (node == nullptr) return state;

    if (node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE)) {
        state.mode = node->chordMode;
    }
    if (node->has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL)) {
        state.spec = node->chordSpec;
    }
    state.spec.clamp();
    state.effectiveVoiceCount =
        state.mode == oc::note::sequencer::StepSequencerChordMode::Local
            ? state.spec.voiceCount
            : 1;
    return state;
}

FLASHMEM void resolveStepChordPreview(
    SequencerStepChordUiState& chord,
    const SequencerContentStepProjection& projection,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (!chord.valid || !projection.valid) return;

    const oc::note::sequencer::StepSequencerStepValues root{
        .note = projection.note,
        .velocity = projection.velocity,
        .gate = projection.gate,
        .nudge = projection.nudge,
    };
    oc::note::sequencer::StepSequencerChordState chordState{};
    chordState.mode = chord.mode;
    chordState.local = chord.spec;

    const uint16_t spanTicks = projection.gate == 0 ? 1U : projection.gate;
    const auto resolution = oc::note::sequencer::resolveStepChord(
        root,
        scaleSettings,
        chordState,
        projection.inheritedChord,
        spanTicks
    );
    if (resolution.count == 0) return;

    chord.preview.valid = true;
    chord.preview.source = resolution.source;
    chord.preview.voiceCount = resolution.count;
    chord.preview.spanTicks = spanTicks;
    chord.effectiveVoiceCount = resolution.count;
    if (resolution.activeForChildren.valid) {
        chord.spec = resolution.activeForChildren.spec;
        chord.spec.clamp();
    }
    for (uint8_t i = 0; i < resolution.count && i < chord.preview.voices.size(); ++i) {
        chord.preview.voices[i] = SequencerChordVoicePreview{
            .note = resolution.voices[i].note,
            .velocity = resolution.voices[i].velocity,
            .gate = resolution.voices[i].gate,
            .delayTicks = resolution.voices[i].delayTicks,
            .interval = resolution.voices[i].interval,
            .intervalUsesScaleDegrees = resolution.voices[i].intervalUsesScaleDegrees,
        };
    }
    chord.preview.analysis = oc::note::sequencer::analyzeResolvedChord(resolution, root);
}

}  // namespace core::state::sequencer

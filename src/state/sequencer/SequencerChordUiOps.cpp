#include "state/sequencer/SequencerChordUiOps.hpp"

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::state::sequencer {

FLASHMEM SequencerStepChordUiState resolveStepChordUiState(
    const SequencerState& sequencer,
    uint8_t step
) {
    SequencerStepChordUiState state{};
    state.rootContext = isRootContentView(sequencer);
    state.pitchFollowsScale =
        authoringPattern(sequencer).pitchEditMode ==
            SequencerPitchEditMode::FOLLOW_SCALE;
    state.mode = defaultChordModeForContentContext(state.rootContext);

    if (step >= activeContentLength(sequencer)) return state;
    state.valid = true;

    const auto nodeId = activeContentStepNodeId(sequencer, step);
    const auto* graph = graphView(authoringPattern(sequencer));
    const auto* node = graph ? graph->stepNode(nodeId) : nullptr;
    bool modePresent = false;
    bool localPresent = false;
    if (node != nullptr) {
        modePresent = node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE);
        localPresent = node->has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL);
        if (modePresent) state.mode = node->chordMode;
        if (localPresent) state.spec = node->chordSpec;
    }

    oc::note::sequencer::StepSequencerChordMode draftMode = state.mode;
    auto draftSpec = state.spec;
    if (resolveStepContentDraftChord(
            sequencer,
            nodeId,
            modePresent,
            localPresent,
            draftMode,
            draftSpec
        )) {
        state.mode = modePresent
            ? draftMode
            : defaultChordModeForContentContext(state.rootContext);
        state.spec = localPresent
            ? draftSpec
            : oc::note::sequencer::StepSequencerChordSpec{};
    }
    state.spec.clamp();
    state.effectiveVoiceCount =
        state.mode == oc::note::sequencer::StepSequencerChordMode::Local
            ? state.spec.voices()
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
        spanTicks,
        chord.pitchFollowsScale
    );
    if (resolution.count == 0) return;

    chord.preview.valid = true;
    chord.preview.source = resolution.source;
    chord.preview.harmonyAdjustedForPitchMode =
        resolution.harmonyAdjustedForPitchMode;
    chord.preview.intervalBasisAdjusted = resolution.intervalBasisAdjusted;
    chord.preview.inversionClamped = resolution.inversionClamped;
    chord.preview.rangeLimited = resolution.rangeLimited;
    chord.preview.rootNote = root.note;
    chord.preview.voiceCount = resolution.count;
    chord.preview.requestedVoiceCount = resolution.requestedVoiceCount;
    chord.preview.effectiveInversion = resolution.effectiveInversion;
    chord.preview.droppedVoiceCount = resolution.droppedVoiceCount;
    chord.preview.requestedIntervalBasis = resolution.requestedIntervalBasis;
    chord.preview.intervalBasis = resolution.intervalBasis;
    chord.preview.harmony = resolution.harmony;
    chord.preview.voicing = resolution.voicing;
    chord.preview.spanTicks = spanTicks;
    chord.preview.scaleSettings = scaleSettings;
    chord.scaleAvailable =
        scaleSettings.type != oc::note::sequencer::StepSequencerScaleType::Chromatic;
    chord.intervalsUseScaleDegrees = resolution.intervalUsesScaleDegrees;
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
            .inSelectedScale = resolution.voices[i].inSelectedScale,
        };
    }
    chord.preview.analysis = oc::note::sequencer::analyzeResolvedChord(resolution, root);
}

}  // namespace core::state::sequencer

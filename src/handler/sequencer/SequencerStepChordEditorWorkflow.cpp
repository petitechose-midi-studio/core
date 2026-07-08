#include "handler/sequencer/SequencerStepChordEditorWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/note/sequencer/StepSequencerChord.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerChordEditOps.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::handler::sequencer::step_chord_editor_workflow {
namespace chord_edit_ops = core::handler::sequencer::chord_edit_ops;
namespace input_utils = core::handler::sequencer::input_utils;

namespace {

FLASHMEM core::state::sequencer::SequencerStepChordUiState resolvedChordState(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    auto chord = core::state::sequencer::resolveStepChordUiState(sequencer, step);
    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        step,
        scaleSettings
    );
    if (projection.valid) {
        core::state::sequencer::resolveStepChordPreview(chord, projection, scaleSettings);
    }
    return chord;
}

}  // namespace

FLASHMEM bool active(const core::state::sequencer::SequencerState& sequencer) {
    return sequencer.stepEdit.chordEditor.active.get();
}

FLASHMEM void open(core::state::sequencer::SequencerState& sequencer) {
    auto& edit = sequencer.stepEdit;
    edit.contextHold.clear();
    edit.localVariationEditActive.set(false);
    edit.chordEditor.active.set(true);
    edit.chordEditor.focusedField.set(core::state::sequencer::SequencerChordEditField::MODE);
}

FLASHMEM void close(core::state::sequencer::SequencerState& sequencer) {
    sequencer.stepEdit.chordEditor.reset();
}

FLASHMEM void moveFocus(core::state::sequencer::SequencerState& sequencer, float delta) {
    auto& chordEditor = sequencer.stepEdit.chordEditor;
    const int current = static_cast<int>(chordEditor.focusedField.get());
    const int next = nav::nextWrappedIndex(delta, current, chord_edit_ops::editFieldCount());
    chordEditor.focusedField.set(
        static_cast<core::state::sequencer::SequencerChordEditField>(next)
    );
}

FLASHMEM void setFocusedFieldValue(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    float normalized
) {
    using Field = core::state::sequencer::SequencerChordEditField;

    auto chord = resolvedChordState(sequencer, step, scaleSettings);
    const auto field = sequencer.stepEdit.chordEditor.focusedField.get();
    if (field == Field::MODE) {
        const int choice = input_utils::normalizedToIndex(
            normalized,
            chord_edit_ops::modeChoiceCount(chord.rootContext)
        );
        chord_edit_ops::applyModeChoice(sequencer, step, choice, chord.spec);
        return;
    }

    chord_edit_ops::applySpecField(sequencer, step, field, chord.spec, normalized);
}

FLASHMEM void configureFocusedFieldEncoder(
    oc::api::EncoderAPI& encoders,
    oc::type::EncoderID encoderId,
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    using Field = core::state::sequencer::SequencerChordEditField;
    using Spec = oc::note::sequencer::StepSequencerChordSpec;

    auto chord = resolvedChordState(sequencer, step, scaleSettings);
    const auto field = sequencer.stepEdit.chordEditor.focusedField.get();

    encoders.setDiscreteTicksPerStep(encoderId, 4);
    encoders.setNormalizedTurns(encoderId, 0.5f);

    switch (field) {
        case Field::MODE:
            encoders.setDiscreteSteps(
                encoderId,
                static_cast<uint8_t>(chord_edit_ops::modeChoiceCount(chord.rootContext))
            );
            encoders.setPosition(
                encoderId,
                input_utils::indexToNormalized(
                    chord_edit_ops::modeChoiceIndex(chord.rootContext, chord.mode),
                    chord_edit_ops::modeChoiceCount(chord.rootContext)
                )
            );
            return;
        case Field::VOICES:
            encoders.setDiscreteSteps(encoderId, Spec::MAX_VOICES - 1U);
            encoders.setPosition(
                encoderId,
                chord_edit_ops::voiceCountToNormalized(chord.spec.voiceCount)
            );
            return;
        case Field::COLOR:
            encoders.setDiscreteSteps(encoderId, Spec::MAX_COLOR + 1U);
            encoders.setPosition(
                encoderId,
                input_utils::indexToNormalized(chord.spec.color, Spec::MAX_COLOR + 1)
            );
            return;
        case Field::VARIANT:
            encoders.setDiscreteSteps(encoderId, Spec::MAX_VARIANT + 1U);
            encoders.setPosition(
                encoderId,
                input_utils::indexToNormalized(chord.spec.variant, Spec::MAX_VARIANT + 1)
            );
            return;
        case Field::SPREAD:
            encoders.setDiscreteSteps(encoderId, Spec::MAX_SPREAD + 1U);
            encoders.setPosition(
                encoderId,
                input_utils::indexToNormalized(chord.spec.spread, Spec::MAX_SPREAD + 1)
            );
            return;
        case Field::STRUM:
            encoders.setDiscreteSteps(
                encoderId,
                static_cast<uint8_t>((Spec::MAX_STRUM - Spec::MIN_STRUM) + 1)
            );
            encoders.setNormalizedTurns(encoderId, 2.0f);
            encoders.setPosition(
                encoderId,
                chord_edit_ops::signedToNormalized(
                    chord.spec.strum,
                    Spec::MIN_STRUM,
                    Spec::MAX_STRUM
                )
            );
            return;
        case Field::VELOCITY_CURVE:
            encoders.setDiscreteSteps(
                encoderId,
                static_cast<uint8_t>(
                    (Spec::MAX_VELOCITY_CURVE - Spec::MIN_VELOCITY_CURVE) + 1
                )
            );
            encoders.setNormalizedTurns(encoderId, 2.0f);
            encoders.setPosition(
                encoderId,
                chord_edit_ops::signedToNormalized(
                    chord.spec.velocityCurve,
                    Spec::MIN_VELOCITY_CURVE,
                    Spec::MAX_VELOCITY_CURVE
                )
            );
            return;
        case Field::COUNT:
        default:
            return;
    }
}

FLASHMEM bool resetFocusedFieldToDefault(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step
) {
    return chord_edit_ops::resetSpecField(
        sequencer,
        step,
        sequencer.stepEdit.chordEditor.focusedField.get()
    );
}

}  // namespace core::handler::sequencer::step_chord_editor_workflow

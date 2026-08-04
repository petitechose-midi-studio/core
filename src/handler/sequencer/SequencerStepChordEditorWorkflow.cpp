#include "SequencerStepChordEditorWorkflow.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerChordEditOps.hpp"
#include "handler/sequencer/SequencerChordFormulaEditOps.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler::sequencer::step_chord_editor_workflow {
namespace chord_edit_ops = core::handler::sequencer::chord_edit_ops;
namespace input_utils = core::handler::sequencer::input_utils;

namespace {

FLASHMEM core::state::sequencer::SequencerStepChordUiState
resolvedChordState(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    auto chord = core::state::sequencer::resolveStepChordUiState(
        sequencer,
        step
    );
    const auto projection =
        core::state::sequencer::resolveActiveContentStepProjection(
            sequencer,
            step,
            scaleSettings
        );
    if (projection.valid) {
        core::state::sequencer::resolveStepChordPreview(
            chord,
            projection,
            scaleSettings
        );
    }
    return chord;
}

FLASHMEM uint8_t formulaLastFocusableItem(
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    using Spec = oc::note::sequencer::StepSequencerChordSpec;
    const uint8_t voices = chord_edit_ops::formulaVoiceCount(chord);
    return voices < Spec::MAX_CUSTOM_VOICES
        ? voices
        : static_cast<uint8_t>(Spec::MAX_CUSTOM_VOICES - 1U);
}

FLASHMEM bool formulaItemIsAdd(
    const core::state::sequencer::SequencerStepChordUiState& chord,
    uint8_t item
) {
    using Spec = oc::note::sequencer::StepSequencerChordSpec;
    const uint8_t voices = chord_edit_ops::formulaVoiceCount(chord);
    return voices < Spec::MAX_CUSTOM_VOICES && item == voices;
}

FLASHMEM uint8_t nextFormulaItem(
    uint8_t current,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    float delta
) {
    using Editor =
        core::state::sequencer::SequencerChordEditorState;
    const uint8_t last = formulaLastFocusableItem(chord);
    const int count = static_cast<int>(last) -
        static_cast<int>(Editor::FIRST_FORMULA_ITEM) + 1;
    const int index = std::clamp<int>(
        static_cast<int>(current) -
            static_cast<int>(Editor::FIRST_FORMULA_ITEM),
        0,
        count - 1
    );
    return static_cast<uint8_t>(
        nav::nextWrappedIndex(delta, index, count) +
        Editor::FIRST_FORMULA_ITEM
    );
}

FLASHMEM void leaveFormulaEditor(
    core::state::sequencer::SequencerState& sequencer
) {
    auto& editor = sequencer.stepEdit.chordEditor;
    auto subEditor = editor.subEditor.get();
    subEditor.formulaEditorActive = false;
    editor.subEditor.set(subEditor);
    editor.focusedField.set(
        core::state::sequencer::SequencerChordEditField::FORMULA
    );
    editor.formulaSnapshot.reset();
}

FLASHMEM void leaveSourceSelector(
    core::state::sequencer::SequencerState& sequencer
) {
    auto& editor = sequencer.stepEdit.chordEditor;
    auto subEditor = editor.subEditor.get();
    subEditor.sourceSelectorActive = false;
    editor.subEditor.set(subEditor);
}

}  // namespace

FLASHMEM bool active(
    const core::state::sequencer::SequencerState& sequencer
) {
    return sequencer.stepEdit.chordEditor.active.get();
}

FLASHMEM bool formulaEditorActive(
    const core::state::sequencer::SequencerState& sequencer
) {
    return sequencer.stepEdit.chordEditor.subEditor.get()
        .formulaEditorActive;
}

FLASHMEM bool sourceSelectorActive(
    const core::state::sequencer::SequencerState& sequencer
) {
    return sequencer.stepEdit.chordEditor.subEditor.get()
        .sourceSelectorActive;
}

FLASHMEM void open(core::state::sequencer::SequencerState& sequencer) {
    auto& edit = sequencer.stepEdit;
    edit.contextHold.clear();
    edit.localVariationEditActive.set(false);
    edit.chordEditor.active.set(true);
    edit.chordEditor.focusedField.set(
        core::state::sequencer::SequencerChordEditField::SHAPE
    );
    edit.chordEditor.subEditor.set({});
    edit.chordEditor.formulaSnapshot.reset();
}

FLASHMEM void close(core::state::sequencer::SequencerState& sequencer) {
    sequencer.stepEdit.chordEditor.reset();
}

FLASHMEM bool cancelSubEditor(
    core::state::sequencer::SequencerState& sequencer
) {
    auto& editor = sequencer.stepEdit.chordEditor;
    const auto subEditor = editor.subEditor.get();
    if (subEditor.sourceSelectorActive) {
        leaveSourceSelector(sequencer);
        return true;
    }
    if (!subEditor.formulaEditorActive) return false;

    const auto snapshot = editor.formulaSnapshot;
    leaveFormulaEditor(sequencer);
    (void)chord_edit_ops::restoreAuthoringSnapshot(sequencer, snapshot);
    return true;
}

FLASHMEM void toggleSourceSelector(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    auto& editor = sequencer.stepEdit.chordEditor;
    auto subEditor = editor.subEditor.get();
    if (subEditor.formulaEditorActive) return;
    if (subEditor.sourceSelectorActive) {
        leaveSourceSelector(sequencer);
        return;
    }

    const auto chord = resolvedChordState(sequencer, step, scaleSettings);
    subEditor.focusedSourceChoice = chord_edit_ops::sourceChoiceForMode(
        chord.rootContext,
        chord.mode
    );
    subEditor.sourceSelectorActive = true;
    editor.subEditor.set(subEditor);
}

FLASHMEM bool activateFocusedItem(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    auto& editor = sequencer.stepEdit.chordEditor;
    auto subEditor = editor.subEditor.get();
    if (subEditor.sourceSelectorActive) {
        const auto chord = resolvedChordState(
            sequencer,
            step,
            scaleSettings
        );
        (void)chord_edit_ops::applySourceChoice(
            sequencer,
            step,
            subEditor.focusedSourceChoice,
            chord
        );
        leaveSourceSelector(sequencer);
        return true;
    }
    if (subEditor.formulaEditorActive) {
        const auto chord = resolvedChordState(
            sequencer,
            step,
            scaleSettings
        );
        const uint8_t lastItem = formulaLastFocusableItem(chord);
        subEditor.focusedFormulaItem = std::clamp<uint8_t>(
            subEditor.focusedFormulaItem,
            core::state::sequencer::SequencerChordEditorState::
                FIRST_FORMULA_ITEM,
            lastItem
        );
        if (formulaItemIsAdd(chord, subEditor.focusedFormulaItem)) {
            if (chord_edit_ops::formulaAddAvailable(chord) &&
                chord_edit_ops::addFormulaVoice(
                    sequencer,
                    step,
                    chord,
                    scaleSettings
                )) {
                // Before insertion, the Add item index is exactly the new
                // voice index. Keep focus on the voice just materialized.
                editor.subEditor.set(subEditor);
            }
            return true;
        }
        leaveFormulaEditor(sequencer);
        return true;
    }
    if (editor.focusedField.get() !=
        core::state::sequencer::SequencerChordEditField::FORMULA) {
        return false;
    }

    if (!chord_edit_ops::captureAuthoringSnapshot(
            sequencer,
            step,
            editor.formulaSnapshot
        )) {
        return false;
    }
    subEditor.formulaEditorActive = true;
    subEditor.sourceSelectorActive = false;
    subEditor.focusedFormulaItem =
        core::state::sequencer::SequencerChordEditorState::
            FIRST_FORMULA_ITEM;
    editor.subEditor.set(subEditor);
    return true;
}

FLASHMEM void moveFocus(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    float delta
) {
    if (!nav::hasTurnDelta(delta)) return;

    auto& editor = sequencer.stepEdit.chordEditor;
    auto subEditor = editor.subEditor.get();
    if (subEditor.sourceSelectorActive) {
        const auto chord = resolvedChordState(
            sequencer,
            step,
            scaleSettings
        );
        const int current = chord_edit_ops::sourceChoiceIndex(
            chord.rootContext,
            subEditor.focusedSourceChoice
        );
        const int next = nav::nextWrappedIndex(
            delta,
            current,
            chord_edit_ops::sourceChoiceCount(chord.rootContext)
        );
        subEditor.focusedSourceChoice =
            chord_edit_ops::sourceChoiceForIndex(
                chord.rootContext,
                next
            );
        editor.subEditor.set(subEditor);
        return;
    }
    if (subEditor.formulaEditorActive) {
        const auto chord = resolvedChordState(
            sequencer,
            step,
            scaleSettings
        );
        subEditor.focusedFormulaItem = nextFormulaItem(
            subEditor.focusedFormulaItem,
            chord,
            delta
        );
        editor.subEditor.set(subEditor);
        return;
    }

    const int current = static_cast<int>(editor.focusedField.get());
    editor.focusedField.set(
        static_cast<core::state::sequencer::SequencerChordEditField>(
            nav::nextWrappedIndex(
                delta,
                current,
                chord_edit_ops::editFieldCount()
            )
        )
    );
}

FLASHMEM void setFocusedFieldValue(
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    float normalized
) {
    const auto chord = resolvedChordState(
        sequencer,
        step,
        scaleSettings
    );
    auto& editor = sequencer.stepEdit.chordEditor;
    const auto& subEditor = editor.subEditor.get();
    if (subEditor.sourceSelectorActive) return;
    if (subEditor.formulaEditorActive) {
        if (formulaItemIsAdd(chord, subEditor.focusedFormulaItem)) return;
        chord_edit_ops::applyFormulaVoice(
            sequencer,
            step,
            chord,
            subEditor.focusedFormulaItem,
            scaleSettings,
            normalized
        );
        return;
    }

    chord_edit_ops::applySpecField(
        sequencer,
        step,
        editor.focusedField.get(),
        chord,
        scaleSettings,
        normalized
    );
}

FLASHMEM void configureFocusedFieldEncoder(
    oc::api::EncoderAPI& encoders,
    oc::type::EncoderID encoderId,
    core::state::sequencer::SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    using Field = core::state::sequencer::SequencerChordEditField;
    using ChordSpec = oc::note::sequencer::StepSequencerChordSpec;

    const auto chord = resolvedChordState(
        sequencer,
        step,
        scaleSettings
    );
    const auto& editor = sequencer.stepEdit.chordEditor;
    const auto& subEditor = editor.subEditor.get();

    encoders.setDiscreteTicksPerStep(encoderId, 4);
    encoders.setNormalizedTurns(encoderId, 0.5f);

    if (subEditor.sourceSelectorActive) {
        encoders.setDiscreteSteps(encoderId, 1);
        encoders.setPosition(encoderId, 0.0f);
        return;
    }
    if (subEditor.formulaEditorActive) {
        const uint8_t voiceIndex = subEditor.focusedFormulaItem;
        if (formulaItemIsAdd(chord, voiceIndex)) {
            encoders.setDiscreteSteps(encoderId, 1);
            encoders.setPosition(encoderId, 0.0f);
            return;
        }
        encoders.setDiscreteSteps(
            encoderId,
            chord_edit_ops::formulaVoiceChoiceCount(chord, voiceIndex)
        );
        encoders.setPosition(
            encoderId,
            chord_edit_ops::formulaVoiceToNormalized(chord, voiceIndex)
        );
        return;
    }

    const auto field = editor.focusedField.get();
    switch (field) {
        case Field::SHAPE: {
            const bool scaleBased = chord.intervalsUseScaleDegrees;
            const uint8_t count =
                oc::note::sequencer::chordPresetChoiceCount(scaleBased);
            const auto harmony = chord.preview.valid
                ? chord.preview.harmony
                : chord.spec.harmony();
            encoders.setDiscreteSteps(encoderId, count);
            encoders.setPosition(
                encoderId,
                input_utils::indexToNormalized(
                    oc::note::sequencer::chordPresetChoiceIndex(
                        harmony,
                        scaleBased
                    ),
                    count
                )
            );
            return;
        }
        case Field::FORMULA:
        case Field::PITCH_CONTEXT:
            encoders.setDiscreteSteps(encoderId, 1);
            encoders.setPosition(encoderId, 0.0f);
            return;
        case Field::INVERSION: {
            const uint8_t count = std::max<uint8_t>(
                chord.spec.voices(),
                1U
            );
            const uint8_t inversion = std::min<uint8_t>(
                chord.spec.inversion(),
                count - 1U
            );
            encoders.setDiscreteSteps(encoderId, count);
            encoders.setPosition(
                encoderId,
                input_utils::indexToNormalized(inversion, count)
            );
            return;
        }
        case Field::VOICING: {
            constexpr uint8_t count = static_cast<uint8_t>(
                oc::note::sequencer::StepSequencerChordVoicing::Count
            );
            const auto voicing = chord.spec.voicing();
            encoders.setDiscreteSteps(encoderId, count);
            encoders.setPosition(
                encoderId,
                input_utils::indexToNormalized(
                    static_cast<int>(voicing),
                    count
                )
            );
            return;
        }
        case Field::STRUM:
            encoders.setDiscreteSteps(
                encoderId,
                static_cast<uint8_t>(
                    (ChordSpec::MAX_STRUM - ChordSpec::MIN_STRUM) + 1
                )
            );
            encoders.setNormalizedTurns(encoderId, 2.0f);
            encoders.setPosition(
                encoderId,
                chord_edit_ops::signedToNormalized(
                    chord.spec.strum,
                    ChordSpec::MIN_STRUM,
                    ChordSpec::MAX_STRUM
                )
            );
            return;
        case Field::VELOCITY_CONTOUR:
            encoders.setDiscreteSteps(
                encoderId,
                static_cast<uint8_t>(
                    (ChordSpec::MAX_VELOCITY_CURVE -
                     ChordSpec::MIN_VELOCITY_CURVE) + 1
                )
            );
            encoders.setNormalizedTurns(encoderId, 2.0f);
            encoders.setPosition(
                encoderId,
                chord_edit_ops::signedToNormalized(
                    chord.spec.velocityCurve,
                    ChordSpec::MIN_VELOCITY_CURVE,
                    ChordSpec::MAX_VELOCITY_CURVE
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
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    const auto chord = resolvedChordState(
        sequencer,
        step,
        scaleSettings
    );
    const auto& editor = sequencer.stepEdit.chordEditor;
    auto subEditor = editor.subEditor.get();
    if (subEditor.sourceSelectorActive) {
        const auto defaultChoice = chord.rootContext
            ? core::state::sequencer::SequencerChordSourceChoice::
                  SINGLE_NOTE
            : core::state::sequencer::SequencerChordSourceChoice::
                  PARENT_CHORD;
        const bool changed = chord_edit_ops::applySourceChoice(
            sequencer,
            step,
            defaultChoice,
            chord
        );
        subEditor.focusedSourceChoice = defaultChoice;
        sequencer.stepEdit.chordEditor.subEditor.set(subEditor);
        return changed;
    }
    if (subEditor.formulaEditorActive) {
        const uint8_t item = subEditor.focusedFormulaItem;
        if (formulaItemIsAdd(chord, item)) return false;
        const uint8_t oldCount = chord_edit_ops::formulaVoiceCount(chord);
        const bool changed = chord_edit_ops::applyFormulaVoiceRemoveIntent(
            sequencer,
            step,
            chord,
            item,
            scaleSettings
        );
        if (!changed || item <= 1U) return changed;

        const uint8_t newCount = static_cast<uint8_t>(oldCount - 1U);
        if (item >= newCount) {
            subEditor.focusedFormulaItem = static_cast<uint8_t>(
                newCount - 1U
            );
            sequencer.stepEdit.chordEditor.subEditor.set(subEditor);
        }
        return true;
    }
    return chord_edit_ops::resetSpecField(
        sequencer,
        step,
        editor.focusedField.get(),
        chord
    );
}

}  // namespace core::handler::sequencer::step_chord_editor_workflow

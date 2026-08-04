#include "handler/sequencer/SequencerStepValueRowWorkflow.hpp"

#include <config/PlatformCompat.hpp>

#include "handler/sequencer/SequencerChordEditOps.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

namespace core::handler::sequencer::step_value_row_workflow {
namespace chord_edit_ops = core::handler::sequencer::chord_edit_ops;
namespace input_utils = core::handler::sequencer::input_utils;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;

namespace {

FLASHMEM bool focusedRowIsActivated(const core::state::sequencer::SequencerState& sequencer) {
    return step_edit_rows::isActivated(sequencer.stepEdit.focusedRow.get());
}

FLASHMEM bool focusedRowIsProperty(const core::state::sequencer::SequencerState& sequencer) {
    return step_edit_rows::isProperty(sequencer.stepEdit.focusedRow.get());
}

FLASHMEM bool focusedRowIsChord(const core::state::sequencer::SequencerState& sequencer) {
    return step_edit_rows::isChord(sequencer.stepEdit.focusedRow.get());
}

FLASHMEM core::state::sequencer::StepProperty focusedProperty(
    const core::state::sequencer::SequencerState& sequencer) {
    return step_edit_rows::propertyForRow(sequencer.stepEdit.focusedRow.get());
}

FLASHMEM void configureStepPropertyEncoder(
    oc::api::EncoderAPI& encoders, oc::type::EncoderID encoderId,
    core::state::sequencer::StepProperty property,
    const core::state::sequencer::SequencerState& sequencer, uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings) {
    const auto config = input_utils::encoderConfigForProperty(
        property, sequencer.pattern.pitchEditMode, scaleSettings);
    encoders.setDiscreteTicksPerStep(encoderId, config.discreteTicksPerStep);
    encoders.setNormalizedTurns(encoderId, config.normalizedTurns);
    encoders.setDiscreteSteps(encoderId, config.discreteSteps);
    encoders.setPosition(
        encoderId, core::state::sequencer::activeContentStepPropertyToNormalized(
                       sequencer, step, property, sequencer.pattern.pitchEditMode, scaleSettings));
}

}  // namespace

FLASHMEM bool focusedRowIsValue(const core::state::sequencer::SequencerState& sequencer) {
    return focusedRowIsActivated(sequencer) || focusedRowIsProperty(sequencer) ||
           focusedRowIsChord(sequencer);
}

FLASHMEM bool focusedRowSupportsLocalVariation(
    const core::state::sequencer::SequencerState& sequencer) {
    if (!focusedRowIsProperty(sequencer)) return false;
    return core::state::sequencer::stepPropertySupportsLocalVariation(focusedProperty(sequencer));
}

FLASHMEM bool setFocusedRowValue(core::state::sequencer::SequencerState& sequencer, uint8_t step,
                                 oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
                                 float normalized) {
    if (focusedRowIsActivated(sequencer)) {
        return core::state::sequencer::setActiveContentStepEnabled(sequencer, step,
                                                                   normalized >= 0.5f);
    }

    if (focusedRowIsChord(sequencer)) {
        const auto chord = core::state::sequencer::resolveStepChordUiState(sequencer, step);
        const int choice = input_utils::normalizedToIndex(
            normalized, chord_edit_ops::quickChoiceCount(chord.rootContext));
        return chord_edit_ops::applyQuickChoice(
            sequencer, step, choice,
            core::state::sequencer::pitchContextUsesScaleDegrees(
                core::state::sequencer::authoringPattern(sequencer).pitchEditMode, scaleSettings));
    }

    if (!focusedRowIsProperty(sequencer)) return false;

    const auto property = focusedProperty(sequencer);
    if (sequencer.stepEdit.localVariationEditActive.get() &&
        core::state::sequencer::stepPropertySupportsLocalVariation(property)) {
        const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
        const uint8_t range = input_utils::normalizedToVariationRange(property, normalized);
        const bool changed = core::state::sequencer::setNodeLocalVariationRange(
            sequencer.pattern, nodeId, property, range);
        if (changed) { sequencer.invalidateVariationTelemetry(); }
        return changed;
    }

    return core::state::sequencer::setActiveContentStepFromNormalized(
        sequencer, step, property, normalized, sequencer.pattern.pitchEditMode, scaleSettings);
}

FLASHMEM void configureFocusedRowEncoder(
    oc::api::EncoderAPI& encoders, oc::type::EncoderID encoderId,
    const core::state::sequencer::SequencerState& sequencer, uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings) {
    if (focusedRowIsActivated(sequencer)) {
        const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
            sequencer, step, scaleSettings);
        if (!projection.valid) return;
        encoders.setDiscreteTicksPerStep(encoderId, 8);
        encoders.setNormalizedTurns(encoderId, 0.25f);
        encoders.setDiscreteSteps(encoderId, 2);
        encoders.setPosition(encoderId, projection.enabled ? 1.0f : 0.0f);
        return;
    }

    if (focusedRowIsChord(sequencer)) {
        const auto chord = core::state::sequencer::resolveStepChordUiState(sequencer, step);
        encoders.setDiscreteTicksPerStep(encoderId, 8);
        encoders.setNormalizedTurns(encoderId, 0.25f);
        encoders.setDiscreteSteps(
            encoderId, static_cast<uint8_t>(chord_edit_ops::quickChoiceCount(chord.rootContext)));
        encoders.setPosition(encoderId, input_utils::indexToNormalized(
                                            chord_edit_ops::quickChoiceIndex(chord),
                                            chord_edit_ops::quickChoiceCount(chord.rootContext)));
        return;
    }

    if (!focusedRowIsProperty(sequencer)) return;

    const auto property = focusedProperty(sequencer);
    if (sequencer.stepEdit.localVariationEditActive.get() &&
        core::state::sequencer::stepPropertySupportsLocalVariation(property)) {
        const auto config = input_utils::encoderConfigForVariationRange(property);
        encoders.setDiscreteTicksPerStep(encoderId, config.discreteTicksPerStep);
        encoders.setNormalizedTurns(encoderId, config.normalizedTurns);
        encoders.setDiscreteSteps(encoderId, config.discreteSteps);

        uint8_t range = 0;
        const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
        const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
        if (graph != nullptr) {
            const auto* node = graph->stepNode(nodeId);
            if (node != nullptr) {
                range = core::state::sequencer::nodeLocalVariationRange(*node, property);
            }
        }
        encoders.setPosition(encoderId, input_utils::variationRangeToNormalized(property, range));
        return;
    }

    configureStepPropertyEncoder(encoders, encoderId, property, sequencer, step, scaleSettings);
}

FLASHMEM bool resetFocusedRowToDefault(core::state::sequencer::SequencerState& sequencer,
                                       uint8_t step) {
    bool changed = false;
    if (focusedRowIsActivated(sequencer)) {
        changed = core::state::sequencer::setActiveContentStepEnabled(sequencer, step, false);
    } else if (focusedRowIsChord(sequencer)) {
        changed = core::state::sequencer::clearNodeChordState(
            sequencer.pattern, core::state::sequencer::activeContentStepNodeId(sequencer, step));
    } else if (focusedRowIsProperty(sequencer)) {
        changed = core::state::sequencer::resetActiveContentStepPropertyToDefault(
            sequencer, step, focusedProperty(sequencer));
    }

    if (changed) { sequencer.invalidateVariationTelemetry(); }
    return changed;
}

}  // namespace core::handler::sequencer::step_value_row_workflow

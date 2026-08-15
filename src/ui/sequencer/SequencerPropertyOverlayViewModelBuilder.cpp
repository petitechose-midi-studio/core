#include "ui/sequencer/SequencerPropertyOverlayViewModelBuilder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <oc/type/TextFormat.hpp>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/DrumPatternState.hpp"
#include "state/sequencer/SequencerCcLaneDomain.hpp"
#include "state/sequencer/SequencerCcLanePropertySelection.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerQuickControls.hpp"
#include "state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"
#include "ui/sequencer/SequencerQuickControlVisuals.hpp"
#include "ui/sequencer/SequencerStepContentDraftTransitionLabels.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer {

namespace {

using QuickItem = core::state::sequencer::PatternQuickControlItem;

using DrumDimension =
    core::state::sequencer::DrumSequencerDimension;
using DrumProperty = core::state::sequencer::DrumSequencerProperty;
using DrumPatternField = core::state::sequencer::DrumPatternDefaultField;

FLASHMEM void formatDrumPropertyValue(
    char* buffer,
    size_t size,
    const core::state::sequencer::DrumSequencerState& drumUi
) {
    if (!buffer || size == 0U || !drumUi.drumTrack) return;
    const uint8_t step = std::min<uint8_t>(
        drumUi.focusedStep,
        static_cast<uint8_t>(drumUi.MAX_STEPS - 1U)
    );
    const auto& lane =
        drumUi.drumTrack->pattern.lanes[drumUi.selectedLane];
    switch (drumUi.property) {
        case DrumProperty::STATE:
            std::snprintf(
                buffer,
                size,
                "%s",
                drumUi.drumTrack->pattern.stepEnabled(
                    drumUi.selectedLane,
                    step
                ) ? "On" : "Off"
            );
            return;
        case DrumProperty::PROBABILITY:
            std::snprintf(
                buffer,
                size,
                "%u%%",
                static_cast<unsigned>(lane.probability[step])
            );
            return;
        case DrumProperty::GATE:
            std::snprintf(
                buffer,
                size,
                "%u%%",
                static_cast<unsigned>(lane.gate[step])
            );
            return;
        case DrumProperty::NUDGE:
            std::snprintf(
                buffer,
                size,
                "%+d%%",
                static_cast<int>(lane.nudge[step])
            );
            return;
        case DrumProperty::VELOCITY:
        case DrumProperty::COUNT:
        default:
            std::snprintf(
                buffer,
                size,
                "%u",
                static_cast<unsigned>(lane.velocity[step])
            );
            return;
    }
}

FLASHMEM const char* drumDimensionIcon(DrumDimension dimension) {
    switch (dimension) {
        case DrumDimension::MODE: return standalone::icons::ROUTE_PIN;
        case DrumDimension::DIVISION: return standalone::icons::DIVISION;
        case DrumDimension::LENGTH:
        case DrumDimension::COUNT:
        default: return standalone::icons::LENGTH;
    }
}

FLASHMEM const char* drumDimensionLabel(DrumDimension dimension) {
    switch (dimension) {
        case DrumDimension::MODE: return "Lane timing";
        case DrumDimension::DIVISION: return "Division";
        case DrumDimension::LENGTH:
        case DrumDimension::COUNT:
        default: return "Length";
    }
}

FLASHMEM uint32_t drumDimensionColor(DrumDimension dimension) {
    switch (dimension) {
        case DrumDimension::MODE:
            return standalone::theme::color::STEP_STATE;
        case DrumDimension::DIVISION:
            return standalone::theme::color::STEP_DIVISION;
        case DrumDimension::LENGTH:
        case DrumDimension::COUNT:
        default: return standalone::theme::color::STEP_LENGTH;
    }
}

FLASHMEM void formatDrumDimensionValue(
    char* buffer,
    size_t size,
    const core::state::sequencer::DrumSequencerState& drumUi
) {
    if (!buffer || size == 0U || !drumUi.drumTrack) return;
    const auto& pattern = drumUi.drumTrack->pattern;
    switch (drumUi.dimension) {
        case DrumDimension::MODE:
            std::snprintf(
                buffer,
                size,
                "%s",
                pattern.lanes[drumUi.selectedLane].timing.mode ==
                        core::state::sequencer::DrumLaneTimingMode::CUSTOM
                    ? "Custom"
                    : "Inherited"
            );
            return;
        case DrumDimension::DIVISION:
            std::snprintf(
                buffer,
                size,
                "1/%u",
                static_cast<unsigned>(
                    pattern.effectiveStepsPerBeat(drumUi.selectedLane) * 4U
                )
            );
            return;
        case DrumDimension::LENGTH:
        case DrumDimension::COUNT:
        default:
            std::snprintf(
                buffer,
                size,
                "%u steps",
                static_cast<unsigned>(
                    pattern.effectiveLength(drumUi.selectedLane)
                )
            );
            return;
    }
}

FLASHMEM const char* drumPatternFieldIcon(DrumPatternField field) {
    return field == DrumPatternField::DIVISION
        ? standalone::icons::DIVISION
        : standalone::icons::LENGTH;
}

FLASHMEM const char* drumPatternFieldLabel(DrumPatternField field) {
    return field == DrumPatternField::DIVISION
        ? "Default division"
        : "Default length";
}

FLASHMEM uint32_t drumPatternFieldColor(DrumPatternField field) {
    return field == DrumPatternField::DIVISION
        ? standalone::theme::color::STEP_DIVISION
        : standalone::theme::color::STEP_LENGTH;
}

FLASHMEM void formatDrumPatternFieldValue(
    char* buffer,
    size_t size,
    const core::state::sequencer::DrumSequencerState& drumUi
) {
    if (!buffer || size == 0U || !drumUi.drumTrack) return;
    if (drumUi.patternDefaultField == DrumPatternField::DIVISION) {
        std::snprintf(
            buffer,
            size,
            "1/%u",
            static_cast<unsigned>(
                drumUi.drumTrack->pattern.defaultStepsPerBeat * 4U
            )
        );
    } else {
        std::snprintf(
            buffer,
            size,
            "%u steps",
            static_cast<unsigned>(drumUi.drumTrack->pattern.defaultLength)
        );
    }
}

const char* sequencerContextLabel(core::state::StructureNavigationFocus focus) {
    switch (focus) {
        case core::state::StructureNavigationFocus::TRACK:
            return "Track";
        case core::state::StructureNavigationFocus::STEP:
            return "Step";
        case core::state::StructureNavigationFocus::PAGE:
        default:
            return "Pattern";
    }
}

const char* sequencerContextIcon(core::state::StructureNavigationFocus focus) {
    switch (focus) {
        case core::state::StructureNavigationFocus::TRACK:
            return standalone::icons::ROUTING;
        case core::state::StructureNavigationFocus::STEP:
            return standalone::icons::NOTE;
        case core::state::StructureNavigationFocus::PAGE:
        default:
            return standalone::icons::LENGTH;
    }
}

const char* stepContentActionIcon(
    core::state::sequencer::SequencerStepContentAction action
) {
    using Action = core::state::sequencer::SequencerStepContentAction;
    switch (action) {
        case Action::CHORD:
            return standalone::icons::CHORD;
        case Action::MICRO_SEQUENCE:
            return standalone::icons::MICRO_SEQUENCE;
        case Action::CYCLE_STATES:
            return standalone::icons::CYCLE_STATE;
        default:
            return standalone::icons::CHORD;
    }
}

const char* stepContentActionLabel(
    core::state::sequencer::SequencerStepContentAction action
) {
    using Action = core::state::sequencer::SequencerStepContentAction;
    switch (action) {
        case Action::CHORD:
            return "Chord";
        case Action::MICRO_SEQUENCE:
            return "Micro";
        case Action::CYCLE_STATES:
            return "Cycle";
        default:
            return "Chord";
    }
}

FLASHMEM const char* stepContentDraftFailureValue(
    const core::state::sequencer::SequencerStepContentDraftSession& draft
) {
    using Failure = core::state::sequencer::SequencerStepContentDraftFailure;
    switch (draft.failure) {
        case Failure::OUT_OF_MEMORY:
            return "Out of memory";
        case Failure::HISTORY_UNAVAILABLE:
            return "History unavailable";
        case Failure::PUBLISH_FAILED:
            return "Publish failed";
        case Failure::UNPUBLISHABLE_MUTATION:
            return "Unsupported edit";
        case Failure::TRANSITION_BLOCKED:
            return propertyOverlayStepContentDraftTransitionLabel(draft.blockedTransition);
        case Failure::NONE:
        default:
            return "";
    }
}

void formatQuickControlValue(
    char* buffer,
    size_t size,
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::project::ProjectNavigationState& projectNavigation,
    QuickItem item
) {
    if (!buffer || size == 0) return;
    const auto& pattern = core::state::sequencer::authoringPattern(sequencer);
    if (core::state::sequencer::isChildContentView(sequencer) &&
        item == QuickItem::DIVISION) {
        buffer[0] = '\0';
        return;
    }

    switch (item) {
        case QuickItem::OFFSET:
            oc::type::text::formatSigned(
                buffer,
                size,
                sequencer.patternQuickControls.offsetSteps.get(),
                true
            );
            return;
        case QuickItem::DIVISION:
            oc::type::text::formatFraction(
                buffer,
                size,
                1U,
                static_cast<unsigned>(
                    4U * static_cast<uint16_t>(pattern.stepsPerBeat.get())
                )
            );
            return;
        case QuickItem::SWING:
            std::snprintf(
                buffer,
                size,
                "%u%%",
                static_cast<unsigned>(
                    pattern.effectiveSwingPercent(
                        projectNavigation.transportSwingPercent
                    )
                )
            );
            return;
        case QuickItem::NUDGE:
            std::snprintf(
                buffer,
                size,
                "%+d%%",
                static_cast<int>(pattern.patternNudgePercent.get())
            );
            return;
        case QuickItem::LENGTH:
        default:
            oc::type::text::formatUnsigned(
                buffer,
                size,
                static_cast<unsigned>(
                    core::state::sequencer::activeContentLength(sequencer)
                )
            );
            return;
    }
}

uint8_t localVariationRangeForStep(
    const core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::StepProperty property
) {
    if (property == core::state::sequencer::StepProperty::PROBABILITY) return 0;

    const auto& selector = sequencer.stepPropertyInlineSelector;
    if (selector.localVariationStepIndex >=
        core::state::sequencer::activeContentLength(sequencer)) {
        return 0;
    }

    const auto* graph = core::state::sequencer::graphView(
        core::state::sequencer::authoringPattern(sequencer)
    );
    if (graph == nullptr) return 0;

    const auto nodeId = core::state::sequencer::activeContentStepNodeId(
        sequencer,
        selector.localVariationStepIndex
    );
    const auto* node = graph->stepNode(nodeId);
    return node ? core::state::sequencer::nodeLocalVariationRange(*node, property) : 0;
}

void formatLocalVariationRangeText(
    char* buffer,
    size_t size,
    core::state::sequencer::StepProperty property,
    uint8_t range,
    bool pitchUsesScaleDegrees
) {
    if (!buffer || size == 0) return;
    if (!core::state::sequencer::stepPropertySupportsLocalVariation(property)) {
        std::snprintf(buffer, size, "--");
        return;
    }

    const char* unit = "";
    if (property == core::state::sequencer::StepProperty::GATE) {
        unit = "%";
    } else if (property == core::state::sequencer::StepProperty::NOTE &&
               pitchUsesScaleDegrees) {
        unit = "d";
    } else if (property == core::state::sequencer::StepProperty::NOTE) {
        unit = "st";
    }

    std::snprintf(buffer, size, "±%u%s", static_cast<unsigned>(range), unit);
}

void formatLocalVariationOverlayValue(
    char* buffer,
    size_t size,
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::StepProperty property,
    uint8_t step,
    uint8_t range
) {
    if (!buffer || size == 0) return;

    const auto displayContext =
        core::state::sequencer::makeSequencerResolvedDisplayProjectionContext(
            sequencer,
            tracks.projectScaleSettings(),
            property
        );
    char rangeText[8] = {};
    formatLocalVariationRangeText(
        rangeText,
        sizeof(rangeText),
        property,
        range,
        displayContext.scaleSettings.isConstrained()
    );
    if (!core::state::sequencer::stepPropertySupportsLocalVariation(property)) {
        std::snprintf(buffer, size, "%s", rangeText);
        return;
    }

    const auto touchedMask = sequencer.stepInlineFeedback.touchedMask.get();
    const bool stepInlineEditActive =
        sequencer.stepInlineFeedback.visible.get() && touchedMask.test(step);
    const auto resolved =
        core::state::sequencer::buildSequencerResolvedStepDisplayState(
            displayContext,
            step,
            stepInlineEditActive
        );
    if (!resolved.valid) {
        std::snprintf(buffer, size, "%s", rangeText);
        return;
    }

    const auto values =
        core::state::sequencer::sequencerResolvedStepDisplayValues(resolved);
    char valueText[8] = {};
    core::state::sequencer::formatStepPropertyValue(
        valueText,
        sizeof(valueText),
        property,
        values.note,
        values.velocity,
        values.gate,
        values.nudge,
        resolved.probability
    );
    std::snprintf(buffer, size, "%s %s", valueText, rangeText);
}

}  // namespace

FLASHMEM StepPropertySelectionOverlayProps buildSequencerPropertySelectionOverlayProps(
    const SequencerViewModelSource& source
) {
    const auto& sequencer = source.sequencer;

    if (sequencer.drumSequencer.typePickerVisible()) {
        const bool drum = sequencer.drumSequencer.selectedKind ==
            core::state::sequencer::DrumSequencerKind::DRUM;
        return {
            .visible = true,
            .customContent = true,
            .icon = drum
                ? standalone::icons::CYCLE_STATE
                : standalone::icons::NOTE,
            .label = drum ? "Track: Drum" : "Track: Instrument",
            .value = "Turn NAV · press",
            .color = drum
                ? standalone::theme::color::STEP_VELOCITY
                : standalone::theme::color::STEP_PITCH,
        };
    }
    if (sequencer.drumSequencer.kitPickerVisible()) {
        const auto preset =
            sequencer.drumSequencer.selectedKitPreset;
        return {
            .visible = true,
            .customContent = true,
            .icon = preset == core::state::sequencer::DrumKitPreset::EMPTY
                ? standalone::icons::ACTION_CLEAR
                : standalone::icons::CYCLE_STATE,
            .label = core::state::sequencer::drumKitPresetLabel(preset),
            .value = "Turn NAV · press",
            .color = standalone::theme::color::STEP_VELOCITY,
        };
    }
    if (sequencer.drumSequencer.gridVisible() &&
        sequencer.drumSequencer.selectorVisible()) {
        const auto& drumUi = sequencer.drumSequencer;
        if (drumUi.selector == core::state::sequencer::
                DrumSequencerSelector::LANE_EDITOR) {
            return {};
        }
        StepPropertySelectionOverlayProps props{
            .visible = true,
            .customContent = true,
            .useValueText = true,
        };
        if (drumUi.selector == core::state::sequencer::
                DrumSequencerSelector::DIMENSION) {
            props.icon = drumDimensionIcon(drumUi.dimension);
            props.label = drumDimensionLabel(drumUi.dimension);
            props.color = drumDimensionColor(drumUi.dimension);
            formatDrumDimensionValue(
                props.valueText.data(),
                props.valueText.size(),
                drumUi
            );
        } else if (drumUi.selector == core::state::sequencer::
                       DrumSequencerSelector::PROPERTY) {
            const auto propertyVisual =
                visual::buildDrumPropertyVisual(drumUi.property);
            props.icon = propertyVisual.icon;
            props.label = propertyVisual.label;
            props.color = propertyVisual.color;
            formatDrumPropertyValue(
                props.valueText.data(),
                props.valueText.size(),
                drumUi
            );
        } else {
            props.icon = drumPatternFieldIcon(drumUi.patternDefaultField);
            props.label = drumPatternFieldLabel(drumUi.patternDefaultField);
            props.color = drumPatternFieldColor(drumUi.patternDefaultField);
            formatDrumPatternFieldValue(
                props.valueText.data(),
                props.valueText.size(),
                drumUi
            );
        }
        return props;
    }

    if (sequencer.stepContentDraft.exitPromptVisible.get()) {
        using Choice = core::state::sequencer::SequencerStepContentDraftExitChoice;
        switch (sequencer.stepContentDraft.exitChoice.get()) {
            case Choice::CONTINUE:
                return {
                    .visible = true,
                    .customContent = true,
                    .icon = standalone::icons::ACTION_BACKWARD,
                    .label = "Continue",
                    .value = "NAV turn/press",
                    .color = standalone::theme::color::STEP_STATE,
                };
            case Choice::DISCARD:
                return {
                    .visible = true,
                    .customContent = true,
                    .icon = standalone::icons::ACTION_CANCEL,
                    .label = "Discard draft",
                    .value = "NAV turn/press",
                    .color = standalone::theme::color::STEP_CYCLE_STATE,
                };
            case Choice::SAVE:
            case Choice::COUNT:
            default:
                return {
                    .visible = true,
                    .customContent = true,
                    .icon = standalone::icons::ACTION_VALIDATE,
                    .label = sequencer.stepContentDraft.failure ==
                                     core::state::sequencer::
                                         SequencerStepContentDraftFailure::NONE
                                 ? "Save draft"
                                 : "Apply failed",
                    .value = sequencer.stepContentDraft.failure ==
                                     core::state::sequencer::
                                         SequencerStepContentDraftFailure::NONE
                                 ? "NAV turn/press"
                                 : stepContentDraftFailureValue(
                                       sequencer.stepContentDraft
                                   ),
                    .color = standalone::theme::color::STEP_MICRO_SEQUENCE,
                };
        }
    }

    if (sequencer.stepContentDraft.failure !=
        core::state::sequencer::SequencerStepContentDraftFailure::NONE) {
        return {
            .visible = true,
            .customContent = true,
            .icon = standalone::icons::ACTION_CANCEL,
            .label = sequencer.stepContentDraft.failure ==
                             core::state::sequencer::
                                 SequencerStepContentDraftFailure::TRANSITION_BLOCKED
                         ? "Finish draft first"
                         : "Apply failed",
            .value = stepContentDraftFailureValue(sequencer.stepContentDraft),
            .color = standalone::theme::color::STEP_CYCLE_STATE,
        };
    }

    if (sequencer.contextSelector.visible) {
        const auto focus = sequencer.contextSelector.previewFocus;
        return {
            .visible = true,
            .customContent = true,
            .icon = sequencerContextIcon(focus),
            .label = sequencerContextLabel(focus),
            .value = "Turn NAV · release",
            .color = standalone::theme::color::STEP_STATE,
        };
    }

    if (sequencer.stepContentSelector.selecting.get()) {
        const auto action = sequencer.stepContentSelector.focusedAction.get();
        StepPropertySelectionOverlayProps props{
            .visible = true,
            .customContent = true,
            .icon = stepContentActionIcon(action),
            .label = stepContentActionLabel(action),
            .color = standalone::theme::color::STEP_STATE,
        };
        return props;
    }

    if (sequencer.stepPropertyInlineSelector.selecting.get()) {
        const int selectedIndex =
            sequencer.stepPropertyInlineSelector.selectedIndex.get();
        if (core::state::sequencer::sequencerPropertySelectionIsState(
                selectedIndex
            )) {
            const uint8_t length =
                core::state::sequencer::activeContentLength(sequencer);
            const uint8_t step = length == 0
                ? 0
                : std::min<uint8_t>(
                      sequencer.focusedStep.get(),
                      static_cast<uint8_t>(length - 1U)
                  );
            return {
                .visible = true,
                .customContent = true,
                .icon = standalone::icons::ACTION_VALIDATE,
                .label = "State",
                .value = length > 0 &&
                        core::state::sequencer::activeContentStepEnabled(
                            sequencer,
                            step
                        )
                    ? "On"
                    : "Off",
                .color = standalone::theme::color::STEP_STATE,
            };
        }
        if (selectedIndex >=
            core::state::sequencer::SEQUENCER_BASE_STEP_PROPERTY_COUNT) {
            StepPropertySelectionOverlayProps props{
                .visible = true,
                .customContent = true,
                .icon = standalone::icons::MIDI_CC,
                .label = "CC lane",
                .useValueText = true,
                .color = standalone::theme::color::MACRO_CC_COLOR,
            };
            const auto* bank =
                core::state::sequencer::sequencerCcLaneView(
                    core::state::sequencer::authoringPattern(sequencer)
                );
            const int8_t laneIndex =
                core::state::sequencer::sequencerPropertySelectionLaneAt(
                    bank,
                    selectedIndex
                );
            if (laneIndex >= 0 && bank != nullptr) {
                const auto& lane = bank->lanes[static_cast<uint8_t>(laneIndex)];
                std::snprintf(
                    props.valueText.data(),
                    props.valueText.size(),
                    "CC %u",
                    static_cast<unsigned>(lane.destination.controller)
                );
                return props;
            }
            const uint8_t count = bank
                ? core::state::sequencer::sequencerCcLaneCount(*bank)
                : 0;
            std::snprintf(
                props.valueText.data(),
                props.valueText.size(),
                "+ Add · %u/4",
                static_cast<unsigned>(count)
            );
            return props;
        }
        if (sequencer.stepPropertyInlineSelector.macroLocalVariationEditActive.get()) {
            const auto property = sequencer.activeStepProperty.get();
            StepPropertySelectionOverlayProps props{
                .visible = true,
                .property = property,
                .customContent = true,
                .icon = visual::propertyIconGlyph(property),
                .label = semantic::labelForProperty(property),
                .useValueText = true,
                .color = semantic::colorForProperty(property),
            };
            formatLocalVariationOverlayValue(
                props.valueText.data(),
                props.valueText.size(),
                sequencer,
                source.tracks,
                property,
                sequencer.stepPropertyInlineSelector.localVariationStepIndex,
                localVariationRangeForStep(sequencer, property)
            );
            return props;
        }

        return {
            .visible = true,
            .property = sequencer.activeStepProperty.get(),
        };
    }

    if (sequencer.patternQuickControls.selecting.get() ||
        sequencer.patternQuickControls.feedbackVisible.get()) {
        const auto item = sequencer.patternQuickControls.focusedItem.get();
        StepPropertySelectionOverlayProps props{
            .visible = true,
            .customContent = true,
            .icon = visual::quickControlIconGlyph(item),
            .label = core::state::sequencer::quickControlLabel(item),
            .useValueText = true,
            .color = visual::quickControlColor(item),
        };
        formatQuickControlValue(
            props.valueText.data(),
            props.valueText.size(),
            sequencer,
            source.projectNavigation,
            item
        );
        return props;
    }

    return {.visible = false};
}

}  // namespace core::ui::sequencer

#include "ui/sequencer/SequencerPropertyOverlayViewModelBuilder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <oc/type/TextFormat.hpp>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
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

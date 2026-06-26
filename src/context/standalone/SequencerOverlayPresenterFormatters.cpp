#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <oc/type/TextFormat.hpp>

#include "context/standalone/SequencerChordOverlayFormatters.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/SequencerActionStripVisuals.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {
namespace step_edit_rows = core::state::sequencer::step_edit_rows;
namespace {

constexpr uint32_t ACTIVATED_ICON_COLOR =
    core::ui::sequencer::semantic::color(core::ui::sequencer::semantic::Tone::STATE);
constexpr uint32_t MICRO_SEQUENCE_COLOR =
    core::ui::sequencer::semantic::color(core::ui::sequencer::semantic::Tone::MICRO_SEQUENCE);
constexpr uint32_t CYCLE_STATE_COLOR =
    core::ui::sequencer::semantic::color(core::ui::sequencer::semantic::Tone::CYCLE_STATE);

using StripProps = core::ui::ContextActionStripProps;
using Visual = core::ui::ContextActionStripVisualState;
using Tone = core::ui::ContextActionStripTone;

FLASHMEM core::ui::ContextActionStripSlotProps stepPresetStripSlot() {
    return core::ui::makeStandaloneIconStripSlot(
        ::standalone::icons::SETTINGS_GEAR,
        Visual::ACTIVE,
        Tone::NEUTRAL
    );
}

FLASHMEM const char* availabilityLabel(
    const core::state::sequencer::StepContentCreationAvailability& availability
) {
    using Reason = core::state::sequencer::StepContentCreationBlockReason;

    if (availability.opensExisting) return "Edit";
    if (availability.canCreateOrOpen) return "Create";

    switch (availability.blockedReason) {
        case Reason::MAX_DEPTH_REACHED:
            return "Max depth";
        case Reason::GRAPH_LIMIT_REACHED:
            return "Limit";
        case Reason::INVALID_FOCUSED_STEP:
            return "Invalid";
        case Reason::INACTIVE_CONTEXT:
            return "Closed";
        case Reason::NONE:
        default:
            return "Blocked";
    }
}

FLASHMEM uint32_t mixRevision(uint32_t seed, uint32_t value) {
    return (seed ^ value) * 16777619U;
}

FLASHMEM uint32_t mixSignedRevision(uint32_t seed, int32_t value) {
    return mixRevision(seed, static_cast<uint32_t>(value + 32768));
}

FLASHMEM uint32_t mixResolvedStepRevision(
    uint32_t revision,
    const core::state::sequencer::SequencerResolvedStepDisplayState& resolved
) {
    revision = mixRevision(revision, resolved.valid ? 1U : 0U);
    revision = mixRevision(revision, resolved.enabled ? 1U : 0U);
    revision = mixRevision(revision, resolved.probabilityCycleActive ? 1U : 0U);
    revision = mixRevision(revision, resolved.note);
    revision = mixRevision(revision, resolved.velocity);
    revision = mixRevision(revision, resolved.gate);
    revision = mixSignedRevision(revision, resolved.nudge);
    revision = mixRevision(revision, resolved.probability);
    revision = mixRevision(revision, resolved.childPitchSummaryVisible ? 1U : 0U);
    revision = mixRevision(revision, resolved.childPitchSummaryNote);
    revision = mixRevision(revision, resolved.runtimeNodeId);
    revision = mixRevision(revision, resolved.variation.visible ? 1U : 0U);
    revision = mixRevision(revision, resolved.variation.rangeVisible ? 1U : 0U);
    revision = mixRevision(revision, resolved.variation.deltaVisible ? 1U : 0U);
    revision = mixRevision(revision, resolved.variation.resolved.resolved.note);
    revision = mixRevision(revision, resolved.variation.resolved.resolved.velocity);
    revision = mixRevision(revision, resolved.variation.resolved.resolved.gate);
    revision = mixSignedRevision(revision, resolved.variation.resolved.resolved.nudge);
    revision = mixRevision(revision, resolved.variation.resolved.ranges.pitchSemitones);
    revision = mixRevision(revision, resolved.variation.resolved.ranges.velocity);
    revision = mixRevision(revision, resolved.variation.resolved.ranges.gatePercent);
    revision = mixRevision(revision, resolved.variation.resolved.ranges.nudge);
    return revision;
}

FLASHMEM uint32_t buildStepEditDataRevision(
    const Source& source,
    const core::state::sequencer::SequencerResolvedStepDisplayState& resolved,
    bool localVariationMode,
    bool chordDetailMode,
    uint8_t step,
    uint8_t len
) {
    const auto& sequencer = source.sequencer;
    uint32_t revision = 2166136261U;
    revision = mixRevision(revision, sequencer.pattern.stepDataRevision.get());
    revision = mixRevision(revision, sequencer.pattern.graphRevision.get());
    revision = mixRevision(revision, sequencer.pattern.patternScaleRevision.get());
    revision = mixRevision(revision, source.tracks.projectScaleRevisionSignal().get());
    revision = mixResolvedStepRevision(revision, resolved);
    revision = mixRevision(revision, localVariationMode ? 1U : 0U);
    revision = mixRevision(revision, chordDetailMode ? 1U : 0U);
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(sequencer.stepEdit.chordEditor.focusedField.get())
    );
    revision = mixRevision(revision, static_cast<uint32_t>(step));
    revision = mixRevision(revision, static_cast<uint32_t>(len));
    return revision;
}

FLASHMEM ms::ui::KeyValueRow makeIconRow(
    const char* key,
    const char* value,
    const char* icon,
    uint32_t color
) {
    return {
        .key = key,
        .value = value,
        .icon = icon,
        .iconFont = standalone_fonts.icons_14,
        .iconColor = color,
    };
}

FLASHMEM bool focusedRowIsContextRow(const core::state::sequencer::SequencerState& sequencer) {
    return step_edit_rows::isContext(sequencer.stepEdit.focusedRow.get());
}

FLASHMEM bool focusedRowIsValueRow(const core::state::sequencer::SequencerState& sequencer) {
    const uint8_t row = sequencer.stepEdit.focusedRow.get();
    return step_edit_rows::isActivated(row) ||
           step_edit_rows::isProperty(row) ||
           step_edit_rows::isChord(row);
}

FLASHMEM core::state::sequencer::StepContentChildKind childKindForContextRow(size_t row) {
    return step_edit_rows::childKindForContextRow(static_cast<uint8_t>(row));
}

FLASHMEM core::state::SequencerStepContentClipboardKind clipboardKindForFocusedContextRow(
    const core::state::sequencer::SequencerState& sequencer
) {
    const auto row = static_cast<size_t>(sequencer.stepEdit.focusedRow.get());
    if (row == step_edit_rows::MICRO_SEQUENCE) {
        return core::state::SequencerStepContentClipboardKind::MICRO_SEQUENCE;
    }
    if (row == step_edit_rows::CYCLE_STATES) {
        return core::state::SequencerStepContentClipboardKind::CYCLE_STATES;
    }
    return core::state::SequencerStepContentClipboardKind::NONE;
}

FLASHMEM bool focusedContextHasChild(
    const core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerGraphNodeId nodeId
) {
    const auto row = static_cast<size_t>(sequencer.stepEdit.focusedRow.get());
    if (step_edit_rows::isContext(static_cast<uint8_t>(row))) {
        const auto childKind = childKindForContextRow(row);
        return childKind == core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE
            ? core::state::sequencer::stepNodeHasMicroSequence(sequencer.pattern, nodeId)
            : core::state::sequencer::stepNodeHasCycleStateSet(sequencer.pattern, nodeId);
    }
    return false;
}

FLASHMEM bool canPasteStepContent(const ActionSource& source) {
    return source.structureClipboard.hasSequencerStepContent(
               clipboardKindForFocusedContextRow(source.sequencer)
           ) &&
           core::state::sequencer::activeContentStepCanReceiveChildContent(
               source.sequencer,
               source.sequencer.stepEdit.stepIndex.get()
           );
}

FLASHMEM void copyText(char* out, size_t outSize, const char* text) {
    if (!out || outSize == 0) return;
    const char* source = text ? text : "";
    std::strncpy(out, source, outSize - 1);
    out[outSize - 1] = '\0';
}

FLASHMEM void formatNoteName(char* out, size_t outSize, uint8_t note) {
    core::state::sequencer::formatStepPropertyValue(
        out,
        outSize,
        core::state::sequencer::StepProperty::NOTE,
        note,
        0,
        0
    );
}

FLASHMEM void formatStepSummary(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerContentStepProjection& projection,
    const core::state::sequencer::SequencerChildContentSummary* childSummary
) {
    if (!out || outSize == 0) return;

    char from[8] = {};
    char to[8] = {};
    const bool childContextDiffers =
        !projection.rootContext && projection.parentNote != projection.note;
    const bool childNoteDiffers =
        childSummary != nullptr && childSummary->note != projection.note;

    if (childContextDiffers) {
        formatNoteName(from, sizeof(from), projection.parentNote);
        formatNoteName(to, sizeof(to), projection.note);
        std::snprintf(out, outSize, "%s>%s", from, to);
        return;
    }

    if (childNoteDiffers) {
        formatNoteName(from, sizeof(from), projection.note);
        formatNoteName(to, sizeof(to), childSummary->note);
        std::snprintf(out, outSize, "%s>%s", from, to);
        return;
    }

    formatNoteName(out, outSize, projection.note);
}

FLASHMEM void formatCompactOffset(
    char* out,
    size_t outSize,
    core::state::sequencer::StepProperty property,
    int16_t offset,
    bool noteOffsetUsesScaleDegrees
) {
    if (!out || outSize == 0) return;

    const char sign = offset >= 0 ? '+' : '-';
    const int magnitude = offset >= 0 ? offset : -offset;
    const char* unit = (property == core::state::sequencer::StepProperty::NOTE &&
                        noteOffsetUsesScaleDegrees)
                           ? "d"
                           : "";
    std::snprintf(out, outSize, "%c%d%s", sign, magnitude, unit);
}

FLASHMEM void formatLocalVariationRange(
    char* out,
    size_t outSize,
    core::state::sequencer::StepProperty property,
    uint8_t range,
    bool pitchUsesScaleDegrees
) {
    if (!out || outSize == 0) return;
    if (!core::state::sequencer::stepPropertySupportsLocalVariation(property)) {
        copyText(out, outSize, "--");
        return;
    }

    const char* unit = "";
    if (property == core::state::sequencer::StepProperty::GATE) {
        unit = "%";
    } else if (property == core::state::sequencer::StepProperty::NOTE &&
               pitchUsesScaleDegrees) {
        unit = "d";
    }
    std::snprintf(out, outSize, "±%u%s", static_cast<unsigned>(range), unit);
}

FLASHMEM const char* focusLabelForSelectedRow(int selectedIndex) {
    using namespace core::state::sequencer::step_edit_rows;

    if (selectedIndex == ACTIVATED) return "State";
    if (selectedIndex == CHORD) return "Chord";
    if (selectedIndex == MICRO_SEQUENCE) return "Micro sequence";
    if (selectedIndex == CYCLE_STATES) return "Cycle state";
    if (isProperty(static_cast<uint8_t>(std::max(0, selectedIndex)))) {
        const int index = selectedIndex - PROPERTY_OFFSET;
        if (index >= 0 && index < static_cast<int>(PROPERTIES.size())) {
            return core::ui::sequencer::semantic::labelForProperty(
                PROPERTIES[static_cast<size_t>(index)]
            );
        }
    }
    return "Step";
}

}  // namespace

FLASHMEM StepEditRenderData buildStepEditRenderData(const Source& source) {
    StepEditRenderData data{};
    auto& sequencer = source.sequencer;
    data.visible = sequencer.stepEdit.visible.get();
    if (!data.visible) {
        return data;
    }

    const uint8_t step = sequencer.stepEdit.stepIndex.get();
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer);
    if (step >= len) {
        data.visible = false;
        return data;
    }

    data.rowCount = static_cast<int>(StepEditRenderData::ROW_COUNT);
    data.stepIndex = step;
    data.selectedIndex = std::min<int>(
        sequencer.stepEdit.focusedRow.get(),
        data.rowCount - 1
    );

    size_t titlePos = oc::type::text::appendString(data.title.data(), data.title.size(), 0, "STEP ");
    titlePos = oc::type::text::appendUnsigned(
        data.title.data(),
        data.title.size(),
        titlePos,
        static_cast<unsigned>(step) + 1U
    );
    oc::type::text::terminate(data.title.data(), data.title.size(), titlePos);

    if (len > 0) {
        oc::type::text::formatFraction(
            data.meta.data(),
            data.meta.size(),
            static_cast<unsigned>(step) + 1U,
            static_cast<unsigned>(len)
        );
    } else {
        oc::type::text::formatUnsigned(data.meta.data(), data.meta.size(), static_cast<unsigned>(step) + 1U);
    }

    const bool selectedRowIsProperty =
        step_edit_rows::isProperty(static_cast<uint8_t>(data.selectedIndex));
    const auto selectedProperty = selectedRowIsProperty
        ? step_edit_rows::propertyForRow(static_cast<uint8_t>(data.selectedIndex))
        : sequencer.activeStepProperty.get();
    const auto displayContext =
        core::state::sequencer::makeSequencerResolvedDisplayProjectionContext(
            sequencer,
            source.tracks.projectScaleSettings(),
            selectedProperty
        );
    const auto touchedMask = sequencer.stepInlineFeedback.touchedMask.get();
    const bool stepInlineEditActive =
        sequencer.stepInlineFeedback.visible.get() && touchedMask.test(step);
    const auto resolved = core::state::sequencer::buildSequencerResolvedStepDisplayState(
        displayContext,
        step,
        stepInlineEditActive
    );
    if (!resolved.valid) {
        data.visible = false;
        return data;
    }

    const auto effectiveScaleSettings = displayContext.scaleSettings;
    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        step,
        effectiveScaleSettings
    );
    if (!projection.valid) {
        data.visible = false;
        return data;
    }

    const auto displayValues =
        core::state::sequencer::sequencerResolvedStepDisplayValues(resolved);

    const bool localVariationMode =
        sequencer.stepEdit.localVariationEditActive.get() &&
        selectedRowIsProperty &&
        core::state::sequencer::stepPropertySupportsLocalVariation(selectedProperty);

    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    const auto* node = graph ? graph->stepNode(nodeId) : nullptr;
    auto chordUi = core::state::sequencer::resolveStepChordUiState(sequencer, step);
    const bool chordDetailMode = sequencer.stepEdit.chordEditor.active.get();

    std::snprintf(
        data.stepBadge.data(),
        data.stepBadge.size(),
        "S%u",
        static_cast<unsigned>(step) + 1U
    );

    if (chordDetailMode) {
        core::state::sequencer::resolveStepChordPreview(
            chordUi,
            projection,
            effectiveScaleSettings
        );
        populateChordDetailOverlay(
            data,
            chordUi,
            sequencer.stepEdit.chordEditor.focusedField.get(),
            projection.enabled
        );

        const uint32_t revision = buildStepEditDataRevision(
            source,
            resolved,
            false,
            true,
            step,
            len
        );
        data.dataRevision = revision;
        data.overlayProps.dataRevision = revision;
        return data;
    }

    core::state::sequencer::SequencerChildContentSummary childSummary{};
    bool hasChildSummary = false;
    if (projection.hasMicroSequence || projection.hasCycleStates) {
        hasChildSummary = core::state::sequencer::resolveRepresentativeChildContentSummary(
            sequencer,
            projection,
            effectiveScaleSettings,
            childSummary
        );
    }

    std::snprintf(
        data.valueBuffers[step_edit_rows::ACTIVATED].data(),
        data.valueBuffers[step_edit_rows::ACTIVATED].size(),
        "%s",
        projection.enabled ? "On" : "Off"
    );
    data.rows[step_edit_rows::ACTIVATED] = makeIconRow(
        "State",
        data.valueBuffers[step_edit_rows::ACTIVATED].data(),
        ::standalone::icons::ACTION_VALIDATE,
        ACTIVATED_ICON_COLOR
    );
    data.overlayProps.state = core::ui::SequencerStepEditPropertyChip{
        .key = core::ui::sequencer::semantic::label(
            core::ui::sequencer::semantic::Tone::STATE
        ),
        .value = data.valueBuffers[step_edit_rows::ACTIVATED].data(),
        .icon = ::standalone::icons::ACTION_VALIDATE,
        .color = ACTIVATED_ICON_COLOR,
    };

    for (size_t i = 0; i < step_edit_rows::PROPERTIES.size(); ++i) {
        const size_t rowIndex = step_edit_rows::PROPERTY_OFFSET + i;
        const auto property = step_edit_rows::PROPERTIES[i];
        if (localVariationMode) {
            const uint8_t range = node
                ? core::state::sequencer::nodeLocalVariationRange(*node, property)
                : 0;
            formatLocalVariationRange(
                data.valueBuffers[rowIndex].data(),
                data.valueBuffers[rowIndex].size(),
                property,
                range,
                effectiveScaleSettings.isConstrained()
            );
            copyText(
                data.compactValueBuffers[i].data(),
                data.compactValueBuffers[i].size(),
                data.valueBuffers[rowIndex].data()
            );
        } else if (projection.rootContext) {
            core::state::sequencer::formatStepPropertyValue(
                data.valueBuffers[rowIndex].data(),
                data.valueBuffers[rowIndex].size(),
                property,
                displayValues.note,
                displayValues.velocity,
                displayValues.gate,
                displayValues.nudge,
                resolved.probability
            );
            copyText(
                data.compactValueBuffers[i].data(),
                data.compactValueBuffers[i].size(),
                data.valueBuffers[rowIndex].data()
            );
        } else {
            core::state::sequencer::formatStepPropertyResolvedOffsetValue(
                data.valueBuffers[rowIndex].data(),
                data.valueBuffers[rowIndex].size(),
                property,
                projection.note,
                projection.velocity,
                projection.gate,
                projection.nudge,
                projection.probability,
                core::state::sequencer::stepContentProjectionOffsetForProperty(
                    projection,
                    step_edit_rows::PROPERTIES[i]
                ),
                effectiveScaleSettings.isConstrained()
            );
            formatCompactOffset(
                data.compactValueBuffers[i].data(),
                data.compactValueBuffers[i].size(),
                property,
                core::state::sequencer::stepContentProjectionOffsetForProperty(
                    projection,
                    property
                ),
                property == core::state::sequencer::StepProperty::NOTE &&
                    effectiveScaleSettings.isConstrained()
            );
        }
        data.rows[rowIndex] = makeIconRow(
            step_edit_rows::KEYS[i],
            data.valueBuffers[rowIndex].data(),
            core::ui::sequencer::visual::propertyIconGlyph(property),
            core::ui::sequencer::semantic::colorForProperty(property)
        );
        data.overlayProps.properties[i] = core::ui::SequencerStepEditPropertyChip{
            .key = core::ui::sequencer::semantic::labelForProperty(property),
            .value = data.compactValueBuffers[i].data(),
            .icon = core::ui::sequencer::visual::propertyIconGlyph(property),
            .color = core::ui::sequencer::semantic::colorForProperty(property),
        };
    }

    formatChordValue(
        data.valueBuffers[step_edit_rows::CHORD].data(),
        data.valueBuffers[step_edit_rows::CHORD].size(),
        chordUi
    );
    data.rows[step_edit_rows::CHORD] = makeIconRow(
        "Chord",
        data.valueBuffers[step_edit_rows::CHORD].data(),
        ::standalone::icons::CHORD,
        chordColor()
    );

    const auto microAvailability = core::state::sequencer::activeContentChildCreationAvailability(
        sequencer,
        step,
        core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
        core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    const auto cycleAvailability = core::state::sequencer::activeContentChildCreationAvailability(
        sequencer,
        step,
        core::state::sequencer::StepContentChildKind::CYCLE_STATES,
        core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT
    );

    std::snprintf(
        data.valueBuffers[step_edit_rows::MICRO_SEQUENCE].data(),
        data.valueBuffers[step_edit_rows::MICRO_SEQUENCE].size(),
        "%s",
        availabilityLabel(microAvailability)
    );
    data.rows[step_edit_rows::MICRO_SEQUENCE] = makeIconRow(
        "Micro sequence",
        data.valueBuffers[step_edit_rows::MICRO_SEQUENCE].data(),
        ::standalone::icons::MICRO_SEQUENCE,
        MICRO_SEQUENCE_COLOR
    );

    std::snprintf(
        data.valueBuffers[step_edit_rows::CYCLE_STATES].data(),
        data.valueBuffers[step_edit_rows::CYCLE_STATES].size(),
        "%s",
        availabilityLabel(cycleAvailability)
    );
    data.rows[step_edit_rows::CYCLE_STATES] = makeIconRow(
        "Cycle state",
        data.valueBuffers[step_edit_rows::CYCLE_STATES].data(),
        ::standalone::icons::CYCLE_STATE,
        CYCLE_STATE_COLOR
    );

    formatStepSummary(
        data.summary.data(),
        data.summary.size(),
        projection,
        hasChildSummary ? &childSummary : nullptr
    );

    if (localVariationMode) {
        copyText(
            data.focusLabel.data(),
            data.focusLabel.size(),
            core::ui::sequencer::semantic::labelForProperty(selectedProperty)
        );
    } else {
        copyText(
            data.focusLabel.data(),
            data.focusLabel.size(),
            focusLabelForSelectedRow(data.selectedIndex)
        );
    }

    data.overlayProps.visible = true;
    data.overlayProps.stepBadge = data.stepBadge.data();
    data.overlayProps.title = data.summary.data();
    data.overlayProps.meta = data.meta.data();
    data.overlayProps.focusLabel = data.focusLabel.data();
    data.overlayProps.enabled = resolved.enabled;
    data.overlayProps.microSequence = projection.hasMicroSequence;
    data.overlayProps.cycleStates = projection.hasCycleStates;
    data.overlayProps.probabilityActive = resolved.probability < 100;
    data.overlayProps.selectedIndex = data.selectedIndex;
    data.overlayProps.actions[0] = core::ui::SequencerStepEditActionChip{
        .key = "Chord",
        .value = data.valueBuffers[step_edit_rows::CHORD].data(),
        .icon = ::standalone::icons::CHORD,
        .color = chordColor(),
    };
    data.overlayProps.actions[1] = core::ui::SequencerStepEditActionChip{
        .key = "Micro sequence",
        .value = data.valueBuffers[step_edit_rows::MICRO_SEQUENCE].data(),
        .icon = ::standalone::icons::MICRO_SEQUENCE,
        .color = MICRO_SEQUENCE_COLOR,
    };
    data.overlayProps.actions[2] = core::ui::SequencerStepEditActionChip{
        .key = "Cycle state",
        .value = data.valueBuffers[step_edit_rows::CYCLE_STATES].data(),
        .icon = ::standalone::icons::CYCLE_STATE,
        .color = CYCLE_STATE_COLOR,
    };

    const uint32_t revision = buildStepEditDataRevision(
        source,
        resolved,
        localVariationMode,
        false,
        step,
        len
    );
    data.dataRevision = revision;
    data.overlayProps.dataRevision = revision;
    return data;
}

FLASHMEM core::ui::ContextActionStripProps buildStepEditActionStripProps(const ActionSource& source) {
    StripProps props;
    using Action = core::state::sequencer::SequencerInteractionAction;

    auto& sequencer = source.sequencer;
    if (!sequencer.stepEdit.visible.get()) {
        props.visible = false;
        return props;
    }

    const uint8_t step = sequencer.stepEdit.stepIndex.get();
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer);
    if (step >= len) {
        props.visible = false;
        return props;
    }

    if (focusedRowIsValueRow(sequencer)) {
        constexpr auto resetAction = Action::RESET_STEP_EDITOR_ROW;
        props.visible = true;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            core::ui::sequencer::interactionActionIcon(resetAction),
            Visual::ACTIVE,
            Tone::WARNING
        );
        props.slots[1] = stepPresetStripSlot();
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }

    if (!focusedRowIsContextRow(sequencer)) {
        props.visible = true;
        props.slots[0].visualState = Visual::HIDDEN;
        props.slots[1] = stepPresetStripSlot();
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }

    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
    const bool hasChild = focusedContextHasChild(sequencer, nodeId);
    const bool canPaste = canPasteStepContent(source);
    const auto row = static_cast<size_t>(sequencer.stepEdit.focusedRow.get());
    const Tone contextTone = row == step_edit_rows::MICRO_SEQUENCE
        ? Tone::CONSTRUCTIVE
        : Tone::WARNING;
    const auto holdAction = sequencer.stepEdit.contextHold.action.get();
    const bool removeHoldActive = holdAction == core::state::StructureHoldAction::REMOVE;
    const bool pasteHoldActive = holdAction == core::state::StructureHoldAction::PASTE;
    constexpr auto removeAction = Action::REMOVE_STEP_EDITOR_CONTEXT;
    const auto rightAction =
        canPaste ? Action::PASTE_STEP_EDITOR_CONTEXT : Action::COPY_STEP_EDITOR_CONTEXT;

    props.visible = true;
    props.slots[0] = core::ui::makeStandaloneIconStripSlot(
        core::ui::sequencer::interactionActionIcon(removeAction),
        removeHoldActive ? Visual::ARMED : (hasChild ? Visual::ACTIVE : Visual::DISABLED),
        removeHoldActive ? Tone::DESTRUCTIVE : Tone::WARNING
    );
    props.slots[1] = stepPresetStripSlot();
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        core::ui::sequencer::interactionActionIcon(rightAction),
        pasteHoldActive && canPaste
            ? Visual::ARMED
            : ((hasChild || canPaste) ? Visual::ACTIVE : Visual::DISABLED),
        pasteHoldActive && canPaste ? Tone::POSITIVE : (canPaste ? Tone::POSITIVE : contextTone)
    );
    props.slots[0].holdActive = removeHoldActive;
    props.slots[0].holdStartedAtMs = sequencer.stepEdit.contextHold.startedAtMs.get();
    props.slots[0].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    props.slots[2].holdActive = pasteHoldActive && canPaste;
    props.slots[2].holdStartedAtMs = sequencer.stepEdit.contextHold.startedAtMs.get();
    props.slots[2].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    return props;
}

FLASHMEM const char* stepPresetFeedbackLabel(
    core::state::sequencer::SequencerStepPresetFeedback feedback
) {
    using Feedback = core::state::sequencer::SequencerStepPresetFeedback;
    switch (feedback) {
        case Feedback::SAVED:
            return "Saved";
        case Feedback::EMPTY:
            return "No preset";
        case Feedback::INCOMPATIBLE:
            return "Incompatible";
        case Feedback::FAILED:
            return "Failed";
        case Feedback::NONE:
        default:
            return "";
    }
}

FLASHMEM StepPresetPickerRenderData buildStepPresetPickerRenderData(
    const Source& source
) {
    StepPresetPickerRenderData data{};
    const auto& picker = source.sequencer.stepPresetPicker;
    if (!picker.visible.get()) {
        return data;
    }

    using Mode = core::state::sequencer::SequencerStepPresetPickerMode;
    const bool saveMode = picker.mode.get() == Mode::SAVE;
    data.visible = true;
    data.title = saveMode ? "Save Step Preset" : "Load Step Preset";

    int itemIndex = 0;
    if (saveMode) {
        std::snprintf(
            data.itemBuffers[itemIndex].data(),
            data.itemBuffers[itemIndex].size(),
            "New Step Preset"
        );
        data.items[itemIndex] = data.itemBuffers[itemIndex].data();
        ++itemIndex;
    }

    const uint8_t entryCount = picker.entryCount.get();
    for (uint8_t i = 0; i < entryCount && itemIndex < static_cast<int>(data.items.size()); ++i) {
        std::snprintf(
            data.itemBuffers[itemIndex].data(),
            data.itemBuffers[itemIndex].size(),
            "%s",
            picker.entryId(i)
        );
        data.items[itemIndex] = data.itemBuffers[itemIndex].data();
        ++itemIndex;
    }

    if (itemIndex == 0) {
        std::snprintf(
            data.itemBuffers[0].data(),
            data.itemBuffers[0].size(),
            "No Step Presets"
        );
        data.items[0] = data.itemBuffers[0].data();
        itemIndex = 1;
    }
    data.itemCount = itemIndex;
    data.selectedIndex = std::clamp<int>(picker.selectedIndex.get(), 0, itemIndex - 1);

    const char* feedback = stepPresetFeedbackLabel(picker.feedback.get());
    if (feedback[0] != '\0') {
        std::snprintf(data.meta.data(), data.meta.size(), "%s", feedback);
    } else if (picker.truncated.get()) {
        std::snprintf(data.meta.data(), data.meta.size(), "More on SD");
    } else {
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "Step %02u",
            static_cast<unsigned>(source.sequencer.stepEdit.stepIndex.get() + 1U)
        );
    }

    uint32_t revision = picker.revision.get();
    revision = mixRevision(revision, picker.visible.get() ? 1U : 0U);
    revision = mixRevision(revision, static_cast<uint32_t>(picker.mode.get()));
    revision = mixRevision(revision, picker.selectedIndex.get());
    revision = mixRevision(revision, picker.entryCount.get());
    revision = mixRevision(revision, picker.truncated.get() ? 1U : 0U);
    revision = mixRevision(revision, static_cast<uint32_t>(picker.feedback.get()));
    data.dataRevision = revision;
    return data;
}

FLASHMEM core::ui::ContextActionStripProps buildStepPresetActionStripProps(
    const Source& source
) {
    StripProps props{};
    const auto& picker = source.sequencer.stepPresetPicker;
    if (!picker.visible.get()) {
        props.visible = false;
        return props;
    }

    using Mode = core::state::sequencer::SequencerStepPresetPickerMode;
    const bool saveMode = picker.mode.get() == Mode::SAVE;
    const bool canLoad = picker.entryCount.get() > 0;

    props.visible = true;
    props.slots[0] = core::ui::makeStandaloneIconStripSlot(
        ::standalone::icons::ACTION_CANCEL,
        Visual::ACTIVE,
        Tone::NEUTRAL
    );
    props.slots[1] = core::ui::makeStandaloneIconStripSlot(
        ::standalone::icons::STORAGE,
        saveMode ? Visual::ARMED : Visual::ACTIVE,
        Tone::CONSTRUCTIVE
    );
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        ::standalone::icons::ACTION_VALIDATE,
        (saveMode || canLoad) ? Visual::ACTIVE : Visual::DISABLED,
        Tone::POSITIVE
    );
    return props;
}

}  // namespace core::context::standalone::sequencer_overlay_presenter

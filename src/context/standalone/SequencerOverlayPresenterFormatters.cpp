#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <oc/type/TextFormat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"
#include "ui/font/StandaloneIcons.hpp"
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
    return step_edit_rows::isActivated(row) || step_edit_rows::isProperty(row);
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
    const core::state::sequencer::SequencerContentStepProjection& projection
) {
    const auto row = static_cast<size_t>(sequencer.stepEdit.focusedRow.get());
    if (step_edit_rows::isContext(static_cast<uint8_t>(row))) {
        return core::state::sequencer::stepContentProjectionHasChild(
            projection,
            childKindForContextRow(row)
        );
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

FLASHMEM bool propertySupportsLocalVariation(core::state::sequencer::StepProperty property) {
    return property != core::state::sequencer::StepProperty::PROBABILITY;
}

FLASHMEM void formatLocalVariationRange(
    char* out,
    size_t outSize,
    core::state::sequencer::StepProperty property,
    uint8_t range,
    bool pitchUsesScaleDegrees
) {
    if (!out || outSize == 0) return;
    if (!propertySupportsLocalVariation(property)) {
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

    const auto effectiveScaleSettings = core::state::sequencer::resolveEffectiveScaleSettings(
        source.tracks.projectScaleSettings(),
        sequencer.pattern.scalePolicy,
        sequencer.pattern.scaleOverride
    );
    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        step,
        effectiveScaleSettings
    );
    if (!projection.valid) {
        data.visible = false;
        return data;
    }

    const bool selectedRowIsProperty =
        step_edit_rows::isProperty(static_cast<uint8_t>(data.selectedIndex));
    const auto selectedProperty = selectedRowIsProperty
        ? step_edit_rows::propertyForRow(static_cast<uint8_t>(data.selectedIndex))
        : core::state::sequencer::StepProperty::NOTE;
    const bool localVariationMode =
        sequencer.stepEdit.localVariationEditActive.get() &&
        selectedRowIsProperty &&
        propertySupportsLocalVariation(selectedProperty);

    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    const auto* node = graph ? graph->stepNode(nodeId) : nullptr;

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
                projection.note,
                projection.velocity,
                projection.gate,
                projection.nudge,
                projection.probability
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

    std::snprintf(
        data.stepBadge.data(),
        data.stepBadge.size(),
        "S%u",
        static_cast<unsigned>(step) + 1U
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
    data.overlayProps.enabled = projection.enabled;
    data.overlayProps.microSequence = projection.hasMicroSequence;
    data.overlayProps.cycleStates = projection.hasCycleStates;
    data.overlayProps.probabilityActive = projection.probability < 100;
    data.overlayProps.selectedIndex = data.selectedIndex;
    data.overlayProps.actions[0] = core::ui::SequencerStepEditActionChip{
        .key = "Micro sequence",
        .value = data.valueBuffers[step_edit_rows::MICRO_SEQUENCE].data(),
        .icon = ::standalone::icons::MICRO_SEQUENCE,
        .color = MICRO_SEQUENCE_COLOR,
    };
    data.overlayProps.actions[1] = core::ui::SequencerStepEditActionChip{
        .key = "Cycle state",
        .value = data.valueBuffers[step_edit_rows::CYCLE_STATES].data(),
        .icon = ::standalone::icons::CYCLE_STATE,
        .color = CYCLE_STATE_COLOR,
    };

    uint32_t revision = 2166136261U;
    revision = mixRevision(revision, sequencer.pattern.stepDataRevision.get());
    revision = mixRevision(revision, sequencer.pattern.graphRevision.get());
    revision = mixRevision(revision, projection.enabled ? 1U : 0U);
    revision = mixRevision(revision, localVariationMode ? 1U : 0U);
    revision = mixRevision(revision, static_cast<uint32_t>(step));
    revision = mixRevision(revision, static_cast<uint32_t>(len));
    data.dataRevision = revision;
    data.overlayProps.dataRevision = revision;
    return data;
}

FLASHMEM core::ui::ContextActionStripProps buildStepEditActionStripProps(const ActionSource& source) {
    StripProps props;

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

    const auto effectiveScaleSettings = core::state::sequencer::resolveEffectiveScaleSettings(
        source.tracks.projectScaleSettings(),
        sequencer.pattern.scalePolicy,
        sequencer.pattern.scaleOverride
    );
    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        step,
        effectiveScaleSettings
    );
    if (!projection.valid) {
        props.visible = false;
        return props;
    }

    if (focusedRowIsValueRow(sequencer)) {
        props.visible = true;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            ::standalone::icons::ACTION_CLEAR,
            Visual::ACTIVE,
            Tone::WARNING
        );
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }

    if (!focusedRowIsContextRow(sequencer)) {
        props.visible = false;
        return props;
    }

    const bool hasChild = focusedContextHasChild(sequencer, projection);
    const bool canPaste = canPasteStepContent(source);
    const auto row = static_cast<size_t>(sequencer.stepEdit.focusedRow.get());
    const Tone contextTone = row == step_edit_rows::MICRO_SEQUENCE
        ? Tone::CONSTRUCTIVE
        : Tone::WARNING;
    const auto holdAction = sequencer.stepEdit.contextHold.action.get();
    const bool removeHoldActive = holdAction == core::state::StructureHoldAction::REMOVE;
    const bool pasteHoldActive = holdAction == core::state::StructureHoldAction::PASTE;

    props.visible = true;
    props.slots[0] = core::ui::makeStandaloneIconStripSlot(
        ::standalone::icons::ACTION_CLEAR,
        removeHoldActive ? Visual::ARMED : (hasChild ? Visual::ACTIVE : Visual::DISABLED),
        removeHoldActive ? Tone::DESTRUCTIVE : Tone::WARNING
    );
    props.slots[1].visualState = Visual::HIDDEN;
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        canPaste ? ::standalone::icons::ACTION_PASTE : ::standalone::icons::ACTION_COPY,
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

}  // namespace core::context::standalone::sequencer_overlay_presenter

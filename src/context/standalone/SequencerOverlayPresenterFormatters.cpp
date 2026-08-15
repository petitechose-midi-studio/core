#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <oc/type/TextFormat.hpp>

#include "context/standalone/SequencerChordOverlayFormatters.hpp"
#include "midi/MidiUtils.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerChordUiOps.hpp"
#include "state/sequencer/SequencerExpansionBudgetProjection.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerPresetLibraryEntryPolicy.hpp"
#include "state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"
#include "state/sequencer/DrumPatternState.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/SequencerActionStripVisuals.hpp"
#include "ui/sequencer/SequencerStepContentDraftTransitionLabels.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

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

struct DrumScalarRow {
    core::state::sequencer::StepProperty property;
    size_t propertyIndex;
    uint8_t row;
};

// The retained overlay is a cold presentation path. Keep its descriptors and
// format strings in flash so enabling Drum UX does not consume scarce RAM1.
constexpr DrumScalarRow DRUM_SCALAR_ROWS[] PROGMEM = {
    {core::state::sequencer::StepProperty::VELOCITY, 1U,
     static_cast<uint8_t>(step_edit_rows::PROPERTY_OFFSET + 1U)},
    {core::state::sequencer::StepProperty::GATE, 2U,
     static_cast<uint8_t>(step_edit_rows::PROPERTY_OFFSET + 2U)},
    {core::state::sequencer::StepProperty::NUDGE, 3U,
     static_cast<uint8_t>(step_edit_rows::PROPERTY_OFFSET + 3U)},
    {core::state::sequencer::StepProperty::PROBABILITY, 4U,
     static_cast<uint8_t>(step_edit_rows::PROPERTY_OFFSET + 4U)},
};
const char kDrumLaneBadgeFormat[] PROGMEM = "L%u";
const char kDrumChildMetaFormat[] PROGMEM = "S%u · %u/%u";
const char kDrumStateKey[] PROGMEM = "State";
const char kDrumLaneKey[] PROGMEM = "Lane";
const char kDrumOnValue[] PROGMEM = "On";
const char kDrumOffValue[] PROGMEM = "Off";
const char kDrumCustomTimingSuffix[] PROGMEM = "*";
const char kDrumEmptySuffix[] PROGMEM = "";

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

FLASHMEM const char* draftFailureLabel(
    const core::state::sequencer::SequencerStepContentDraftSession& draft
) {
    using Failure = core::state::sequencer::SequencerStepContentDraftFailure;
    switch (draft.failure) {
        case Failure::OUT_OF_MEMORY: return "APPLY FAILED · OUT OF MEMORY";
        case Failure::HISTORY_UNAVAILABLE: return "APPLY FAILED · HISTORY FULL";
        case Failure::PUBLISH_FAILED: return "APPLY FAILED · PUBLISH";
        case Failure::UNPUBLISHABLE_MUTATION: return "APPLY FAILED · INVALID EDIT";
        case Failure::TRANSITION_BLOCKED:
            return core::ui::sequencer::standaloneStepContentDraftTransitionLabel(
                draft.blockedTransition
            );
        case Failure::NONE:
        default: return nullptr;
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
    const auto& pattern = core::state::sequencer::authoringPattern(sequencer);
    uint32_t revision = 2166136261U;
    revision = mixRevision(revision, pattern.stepDataRevision.get());
    revision = mixRevision(revision, pattern.graphRevision.get());
    revision = mixRevision(revision, pattern.patternScaleRevision.get());
    revision = mixRevision(revision, sequencer.stepContentDraft.revision.get());
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(sequencer.stepContentDraft.exitChoice.get())
    );
    revision = mixRevision(revision, source.tracks.projectScaleRevisionSignal().get());
    revision = mixResolvedStepRevision(revision, resolved);
    revision = mixRevision(revision, localVariationMode ? 1U : 0U);
    revision = mixRevision(revision, chordDetailMode ? 1U : 0U);
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(sequencer.stepEdit.chordEditor.focusedField.get())
    );
    const auto& chordSubEditor =
        sequencer.stepEdit.chordEditor.subEditor.get();
    revision = mixRevision(
        revision,
        chordSubEditor.formulaEditorActive ? 1U : 0U
    );
    revision = mixRevision(
        revision,
        chordSubEditor.focusedFormulaItem
    );
    revision = mixRevision(
        revision,
        chordSubEditor.sourceSelectorActive ? 1U : 0U
    );
    revision = mixRevision(
        revision,
        static_cast<uint32_t>(chordSubEditor.focusedSourceChoice)
    );
    revision = mixRevision(revision, static_cast<uint32_t>(step));
    revision = mixRevision(revision, static_cast<uint32_t>(len));
    return revision;
}

FLASHMEM StepEditKeyValueRow makeIconRow(
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
           step_edit_rows::isProperty(row);
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
            ? core::state::sequencer::stepNodeHasMicroSequence(
                  core::state::sequencer::authoringPattern(sequencer),
                  nodeId
              )
            : core::state::sequencer::stepNodeHasCycleStateSet(
                  core::state::sequencer::authoringPattern(sequencer),
                  nodeId
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

FLASHMEM void buildDrumStepEditRenderData(
    const Source& source,
    StepEditRenderData& data
) {
    auto& sequencer = source.sequencer;
    auto& edit = sequencer.stepEdit;
    auto& drumUi = sequencer.drumSequencer;
    data.visible = edit.visible.get() && edit.drumContext;
    if (!data.visible) return;

    const uint8_t lane = edit.drumLane;
    const uint8_t drumStep = edit.drumStep;
    const bool childContext =
        core::state::sequencer::isChildContentView(sequencer);
    const uint8_t editedStep = edit.stepIndex.get();
    if (!drumUi.stepInRange(lane, drumStep) ||
        (childContext &&
         editedStep >= core::state::sequencer::activeContentLength(sequencer))) {
        data.visible = false;
        return;
    }

    const auto& descriptor = drumUi.drumTrack->kit.lanes[lane];
    const auto& lanePattern = drumUi.drumTrack->pattern.lanes[lane];
    const uint8_t length = drumUi.drumTrack->pattern.effectiveLength(lane);
    const uint8_t stepsPerBeat =
        drumUi.drumTrack->pattern.effectiveStepsPerBeat(lane);
    const auto displayContext =
        core::state::sequencer::makeSequencerResolvedDisplayProjectionContext(
            sequencer,
            source.tracks.projectScaleSettings(),
            core::state::sequencer::StepProperty::VELOCITY
        );
    core::state::sequencer::SequencerContentStepProjection projection{};
    if (childContext) {
        projection = core::state::sequencer::resolveActiveContentStepProjection(
            sequencer,
            editedStep,
            displayContext.scaleSettings
        );
        if (!projection.valid) {
            data.visible = false;
            return;
        }
    }
    const bool enabled = childContext
        ? projection.enabled
        : drumUi.drumTrack->pattern.stepEnabled(lane, drumStep);

    data.rowCount = static_cast<int>(StepEditRenderData::ROW_COUNT);
    data.stepIndex = editedStep;
    data.selectedIndex = edit.focusedRow.get();
    std::snprintf(
        data.stepBadge.data(),
        data.stepBadge.size(),
        kDrumLaneBadgeFormat,
        static_cast<unsigned>(lane) + 1U
    );
    std::array<char, 8> laneNoteName{};
    core::midi::formatNoteName(
        laneNoteName.data(), laneNoteName.size(), descriptor.midiNote
    );
    const uint32_t laneColor = ::standalone::theme::color::trackColor(
        core::state::sequencer::drumLaneDisplayColorIndex(descriptor)
    );
    if (childContext) {
        const char* childKind = core::state::sequencer::
                isMicroSequenceContentView(sequencer)
            ? "M"
            : "C";
        std::snprintf(
            data.summary.data(),
            data.summary.size(),
            "%s",
            core::state::sequencer::drumLaneDisplayName(descriptor)
        );
        std::snprintf(
            data.headerContext.data(),
            data.headerContext.size(),
            "%s%u",
            childKind,
            static_cast<unsigned>(editedStep) + 1U
        );
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            kDrumChildMetaFormat,
            static_cast<unsigned>(drumStep) + 1U,
            static_cast<unsigned>(editedStep) + 1U,
            static_cast<unsigned>(
                core::state::sequencer::activeContentLength(sequencer)
            )
        );
    } else {
        std::snprintf(
            data.summary.data(),
            data.summary.size(),
            "%s",
            core::state::sequencer::drumLaneDisplayName(descriptor)
        );
        std::snprintf(
            data.headerContext.data(),
            data.headerContext.size(),
            "S%u",
            static_cast<unsigned>(drumStep) + 1U
        );
        std::snprintf(
            data.headerMetricValues[0].data(),
            data.headerMetricValues[0].size(),
            "%u",
            static_cast<unsigned>(length)
        );
        std::snprintf(
            data.headerMetricValues[1].data(),
            data.headerMetricValues[1].size(),
            "1/%u%s",
            static_cast<unsigned>(stepsPerBeat * 4U),
            lanePattern.timing.mode ==
                    core::state::sequencer::DrumLaneTimingMode::CUSTOM
                ? kDrumCustomTimingSuffix
                : kDrumEmptySuffix
        );
    }
    copyText(
        data.focusLabel.data(),
        data.focusLabel.size(),
        focusLabelForSelectedRow(data.selectedIndex)
    );

    std::snprintf(
        data.valueBuffers[step_edit_rows::ACTIVATED].data(),
        data.valueBuffers[step_edit_rows::ACTIVATED].size(),
        "%s",
        enabled ? kDrumOnValue : kDrumOffValue
    );
    data.overlayProps.state = core::ui::SequencerStepEditPropertyChip{
        .key = kDrumStateKey,
        .value = data.valueBuffers[step_edit_rows::ACTIVATED].data(),
        .icon = ::standalone::icons::ACTION_VALIDATE,
        .color = ACTIVATED_ICON_COLOR,
    };

    copyText(
        data.compactValueBuffers[0].data(),
        data.compactValueBuffers[0].size(),
        laneNoteName.data()
    );
    data.overlayProps.properties[0] = core::ui::SequencerStepEditPropertyChip{
        .key = kDrumLaneKey,
        .value = data.compactValueBuffers[0].data(),
        .icon = ::standalone::icons::NOTE,
        .color = laneColor,
        .valueColor = laneColor,
        .active = true,
    };
    for (const auto& scalar : DRUM_SCALAR_ROWS) {
        const bool localVariationMode = childContext &&
            edit.localVariationEditActive.get() &&
            data.selectedIndex == scalar.row;
        const auto* graph = core::state::sequencer::graphView(
            core::state::sequencer::authoringPattern(sequencer)
        );
        const auto nodeId = childContext
            ? core::state::sequencer::activeContentStepNodeId(
                  sequencer, editedStep
              )
            : oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
        const auto* node = graph == nullptr ? nullptr : graph->stepNode(nodeId);
        if (localVariationMode) {
            const uint8_t range = node == nullptr
                ? 0U
                : core::state::sequencer::nodeLocalVariationRange(
                      *node, scalar.property
                  );
            formatLocalVariationRange(
                data.valueBuffers[scalar.row].data(),
                data.valueBuffers[scalar.row].size(),
                scalar.property,
                range,
                false
            );
            copyText(
                data.compactValueBuffers[scalar.propertyIndex].data(),
                data.compactValueBuffers[scalar.propertyIndex].size(),
                data.valueBuffers[scalar.row].data()
            );
        } else if (childContext) {
            core::state::sequencer::formatStepPropertyResolvedOffsetValue(
                data.valueBuffers[scalar.row].data(),
                data.valueBuffers[scalar.row].size(),
                scalar.property,
                projection.note,
                projection.velocity,
                projection.gate,
                projection.nudge,
                projection.probability,
                core::state::sequencer::stepContentProjectionOffsetForProperty(
                    projection, scalar.property
                ),
                false
            );
            formatCompactOffset(
                data.compactValueBuffers[scalar.propertyIndex].data(),
                data.compactValueBuffers[scalar.propertyIndex].size(),
                scalar.property,
                core::state::sequencer::stepContentProjectionOffsetForProperty(
                    projection, scalar.property
                ),
                false
            );
        } else {
            core::state::sequencer::formatStepPropertyValue(
                data.valueBuffers[scalar.row].data(),
                data.valueBuffers[scalar.row].size(),
                scalar.property,
                descriptor.midiNote,
                lanePattern.velocity[drumStep],
                lanePattern.gate[drumStep],
                lanePattern.nudge[drumStep],
                lanePattern.probability[drumStep]
            );
            copyText(
                data.compactValueBuffers[scalar.propertyIndex].data(),
                data.compactValueBuffers[scalar.propertyIndex].size(),
                data.valueBuffers[scalar.row].data()
            );
        }
        data.overlayProps.properties[scalar.propertyIndex] =
            core::ui::SequencerStepEditPropertyChip{
                .key = core::ui::sequencer::semantic::labelForProperty(
                    scalar.property
                ),
                .value = data.compactValueBuffers[scalar.propertyIndex].data(),
                .icon = core::ui::sequencer::visual::propertyIconGlyph(
                    scalar.property
                ),
                .color = core::ui::sequencer::semantic::colorForProperty(
                    scalar.property
                ),
            };
    }

    core::state::sequencer::SequencerGraphNodeId nodeId =
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    if (childContext) {
        nodeId = core::state::sequencer::activeContentStepNodeId(
            sequencer, editedStep
        );
    } else {
        const int16_t slot = drumUi.drumTrack->advancedRootSlot(lane, drumStep);
        if (slot >= 0) {
            nodeId = core::state::sequencer::rootStepNodeId(
                static_cast<uint8_t>(slot)
            );
        }
    }
    const auto& pattern = core::state::sequencer::authoringPattern(sequencer);
    const bool hasMicro = nodeId !=
            oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID &&
        core::state::sequencer::stepNodeHasMicroSequence(pattern, nodeId);
    const bool hasCycle = nodeId !=
            oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID &&
        core::state::sequencer::stepNodeHasCycleStateSet(pattern, nodeId);
    uint32_t revision = mixRevision(2166136261U, drumUi.revision.get());
    revision = mixRevision(revision, lane);
    revision = mixRevision(revision, drumStep);
    revision = mixRevision(revision, editedStep);
    revision = mixRevision(revision, edit.focusedRow.get());
    revision = mixRevision(revision, pattern.graphRevision.get());
    revision = mixRevision(revision, sequencer.contentView.revision.get());
    data.dataRevision = revision;
    data.overlayProps.visible = true;
    data.overlayProps.stepBadge = data.stepBadge.data();
    data.overlayProps.title = data.summary.data();
    data.overlayProps.context = data.headerContext.data();
    data.overlayProps.meta = data.meta.data();
    if (!childContext) {
        data.overlayProps.headerMetrics[0] = {
            .icon = ::standalone::icons::LENGTH,
            .value = data.headerMetricValues[0].data(),
        };
        data.overlayProps.headerMetrics[1] = {
            .icon = ::standalone::icons::DIVISION,
            .value = data.headerMetricValues[1].data(),
        };
    }
    data.overlayProps.focusLabel = data.focusLabel.data();
    data.overlayProps.enabled = enabled;
    data.overlayProps.actionsVisible = true;
    data.overlayProps.stepBadgeColor = laneColor;
    data.overlayProps.actions[0] = {};
    data.overlayProps.actions[1] = core::ui::SequencerStepEditActionChip{
        .key = "Micro",
        .value = "Micro",
        .icon = ::standalone::icons::MICRO_SEQUENCE,
        .color = MICRO_SEQUENCE_COLOR,
        .valueColor = hasMicro ? MICRO_SEQUENCE_COLOR : 0U,
    };
    data.overlayProps.actions[2] = core::ui::SequencerStepEditActionChip{
        .key = "Cycle",
        .value = "Cycle",
        .icon = ::standalone::icons::CYCLE_STATE,
        .color = CYCLE_STATE_COLOR,
        .valueColor = hasCycle ? CYCLE_STATE_COLOR : 0U,
    };
    data.overlayProps.selectedIndex = data.selectedIndex;
    data.overlayProps.dataRevision = revision;
    return;
}

}  // namespace

FLASHMEM void buildStepEditRenderData(
    const Source& source,
    StepEditRenderData& data
) {
    auto& sequencer = source.sequencer;
    data.visible = sequencer.stepEdit.visible.get();
    if (!data.visible) {
        return;
    }
    if (sequencer.stepEdit.drumContext) {
        buildDrumStepEditRenderData(source, data);
        return;
    }

    const uint8_t step = sequencer.stepEdit.stepIndex.get();
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer);
    if (step >= len) {
        data.visible = false;
        return;
    }

    data.rowCount = static_cast<int>(StepEditRenderData::ROW_COUNT);
    data.stepIndex = step;
    data.selectedIndex = std::min<int>(
        sequencer.stepEdit.focusedRow.get(),
        data.rowCount - 1
    );

    if (sequencer.stepContentDraft.exitPromptVisible.get()) {
        const auto choice = sequencer.stepContentDraft.exitChoice.get();
        copyText(data.stepBadge.data(), data.stepBadge.size(), "BACK");
        copyText(data.summary.data(), data.summary.size(), "EDITED DRAFT");
        copyText(
            data.meta.data(),
            data.meta.size(),
            draftFailureLabel(sequencer.stepContentDraft) != nullptr
                ? draftFailureLabel(sequencer.stepContentDraft)
                : "Save is the default action"
        );
        copyText(data.focusLabel.data(), data.focusLabel.size(), "NAV turn/press");
        data.overlayProps = {
            .visible = true,
            .stepBadge = data.stepBadge.data(),
            .title = data.summary.data(),
            .meta = data.meta.data(),
            .focusLabel = data.focusLabel.data(),
            .titleCentered = true,
            .focusLabelVisible = true,
            .actionsVisible = true,
            .dataRevision = sequencer.stepContentDraft.revision.get(),
            .selectedVisualSlot = static_cast<core::ui::SequencerStepEditVisualSlot>(
                static_cast<uint8_t>(core::ui::SequencerStepEditVisualSlot::ACTION_0) +
                static_cast<uint8_t>(choice)
            ),
        };
        data.overlayProps.actions[0] = {
            .key = "Continue",
            .value = "Continue",
            .icon = ::standalone::icons::ACTION_BACKWARD,
            .color = core::ui::sequencer::semantic::color(
                core::ui::sequencer::semantic::Tone::STATE
            ),
        };
        data.overlayProps.actions[1] = {
            .key = "Discard",
            .value = "Discard",
            .icon = ::standalone::icons::ACTION_CANCEL,
            .color = CYCLE_STATE_COLOR,
        };
        data.overlayProps.actions[2] = {
            .key = "Save",
            .value = "Save",
            .icon = ::standalone::icons::ACTION_VALIDATE,
            .color = MICRO_SEQUENCE_COLOR,
        };
        data.dataRevision = data.overlayProps.dataRevision;
        return;
    }

    size_t titlePos = oc::type::text::appendString(data.title.data(), data.title.size(), 0, "STEP ");
    titlePos = oc::type::text::appendUnsigned(
        data.title.data(),
        data.title.size(),
        titlePos,
        static_cast<unsigned>(step) + 1U
    );
    oc::type::text::terminate(data.title.data(), data.title.size(), titlePos);

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
    // Step Editor is an authoring surface. Runtime variation telemetry may be
    // republished before playback has consumed the edited pattern revision;
    // never let that previous-cycle projection replace the value just written
    // by OPT in the same presentation frame.
    const auto resolved = core::state::sequencer::buildSequencerStepEditorDisplayState(
        displayContext,
        step
    );
    if (!resolved.valid) {
        data.visible = false;
        return;
    }

    const auto effectiveScaleSettings = displayContext.scaleSettings;
    const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
        sequencer,
        step,
        effectiveScaleSettings
    );
    if (!projection.valid) {
        data.visible = false;
        return;
    }

    std::snprintf(
        data.meta.data(),
        data.meta.size(),
        "P%u · S%u/%u · %s",
        static_cast<unsigned>(
            core::state::sequencer::activeContentPageForStep(step)
        ) + 1U,
        static_cast<unsigned>(step) + 1U,
        static_cast<unsigned>(len),
        projection.enabled ? "ON" : "OFF"
    );
    if (const char* failure = draftFailureLabel(sequencer.stepContentDraft)) {
        copyText(data.meta.data(), data.meta.size(), failure);
    }

    const auto displayValues =
        core::state::sequencer::sequencerResolvedStepDisplayValues(resolved);

    const bool localVariationMode =
        sequencer.stepEdit.localVariationEditActive.get() &&
        selectedRowIsProperty &&
        core::state::sequencer::stepPropertySupportsLocalVariation(selectedProperty);

    const auto nodeId = core::state::sequencer::activeContentStepNodeId(sequencer, step);
    const auto* graph = core::state::sequencer::graphView(
        core::state::sequencer::authoringPattern(sequencer)
    );
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
        const auto& chordSubEditor =
            sequencer.stepEdit.chordEditor.subEditor.get();
        core::state::sequencer::resolveStepChordPreview(
            chordUi,
            projection,
            effectiveScaleSettings
        );
        populateChordDetailOverlay(
            data,
            chordUi,
            sequencer.stepEdit.chordEditor.focusedField.get(),
            chordSubEditor.formulaEditorActive,
            chordSubEditor.focusedFormulaItem,
            chordSubEditor.sourceSelectorActive,
            chordSubEditor.focusedSourceChoice,
            projection.enabled
        );
        const auto expansionBudget =
            core::state::sequencer::projectSequencerExpansionBudget(
                sequencer,
                source.tracks.projectScaleSettings(),
                step
            );
        if (expansionBudget.noteBudgetExceeded) {
            constexpr uint32_t LIMIT_WARNING_COLOR =
                ::standalone::theme::color::STEP_PITCH;
            copyText(
                data.meta.data(),
                data.meta.size(),
                "16-note expansion limit"
            );
            data.overlayProps.meta = data.meta.data();
            data.overlayProps.metaColor = LIMIT_WARNING_COLOR;
            data.overlayProps.titleColor = LIMIT_WARNING_COLOR;
        }

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
        return;
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

    const bool hasChord = node != nullptr &&
        (node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE) ||
         node->has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL));
    copyText(
        data.valueBuffers[step_edit_rows::CHORD].data(),
        data.valueBuffers[step_edit_rows::CHORD].size(),
        hasChord ? "Edit" : "Create"
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
    data.overlayProps.selectedIndex = data.selectedIndex;
    data.overlayProps.actions[0] = core::ui::SequencerStepEditActionChip{
        .key = "Chord",
        .value = "Chord",
        .icon = ::standalone::icons::CHORD,
        .color = chordColor(),
        .valueColor = hasChord ? chordColor() : 0U,
    };
    data.overlayProps.actions[1] = core::ui::SequencerStepEditActionChip{
        .key = "Micro sequence",
        .value = "Micro",
        .icon = ::standalone::icons::MICRO_SEQUENCE,
        .color = MICRO_SEQUENCE_COLOR,
        .valueColor = projection.hasMicroSequence ? MICRO_SEQUENCE_COLOR : 0U,
    };
    data.overlayProps.actions[2] = core::ui::SequencerStepEditActionChip{
        .key = "Cycle state",
        .value = "Cycle",
        .icon = ::standalone::icons::CYCLE_STATE,
        .color = CYCLE_STATE_COLOR,
        .valueColor = projection.hasCycleStates ? CYCLE_STATE_COLOR : 0U,
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
    return;
}

FLASHMEM core::ui::ContextActionStripProps buildStepEditActionStripProps(const ActionSource& source) {
    StripProps props;
    using Action = core::state::sequencer::SequencerInteractionAction;

    auto& sequencer = source.sequencer;
    if (!sequencer.stepEdit.visible.get()) {
        props.visible = false;
        return props;
    }

    if (sequencer.stepEdit.drumContext) {
        const uint8_t lane = sequencer.stepEdit.drumLane;
        const uint8_t drumStep = sequencer.stepEdit.drumStep;
        if (!sequencer.drumSequencer.stepInRange(lane, drumStep)) {
            props.visible = false;
            return props;
        }
        props.visible = true;
        if (focusedRowIsValueRow(sequencer)) {
            constexpr auto resetAction = Action::RESET_STEP_EDITOR_ROW;
            props.slots[0] = core::ui::makeStandaloneIconStripSlot(
                core::ui::sequencer::interactionActionIcon(resetAction),
                Visual::ACTIVE,
                Tone::WARNING
            );
            props.slots[1].visualState = Visual::HIDDEN;
            props.slots[2].visualState = Visual::HIDDEN;
            return props;
        }
        if (!focusedRowIsContextRow(sequencer)) {
            props.slots[0].visualState = Visual::HIDDEN;
            props.slots[1].visualState = Visual::HIDDEN;
            props.slots[2].visualState = Visual::HIDDEN;
            return props;
        }

        core::state::sequencer::SequencerGraphNodeId nodeId =
            oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
        if (core::state::sequencer::isChildContentView(sequencer)) {
            nodeId = core::state::sequencer::activeContentStepNodeId(
                sequencer,
                sequencer.stepEdit.stepIndex.get()
            );
        } else if (sequencer.drumSequencer.drumTrack != nullptr) {
            const int16_t slot = sequencer.drumSequencer.drumTrack
                ->advancedRootSlot(lane, drumStep);
            if (slot >= 0) {
                nodeId = core::state::sequencer::rootStepNodeId(
                    static_cast<uint8_t>(slot)
                );
            }
        }
        const bool hasChild = nodeId !=
                oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID &&
            focusedContextHasChild(sequencer, nodeId);
        const bool canPaste = source.structureClipboard.hasSequencerStepContent(
                clipboardKindForFocusedContextRow(sequencer)
            ) &&
            core::state::sequencer::activeContentDepth(sequencer) <
                oc::note::sequencer::StepSequencerGraphLimits::MAX_DEPTH - 1U;
        const auto row = static_cast<size_t>(sequencer.stepEdit.focusedRow.get());
        const Tone contextTone = row == step_edit_rows::MICRO_SEQUENCE
            ? Tone::CONSTRUCTIVE
            : Tone::WARNING;
        const auto holdAction = sequencer.stepEdit.contextHold.action.get();
        const uint32_t holdStartedAtMs =
            sequencer.stepEdit.contextHold.startedAtMs.get();
        const bool removeHoldActive =
            holdAction == core::state::StructureHoldAction::REMOVE;
        const bool pasteHoldActive =
            holdAction == core::state::StructureHoldAction::PASTE;
        constexpr auto removeAction = Action::REMOVE_STEP_EDITOR_CONTEXT;
        const auto rightAction = canPaste
            ? Action::PASTE_STEP_EDITOR_CONTEXT
            : Action::COPY_STEP_EDITOR_CONTEXT;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            core::ui::sequencer::interactionActionIcon(removeAction),
            removeHoldActive
                ? Visual::ARMED
                : (hasChild ? Visual::ACTIVE : Visual::DISABLED),
            removeHoldActive ? Tone::DESTRUCTIVE : Tone::WARNING
        );
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            core::ui::sequencer::interactionActionIcon(rightAction),
            pasteHoldActive && canPaste
                ? Visual::ARMED
                : ((hasChild || canPaste) ? Visual::ACTIVE : Visual::DISABLED),
            pasteHoldActive && canPaste
                ? Tone::POSITIVE
                : (canPaste ? Tone::POSITIVE : contextTone)
        );
        props.slots[0].holdActive = removeHoldActive;
        props.slots[0].holdStartedAtMs = holdStartedAtMs;
        props.slots[0].holdDurationMs =
            Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
        props.slots[2].holdActive = pasteHoldActive && canPaste;
        props.slots[2].holdStartedAtMs = holdStartedAtMs;
        props.slots[2].holdDurationMs =
            Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
        return props;
    }

    const uint8_t step = sequencer.stepEdit.stepIndex.get();
    const uint8_t len = core::state::sequencer::activeContentLength(sequencer);
    if (step >= len) {
        props.visible = false;
        return props;
    }

    if (sequencer.stepContentDraft.exitPromptVisible.get()) {
        props.visible = true;
        props.slots[0].visualState = Visual::HIDDEN;
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }

    if (sequencer.stepContentDraft.active.get()) {
        props.visible = true;
        if (focusedRowIsValueRow(sequencer)) {
            constexpr auto resetAction = Action::RESET_STEP_EDITOR_ROW;
            props.slots[0] = core::ui::makeStandaloneIconStripSlot(
                core::ui::sequencer::interactionActionIcon(resetAction),
                Visual::ACTIVE,
                Tone::WARNING
            );
        } else {
            props.slots[0].visualState = Visual::HIDDEN;
        }
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            ::standalone::icons::ACTION_VALIDATE,
            Visual::ACTIVE,
            Tone::POSITIVE
        );
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
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }

    if (!focusedRowIsContextRow(sequencer)) {
        props.visible = true;
        props.slots[0].visualState = Visual::HIDDEN;
        props.slots[1].visualState = Visual::HIDDEN;
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
    const uint32_t holdStartedAtMs =
        sequencer.stepEdit.contextHold.startedAtMs.get();
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
    props.slots[1].visualState = Visual::HIDDEN;
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        core::ui::sequencer::interactionActionIcon(rightAction),
        pasteHoldActive && canPaste
            ? Visual::ARMED
            : ((hasChild || canPaste) ? Visual::ACTIVE : Visual::DISABLED),
        pasteHoldActive && canPaste ? Tone::POSITIVE : (canPaste ? Tone::POSITIVE : contextTone)
    );
    props.slots[0].holdActive = removeHoldActive;
    props.slots[0].holdStartedAtMs = holdStartedAtMs;
    props.slots[0].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    props.slots[2].holdActive = pasteHoldActive && canPaste;
    props.slots[2].holdStartedAtMs = holdStartedAtMs;
    props.slots[2].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    return props;
}

}  // namespace core::context::standalone::sequencer_overlay_presenter

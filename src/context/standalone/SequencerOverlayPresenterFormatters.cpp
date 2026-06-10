#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

#include "state/sequencer/SequencerStepContentEditSession.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {
namespace {

constexpr std::array<core::state::sequencer::StepProperty, 5> STEP_EDIT_PROPERTIES = {
    core::state::sequencer::StepProperty::NOTE,
    core::state::sequencer::StepProperty::VELOCITY,
    core::state::sequencer::StepProperty::GATE,
    core::state::sequencer::StepProperty::NUDGE,
    core::state::sequencer::StepProperty::PROBABILITY,
};

constexpr std::array<const char*, 5> STEP_EDIT_KEYS = {
    "Note",
    "Velocity",
    "Gate",
    "Nudge",
    "Probability",
};

constexpr size_t MICRO_SEQUENCE_ROW = STEP_EDIT_PROPERTIES.size();
constexpr size_t CYCLE_STATES_ROW = MICRO_SEQUENCE_ROW + 1U;
constexpr size_t MICRO_STEP_ROW = STEP_EDIT_PROPERTIES.size();
constexpr size_t MICRO_LENGTH_ROW = MICRO_STEP_ROW + 1U;

FLASHMEM bool isMicroContext(
    const core::state::sequencer::StepContentContextView& context
) {
    return context.active &&
           context.kind == core::state::sequencer::StepContentContextKind::MICRO_SEQUENCE;
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

FLASHMEM int offsetForProperty(
    const core::state::sequencer::StepContentFocusedValues& values,
    core::state::sequencer::StepProperty property
) {
    switch (property) {
        case core::state::sequencer::StepProperty::NOTE:
            return values.noteOffset;
        case core::state::sequencer::StepProperty::VELOCITY:
            return values.velocityOffset;
        case core::state::sequencer::StepProperty::GATE:
            return values.gateOffset;
        case core::state::sequencer::StepProperty::NUDGE:
            return values.nudgeOffset;
        case core::state::sequencer::StepProperty::PROBABILITY:
            return values.probabilityOffset;
    }
    return 0;
}

FLASHMEM void formatSignedNumber(char* buffer, size_t bufferSize, int value) {
    if (!buffer || bufferSize == 0) return;
    std::snprintf(buffer, bufferSize, "%+d", value);
}

FLASHMEM void formatSignedPercent(char* buffer, size_t bufferSize, int value) {
    if (!buffer || bufferSize == 0) return;
    std::snprintf(buffer, bufferSize, "%+d%%", value);
}

FLASHMEM void formatChildNoteOffset(
    char* buffer,
    size_t bufferSize,
    uint8_t parentNote,
    int offset
) {
    if (!buffer || bufferSize == 0) return;
    const int resolved = std::clamp<int>(static_cast<int>(parentNote) + offset, 0, 127);
    std::array<char, 6> noteName{};
    core::midi::formatNoteName(noteName.data(), noteName.size(), static_cast<uint8_t>(resolved));
    std::snprintf(buffer, bufferSize, "%s %+d", noteName.data(), offset);
}

FLASHMEM void formatChildPropertyValue(
    char* buffer,
    size_t bufferSize,
    core::state::sequencer::StepProperty property,
    const core::state::sequencer::StepContentFocusedValues& values,
    uint8_t parentNote
) {
    const int offset = offsetForProperty(values, property);
    switch (property) {
        case core::state::sequencer::StepProperty::NOTE:
            formatChildNoteOffset(buffer, bufferSize, parentNote, offset);
            return;
        case core::state::sequencer::StepProperty::GATE:
        case core::state::sequencer::StepProperty::NUDGE:
        case core::state::sequencer::StepProperty::PROBABILITY:
            formatSignedPercent(buffer, bufferSize, offset);
            return;
        case core::state::sequencer::StepProperty::VELOCITY:
        default:
            formatSignedNumber(buffer, bufferSize, offset);
            return;
    }
}

FLASHMEM uint32_t mixRevision(uint32_t seed, uint32_t value) {
    return (seed ^ value) * 16777619U;
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
    if (step >= core::state::sequencer::SequencerState::MAX_STEPS) {
        data.visible = false;
        return data;
    }

    const auto context = sequencer.stepEdit.contentSession.current();
    data.rowCount = static_cast<int>(StepEditRenderData::ROW_COUNT);
    data.stepIndex = step;
    data.selectedIndex = std::min<int>(
        sequencer.stepEdit.focusedRow.get(),
        data.rowCount - 1
    );

    const uint8_t len = sequencer.pattern.length.get();
    if (isMicroContext(context)) {
        std::snprintf(data.title.data(), data.title.size(), "MICRO S%u", static_cast<unsigned>(step) + 1U);
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "%u/%u D%u",
            static_cast<unsigned>(context.localIndex) + 1U,
            static_cast<unsigned>(context.length),
            static_cast<unsigned>(context.depth)
        );
    } else {
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
    }

    const uint8_t note = sequencer.pattern.note[step];
    const uint8_t velocity = sequencer.pattern.velocity[step];
    const uint16_t gate = sequencer.pattern.gate[step];
    const int8_t nudge = sequencer.pattern.nudge[step];
    const uint8_t probability = sequencer.pattern.probability[step];

    const auto focusedValues = sequencer.stepEdit.contentSession.focusedValues(sequencer.pattern);
    for (size_t i = 0; i < STEP_EDIT_PROPERTIES.size(); ++i) {
        if (isMicroContext(context) && focusedValues.valid) {
            formatChildPropertyValue(
                data.valueBuffers[i].data(),
                data.valueBuffers[i].size(),
                STEP_EDIT_PROPERTIES[i],
                focusedValues,
                note
            );
        } else {
            core::state::sequencer::formatStepPropertyValue(
                data.valueBuffers[i].data(),
                data.valueBuffers[i].size(),
                STEP_EDIT_PROPERTIES[i],
                note,
                velocity,
                gate,
                nudge,
                probability
            );
        }
        data.rows[i] = {
            .key = STEP_EDIT_KEYS[i],
            .value = data.valueBuffers[i].data(),
        };
    }

    if (isMicroContext(context)) {
        std::snprintf(
            data.valueBuffers[MICRO_STEP_ROW].data(),
            data.valueBuffers[MICRO_STEP_ROW].size(),
            "%u/%u",
            static_cast<unsigned>(context.localIndex) + 1U,
            static_cast<unsigned>(context.length)
        );
        data.rows[MICRO_STEP_ROW] = {
            .key = "Micro step",
            .value = data.valueBuffers[MICRO_STEP_ROW].data(),
        };

        std::snprintf(
            data.valueBuffers[MICRO_LENGTH_ROW].data(),
            data.valueBuffers[MICRO_LENGTH_ROW].size(),
            "%u",
            static_cast<unsigned>(context.length)
        );
        data.rows[MICRO_LENGTH_ROW] = {
            .key = "Length",
            .value = data.valueBuffers[MICRO_LENGTH_ROW].data(),
        };
    } else {
        const auto microAvailability = sequencer.stepEdit.contentSession.childCreationAvailability(
            sequencer.pattern,
            core::state::sequencer::StepContentChildKind::MICRO_SEQUENCE,
            core::state::sequencer::SequencerStepContentEditSession::DEFAULT_MICRO_SEQUENCE_LENGTH
        );
        const auto cycleAvailability = sequencer.stepEdit.contentSession.childCreationAvailability(
            sequencer.pattern,
            core::state::sequencer::StepContentChildKind::CYCLE_STATES,
            core::state::sequencer::SequencerStepContentEditSession::DEFAULT_CYCLE_STATE_COUNT
        );

        std::snprintf(
            data.valueBuffers[MICRO_SEQUENCE_ROW].data(),
            data.valueBuffers[MICRO_SEQUENCE_ROW].size(),
            "%s",
            availabilityLabel(microAvailability)
        );
        data.rows[MICRO_SEQUENCE_ROW] = {
            .key = "Micro-seq",
            .value = data.valueBuffers[MICRO_SEQUENCE_ROW].data(),
        };

        std::snprintf(
            data.valueBuffers[CYCLE_STATES_ROW].data(),
            data.valueBuffers[CYCLE_STATES_ROW].size(),
            "%s",
            availabilityLabel(cycleAvailability)
        );
        data.rows[CYCLE_STATES_ROW] = {
            .key = "Cycle states",
            .value = data.valueBuffers[CYCLE_STATES_ROW].data(),
        };
    }

    uint32_t revision = 2166136261U;
    revision = mixRevision(revision, sequencer.pattern.stepDataRevision.get());
    revision = mixRevision(revision, sequencer.pattern.graphRevision.get());
    revision = mixRevision(revision, sequencer.stepEdit.contentRevision.get());
    revision = mixRevision(revision, static_cast<uint32_t>(data.selectedIndex));
    revision = mixRevision(revision, static_cast<uint32_t>(context.kind));
    revision = mixRevision(revision, static_cast<uint32_t>(context.localIndex));
    revision = mixRevision(revision, static_cast<uint32_t>(context.length));
    revision = mixRevision(revision, static_cast<uint32_t>(context.depth));
    revision = mixRevision(revision, static_cast<uint32_t>(step));
    revision = mixRevision(revision, static_cast<uint32_t>(len));
    data.dataRevision = revision;
    return data;
}

}  // namespace core::context::standalone::sequencer_overlay_presenter

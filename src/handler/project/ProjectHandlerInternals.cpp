#include "handler/project/ProjectHandlerInternals.hpp"

#include <algorithm>
#include <cstring>

#include <oc/type/TextFormat.hpp>

#include "handler/sequencer/SequencerInputUtils.hpp"
#include "persistence/ProjectLoadReport.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectSlug.hpp"

namespace core::handler::project_handler_internal {

FLASHMEM uint8_t projectModulatorFreePeriodIndex(uint32_t periodMs) {
    uint8_t best = 0;
    uint32_t bestDistance = UINT32_MAX;
    for (uint8_t index = 0; index < PROJECT_MODULATOR_FREE_PERIODS_MS.size(); ++index) {
        const uint32_t candidate = PROJECT_MODULATOR_FREE_PERIODS_MS[index];
        const uint32_t distance = candidate > periodMs
            ? candidate - periodMs
            : periodMs - candidate;
        if (distance < bestDistance) {
            best = index;
            bestDistance = distance;
        }
    }
    return best;
}

FLASHMEM int signedStepCount(float delta) {
    if (delta == 0.0f) return 0;
    const float absolute = delta > 0.0f ? delta : -delta;
    int magnitude = static_cast<int>(absolute);
    if (magnitude < 1) magnitude = 1;
    return delta > 0.0f ? magnitude : -magnitude;
}

FLASHMEM int clampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

FLASHMEM int wrapIndex(int value, int count) {
    if (count <= 0) return 0;
    int wrapped = value % count;
    if (wrapped < 0) wrapped += count;
    return wrapped;
}

FLASHMEM float clampNormalized(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

FLASHMEM int normalizedToIndex(float normalized, int count) {
    if (count <= 1) return 0;
    const float value = clampNormalized(normalized);
    return std::clamp(
        static_cast<int>(value * static_cast<float>(count - 1) + 0.5f),
        0,
        count - 1
    );
}

FLASHMEM float indexToNormalized(int index, int count) {
    if (count <= 1) return 0.0f;
    const int clamped = clampInt(index, 0, count - 1);
    return static_cast<float>(clamped) / static_cast<float>(count - 1);
}

FLASHMEM bool isProjectNameEditorNode(core::state::project::ProjectNodeId node) {
    return node == core::state::project::ProjectNodeId::SAVE_AS_PROJECT_NAME ||
           node == core::state::project::ProjectNodeId::RENAME_PROJECT_NAME ||
           node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_RENAME;
}

FLASHMEM char selectedProjectNameKey(
    const core::state::project::ProjectNavigationState& navigation
) {
    char key = core::state::project::projectNameKeyboardCellAt(
        navigation.projectNameKeyIndex
    ).character;
    if (navigation.projectNameShiftActive && key >= 'a' && key <= 'z') {
        key = static_cast<char>(key - 'a' + 'A');
    }
    return key;
}

FLASHMEM bool appendProjectNameChar(
    core::state::project::ProjectNavigationState& navigation,
    char character
) {
    if (character == '\0') return false;
    char* buffer = navigation.editingProjectSlug.data();
    const size_t length = std::strlen(buffer);
    if (length >= core::state::project::PROJECT_SLUG_MAX_LENGTH) return false;
    buffer[length] = character;
    buffer[length + 1U] = '\0';
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM bool appendProjectNameKey(core::state::project::ProjectNavigationState& navigation) {
    return appendProjectNameChar(navigation, selectedProjectNameKey(navigation));
}

FLASHMEM bool appendProjectNameSpace(core::state::project::ProjectNavigationState& navigation) {
    return appendProjectNameChar(navigation, ' ');
}

FLASHMEM bool backspaceProjectName(core::state::project::ProjectNavigationState& navigation) {
    char* buffer = navigation.editingProjectSlug.data();
    const size_t length = std::strlen(buffer);
    if (length == 0) return true;
    buffer[length - 1U] = '\0';
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM bool clearProjectName(core::state::project::ProjectNavigationState& navigation) {
    if (navigation.editingProjectSlug[0] == '\0') return true;
    navigation.editingProjectSlug = {};
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM int tempoFromNormalized(float normalized) {
    return static_cast<int>(project::PROJECT_TEMPO_MIN_BPM) +
           normalizedToIndex(normalized, project::PROJECT_TEMPO_RANGE_STEPS);
}

FLASHMEM float tempoToNormalized(float tempoBpm) {
    const int tempo = clampInt(
        project::roundedProjectTempoBpm(tempoBpm),
        static_cast<int>(project::PROJECT_TEMPO_MIN_BPM),
        static_cast<int>(project::PROJECT_TEMPO_MAX_BPM)
    );
    return indexToNormalized(
        tempo - static_cast<int>(project::PROJECT_TEMPO_MIN_BPM),
        project::PROJECT_TEMPO_RANGE_STEPS
    );
}

FLASHMEM float normalizedTurnsForStepRate(int stepCount, float stepsPerTurn) {
    if (stepCount <= 1 || stepsPerTurn <= 0.0f) return PROJECT_OPT_NORMALIZED_TURNS;
    return static_cast<float>(stepCount - 1) / stepsPerTurn;
}

FLASHMEM int midiSyncModeIndex(core::state::MidiSyncMode mode) {
    switch (mode) {
        case core::state::MidiSyncMode::MASTER:
            return 0;
        case core::state::MidiSyncMode::SLAVE:
            return 1;
        case core::state::MidiSyncMode::AUTO:
        default:
            return 2;
    }
}

FLASHMEM core::state::MidiSyncMode midiSyncModeAt(int index) {
    switch (wrapIndex(index, 3)) {
        case 0:
            return core::state::MidiSyncMode::MASTER;
        case 1:
            return core::state::MidiSyncMode::SLAVE;
        case 2:
        default:
            return core::state::MidiSyncMode::AUTO;
    }
}

FLASHMEM const char* projectLifecycleFailureLabel(
    ProjectLifecycleDomainServices::Status status,
    const char* fallback
) {
    using Status = ProjectLifecycleDomainServices::Status;
    switch (status) {
        case Status::UNAVAILABLE:
            return "Storage unavailable";
        case Status::INVALID_ARGUMENT:
            return "Invalid project";
        case Status::ALREADY_EXISTS:
            return "Name exists";
        case Status::SAVE_FAILED:
            return "Save failed";
        case Status::LOAD_FAILED:
            return "Load failed";
        case Status::LIST_FAILED:
            return "List failed";
        case Status::DRAFT_ACTIVE:
            return "Finish Step draft";
        case Status::OK:
        default:
            return fallback;
    }
}

FLASHMEM const char* projectLoadFeedbackLabel(
    const ProjectLifecycleDomainServices::Result& result
) {
    if (!result.success()) {
        return projectLifecycleFailureLabel(result.status, "Load failed");
    }
    return "Loaded";
}

FLASHMEM void formatProjectLifecycleFeedback(
    char* out,
    size_t outSize,
    const char* verb,
    const char* projectId
) {
    if (!out || outSize == 0U) return;
    size_t pos = oc::type::text::appendString(out, outSize, 0, verb);
    if (projectId && projectId[0] != '\0') {
        pos = oc::type::text::appendChar(out, outSize, pos, ' ');
        pos = oc::type::text::appendString(out, outSize, pos, projectId);
    }
    oc::type::text::terminate(out, outSize, pos);
}

FLASHMEM void configureOptContinuous(oc::api::EncoderAPI& encoders,
                                     float position,
                                     float normalizedTurns) {
    encoders.setMode(EncoderID::OPT, oc::interface::EncoderMode::NORMALIZED);
    encoders.setDiscreteTicksPerStep(EncoderID::OPT, PROJECT_OPT_TICKS_PER_STEP);
    encoders.setNormalizedTurns(EncoderID::OPT, normalizedTurns);
    encoders.setContinuous(EncoderID::OPT);
    encoders.setPosition(EncoderID::OPT, clampNormalized(position));
}

FLASHMEM void configureOptRaw(oc::api::EncoderAPI& encoders) {
    encoders.setMode(EncoderID::OPT, oc::interface::EncoderMode::RAW);
    encoders.setPosition(EncoderID::OPT, 0.0f);
}

FLASHMEM void configureOptDiscrete(oc::api::EncoderAPI& encoders,
                                   int stepCount,
                                   float position,
                                   float normalizedTurns) {
    encoders.setMode(EncoderID::OPT, oc::interface::EncoderMode::NORMALIZED);
    encoders.setDiscreteTicksPerStep(EncoderID::OPT, PROJECT_OPT_TICKS_PER_STEP);
    encoders.setNormalizedTurns(EncoderID::OPT, normalizedTurns);
    encoders.setDiscreteSteps(
        EncoderID::OPT,
        static_cast<uint8_t>(clampInt(stepCount, 1, 255))
    );
    encoders.setPosition(EncoderID::OPT, clampNormalized(position));
}

}  // namespace core::handler::project_handler_internal

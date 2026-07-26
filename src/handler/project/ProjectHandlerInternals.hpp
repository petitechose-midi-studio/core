#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "handler/project/ProjectHandler.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/MidiSyncState.hpp"
#include "state/project/ProjectDomainRules.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectSlug.hpp"

namespace core::handler::project_handler_internal {

using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;
namespace project = core::state::project;

constexpr uint16_t PROJECT_OPT_TICKS_PER_STEP =
    core::handler::sequencer::input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP;
constexpr float PROJECT_OPT_NORMALIZED_TURNS =
    core::handler::sequencer::input_utils::DEFAULT_NORMALIZED_TURNS;
constexpr float PROJECT_OPT_TEMPO_STEPS_PER_TURN = 24.0f;
constexpr float PROJECT_OPT_PERCENT_STEPS_PER_TURN = 18.0f;
constexpr float PROJECT_NAME_KEYBOARD_OPT_TICKS_PER_ROW =
    (600.0f * 4.0f) /
    static_cast<float>(core::state::project::PROJECT_NAME_KEYBOARD_ROW_COUNT);
inline constexpr std::array<uint32_t, 13> PROJECT_MODULATOR_FREE_PERIODS_MS{{
    8U, 16U, 32U, 64U, 125U, 250U, 500U,
    1000U, 2000U, 4000U, 8000U, 16000U, 32000U,
}};
extern const char MODULATOR_PREVIEW_PENDING_FEEDBACK[];

FLASHMEM uint8_t projectModulatorFreePeriodIndex(uint32_t periodMs);

FLASHMEM int signedStepCount(float delta);
FLASHMEM int clampInt(int value, int low, int high);
FLASHMEM int wrapIndex(int value, int count);
FLASHMEM float clampNormalized(float value);
FLASHMEM int normalizedToIndex(float normalized, int count);
FLASHMEM float indexToNormalized(int index, int count);
FLASHMEM bool isProjectNameEditorNode(core::state::project::ProjectNodeId node);
FLASHMEM char selectedProjectNameKey(
    const core::state::project::ProjectNavigationState& navigation
);
FLASHMEM bool appendProjectNameChar(
    core::state::project::ProjectNavigationState& navigation,
    char character
);
FLASHMEM bool appendProjectNameKey(core::state::project::ProjectNavigationState& navigation);
FLASHMEM bool appendProjectNameSpace(core::state::project::ProjectNavigationState& navigation);
FLASHMEM bool backspaceProjectName(core::state::project::ProjectNavigationState& navigation);
FLASHMEM bool clearProjectName(core::state::project::ProjectNavigationState& navigation);
FLASHMEM int tempoFromNormalized(float normalized);
FLASHMEM float tempoToNormalized(float tempoBpm);
FLASHMEM float normalizedTurnsForStepRate(int stepCount, float stepsPerTurn);
FLASHMEM int midiSyncModeIndex(core::state::MidiSyncMode mode);
FLASHMEM core::state::MidiSyncMode midiSyncModeAt(int index);
FLASHMEM const char* projectLifecycleFailureLabel(
    ProjectLifecycleDomainServices::Status status,
    const char* fallback
);
FLASHMEM const char* projectLoadFeedbackLabel(
    const ProjectLifecycleDomainServices::Result& result
);
FLASHMEM void formatProjectLifecycleFeedback(
    char* out,
    size_t outSize,
    const char* verb,
    const char* projectId
);
FLASHMEM void configureOptContinuous(
    oc::api::EncoderAPI& encoders,
    float position,
    float normalizedTurns = PROJECT_OPT_NORMALIZED_TURNS
);
FLASHMEM void configureOptRaw(oc::api::EncoderAPI& encoders);
FLASHMEM void configureOptDiscrete(
    oc::api::EncoderAPI& encoders,
    int stepCount,
    float position,
    float normalizedTurns = PROJECT_OPT_NORMALIZED_TURNS
);

}  // namespace core::handler::project_handler_internal

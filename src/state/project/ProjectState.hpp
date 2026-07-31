#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerScaleState.hpp"

namespace core::state::project {

struct ProjectMetadata {
    static constexpr uint8_t ID_SIZE = 55;
    static constexpr uint8_t NAME_SIZE = 55;

    std::array<char, ID_SIZE> id{};
    std::array<char, NAME_SIZE> name{};
    uint32_t modifiedCounter = 0;
    bool dirty = false;
    bool hasSavedIdentity = false;

    void reset();
};

struct ProjectTransportState {
    static constexpr float DEFAULT_TEMPO_BPM = PROJECT_TEMPO_DEFAULT_BPM;
    static constexpr uint8_t DEFAULT_SWING_PERCENT = PROJECT_SWING_DEFAULT_PERCENT;
    static constexpr uint8_t DEFAULT_RUN_MODE = PROJECT_RUN_MODE_DEFAULT;

    float tempoBpm = DEFAULT_TEMPO_BPM;
    uint8_t swingPercent = DEFAULT_SWING_PERCENT;
    uint8_t runMode = DEFAULT_RUN_MODE;

    void reset();
};

struct ProjectMusicalContext {
    oc::note::sequencer::StepSequencerScaleSettings scale =
        core::state::sequencer::defaultProjectScaleSettings();
    bool patternsInheritScale = true;
    bool clipsInheritScale = true;

    void reset();
};

struct ProjectEditingState {
    ProjectStepPasteMode stepPasteMode = PROJECT_STEP_PASTE_MODE_DEFAULT;
    std::array<uint8_t, PROJECT_CC_LANE_DEFAULT_COUNT>
        ccLaneDefaultControllers = PROJECT_CC_LANE_DEFAULT_CONTROLLERS;

    void reset();
};

struct ProjectState {
    ProjectMetadata metadata{};
    ProjectTransportState transport{};
    ProjectMusicalContext musical{};
    ProjectEditingState editing{};

    ProjectState();
    void reset();
};

}  // namespace core::state::project

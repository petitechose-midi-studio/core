#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::project {

struct ProjectMetadata {
    static constexpr uint8_t ID_SIZE = 16;
    static constexpr uint8_t NAME_SIZE = 24;

    std::array<char, ID_SIZE> id{};
    std::array<char, NAME_SIZE> name{};
    uint32_t modifiedCounter = 0;
    bool dirty = false;
    bool hasSavedIdentity = false;

    void reset();
};

struct ProjectTransportState {
    static constexpr float DEFAULT_TEMPO_BPM = 120.0f;
    static constexpr uint8_t DEFAULT_SWING_PERCENT = 0;
    static constexpr uint8_t DEFAULT_RUN_MODE = 0;

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

struct ProjectRoutingState {
    std::array<uint8_t, core::state::sequencer::SequencerTrackBankState::TRACK_COUNT>
        outputMidiChannels{};

    void reset();
};

struct ProjectState {
    ProjectMetadata metadata{};
    ProjectTransportState transport{};
    ProjectMusicalContext musical{};
    ProjectRoutingState routing{};

    ProjectState();
    void reset();
};

}  // namespace core::state::project

#pragma once

#include <cstdint>

#include "state/project/ProjectState.hpp"

namespace core::persistence::project_state_codec {

inline constexpr uint8_t PROJECT_STATE_CHUNK_VERSION_MAJOR = 1;
inline constexpr uint8_t PROJECT_STATE_CHUNK_VERSION_MINOR = 1;

inline constexpr uint32_t PROJECT_META_PAYLOAD_SIZE =
    core::state::project::ProjectMetadata::ID_SIZE +
    core::state::project::ProjectMetadata::NAME_SIZE +
    8U;
inline constexpr uint32_t PROJECT_TRANSPORT_PAYLOAD_SIZE = 8;
inline constexpr uint32_t PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE = 8;
inline constexpr uint32_t PROJECT_ROUTING_PAYLOAD_SIZE =
    core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
inline constexpr uint32_t PROJECT_EDITING_PAYLOAD_SIZE = 8;

struct ProjectMetaPayload {
    char id[core::state::project::ProjectMetadata::ID_SIZE] = {};
    char name[core::state::project::ProjectMetadata::NAME_SIZE] = {};
    uint32_t modifiedCounter = 0;
    uint8_t flags = 0;
};

struct ProjectTransportPayload {
    uint16_t tempoCentiBpm = core::state::project::projectTempoToCentiBpm(
        core::state::project::PROJECT_TEMPO_DEFAULT_BPM
    );
    uint8_t swingPercent = core::state::project::PROJECT_SWING_DEFAULT_PERCENT;
    uint8_t runMode = core::state::project::PROJECT_RUN_MODE_DEFAULT;
};

struct ProjectMusicalContextPayload {
    uint8_t scaleRoot = 0;
    uint8_t scaleType = 0;
    uint8_t scaleConstraintMode = 0;
    uint8_t flags = 0;
};

struct ProjectRoutingPayload {
    uint8_t outputMidiChannels[core::state::sequencer::SequencerTrackBankState::TRACK_COUNT] = {};
};

struct ProjectEditingPayload {
    uint8_t stepPasteMode =
        static_cast<uint8_t>(core::state::project::PROJECT_STEP_PASTE_MODE_DEFAULT);
};

static_assert(PROJECT_META_PAYLOAD_SIZE == 118, "Unexpected project meta payload size");
static_assert(PROJECT_TRANSPORT_PAYLOAD_SIZE == 8, "Unexpected project transport payload size");
static_assert(PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE == 8,
              "Unexpected project musical-context payload size");
static_assert(PROJECT_ROUTING_PAYLOAD_SIZE == 16, "Unexpected project routing payload size");
static_assert(PROJECT_EDITING_PAYLOAD_SIZE == 8, "Unexpected project editing payload size");

}  // namespace core::persistence::project_state_codec

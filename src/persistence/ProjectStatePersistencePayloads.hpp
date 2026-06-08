#pragma once

#include <cstdint>

#include "state/project/ProjectState.hpp"

namespace core::persistence::project_state_codec {

inline constexpr uint8_t PROJECT_STATE_CHUNK_VERSION_MAJOR = 1;
inline constexpr uint8_t PROJECT_STATE_CHUNK_VERSION_MINOR = 0;

#pragma pack(push, 1)
struct ProjectMetaPayload {
    char id[core::state::project::ProjectMetadata::ID_SIZE] = {};
    char name[core::state::project::ProjectMetadata::NAME_SIZE] = {};
    uint32_t modifiedCounter = 0;
    uint8_t flags = 0;
    uint8_t reserved0 = 0;
    uint16_t reserved1 = 0;
};

struct ProjectTransportPayload {
    uint16_t tempoCentiBpm = 12000;
    uint8_t swingPercent = 0;
    uint8_t runMode = 0;
    uint32_t reserved0 = 0;
};

struct ProjectMusicalContextPayload {
    uint8_t scaleRoot = 0;
    uint8_t scaleType = 0;
    uint8_t scaleConstraintMode = 0;
    uint8_t flags = 0;
    uint32_t reserved0 = 0;
};

struct ProjectRoutingPayload {
    uint8_t outputMidiChannels[core::state::sequencer::SequencerTrackBankState::TRACK_COUNT] = {};
};
#pragma pack(pop)

static_assert(sizeof(ProjectMetaPayload) == 48, "Unexpected ProjectMetaPayload size");
static_assert(sizeof(ProjectTransportPayload) == 8, "Unexpected ProjectTransportPayload size");
static_assert(sizeof(ProjectMusicalContextPayload) == 8, "Unexpected ProjectMusicalContextPayload size");
static_assert(sizeof(ProjectRoutingPayload) == 16, "Unexpected ProjectRoutingPayload size");

}  // namespace core::persistence::project_state_codec

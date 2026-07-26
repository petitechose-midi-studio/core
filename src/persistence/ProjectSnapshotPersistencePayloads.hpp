#pragma once

#include <cstdint>

#include "persistence/MacroTrackBankPersistenceCodec.hpp"
#include "persistence/ProjectControlPersistencePayloads.hpp"

namespace core::persistence::project_snapshot_codec {

inline constexpr uint8_t PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR = 1;
inline constexpr uint8_t PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR = 2;
inline constexpr uint8_t PROJECT_MACRO_STATE_CHUNK_VERSION_MINOR = 3;
inline constexpr uint8_t PROJECT_SEQUENCER_STATE_CHUNK_VERSION_MINOR = 3;
inline constexpr uint8_t PROJECT_MACRO_AUTOMATION_CHUNK_VERSION_MINOR =
    core::persistence::project_control_codec::PROJECT_AUTOMATION_CHUNK_VERSION_MINOR;
inline constexpr uint8_t PROJECT_MODULATION_GRAPH_CHUNK_VERSION_MINOR =
    core::persistence::project_control_codec::
        PROJECT_MODULATION_GRAPH_CHUNK_VERSION_MINOR;
inline constexpr uint32_t PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE =
    core::persistence::project_control_codec::
        PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE;

inline constexpr uint32_t PROJECT_MACRO_STATE_PAYLOAD_SIZE =
    core::persistence::macro_track_codec::MACRO_TRACK_BANK_PAYLOAD_SIZE;
static_assert(PROJECT_MACRO_STATE_PAYLOAD_SIZE == 15412,
              "Unexpected project macro-state payload size");

}  // namespace core::persistence::project_snapshot_codec

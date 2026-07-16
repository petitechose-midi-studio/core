#pragma once

#include <cstdint>

#include "persistence/LegacyMacroAutomationPersistenceCodec.hpp"
#include "persistence/MacroTrackBankPersistenceCodec.hpp"
#include "persistence/ProjectControlPersistencePayloads.hpp"

namespace core::persistence::project_snapshot_codec {

inline constexpr uint8_t PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR = 1;
inline constexpr uint8_t PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR = 2;
inline constexpr uint8_t PROJECT_MACRO_AUTOMATION_LEGACY_CHUNK_VERSION_MINOR =
    core::persistence::macro_automation_legacy_codec::CHUNK_VERSION_MINOR_V14;
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
inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_HEADER_SIZE =
    core::persistence::macro_automation_legacy_codec::HEADER_SIZE;
inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_CURVE_REF_SIZE =
    core::persistence::macro_automation_legacy_codec::CURVE_REF_SIZE;
inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_SLOT_STATE_SIZE =
    core::persistence::macro_automation_legacy_codec::SLOT_STATE_SIZE;
inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_ENTRY_SIZE =
    core::persistence::macro_automation_legacy_codec::ENTRY_SIZE;
inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_POINT_SIZE =
    core::persistence::macro_automation_legacy_codec::POINT_SIZE;
inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_MAX_PAYLOAD_SIZE =
    core::persistence::macro_automation_legacy_codec::MAX_PAYLOAD_SIZE;

static_assert(PROJECT_MACRO_STATE_PAYLOAD_SIZE == 15428,
              "Unexpected project macro-state payload size");
static_assert(PROJECT_MACRO_AUTOMATION_SLOT_STATE_SIZE == 32,
              "Unexpected macro automation slot-state payload size");
static_assert(PROJECT_MACRO_AUTOMATION_ENTRY_SIZE == 36,
              "Unexpected macro automation entry payload size");
static_assert(PROJECT_MACRO_AUTOMATION_POINT_SIZE ==
                  sizeof(core::state::macro::MacroPackedCurvePoint),
              "Unexpected macro automation point payload size");

}  // namespace core::persistence::project_snapshot_codec

#pragma once

#include <cstdint>

#include "persistence/MacroTrackBankPersistenceCodec.hpp"
#include "state/macro/MacroAutomationState.hpp"

namespace core::persistence::project_snapshot_codec {

inline constexpr uint8_t PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR = 1;
inline constexpr uint8_t PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR = 2;
inline constexpr uint8_t PROJECT_MACRO_AUTOMATION_CHUNK_VERSION_MINOR = 4;

inline constexpr uint32_t PROJECT_MACRO_STATE_PAYLOAD_SIZE =
    core::persistence::macro_track_codec::MACRO_TRACK_BANK_PAYLOAD_SIZE;
inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_HEADER_SIZE = 8;
inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_CURVE_REF_SIZE = 14;
inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_SLOT_STATE_SIZE =
    PROJECT_MACRO_AUTOMATION_CURVE_REF_SIZE * 2U + sizeof(float);
inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_ENTRY_SIZE =
    4U + PROJECT_MACRO_AUTOMATION_SLOT_STATE_SIZE;
inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_POINT_SIZE = 4;

inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_MAX_PAYLOAD_SIZE =
    PROJECT_MACRO_AUTOMATION_HEADER_SIZE +
    static_cast<uint32_t>(core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY) *
        PROJECT_MACRO_AUTOMATION_ENTRY_SIZE +
    static_cast<uint32_t>(core::state::macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY) *
        PROJECT_MACRO_AUTOMATION_POINT_SIZE;

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

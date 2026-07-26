#pragma once

#include <cstdint>

#include "state/modulation/ProjectControlDomainState.hpp"

namespace core::persistence::project_control_codec {

inline constexpr uint32_t PROJECT_MODULATION_GRAPH_CHUNK_ID = 0x4D4F4447U;
inline constexpr uint8_t PROJECT_CONTROL_CHUNK_VERSION_MAJOR = 1U;
inline constexpr uint8_t PROJECT_AUTOMATION_CHUNK_VERSION_MINOR = 6U;
inline constexpr uint8_t PROJECT_MODULATION_GRAPH_CHUNK_VERSION_MINOR = 5U;

inline constexpr uint32_t PROJECT_CONTROL_CHUNK_HEADER_SIZE = 32U;
inline constexpr uint32_t PROJECT_AUTOMATION_ENTRY_SIZE = 8U;
inline constexpr uint32_t PROJECT_MODULATOR_SOURCE_DIRECTORY_SIZE = 30U;
inline constexpr uint32_t PROJECT_MODULATOR_SOURCE_PAYLOAD_SIZE = 16U;
inline constexpr uint32_t PROJECT_MODULATION_BINDING_SIZE = 20U;
inline constexpr uint32_t PROJECT_MODULATION_TRIGGER_SIZE = 16U;
inline constexpr uint32_t PROJECT_MODULATION_DESTINATION_SCALE_SIZE = 6U;
inline constexpr uint32_t PROJECT_CONTROL_CURVE_RECORD_SIZE = 20U;
inline constexpr uint32_t PROJECT_CONTROL_CURVE_POINT_SIZE = 4U;

inline constexpr uint32_t PROJECT_AUTOMATION_MAX_PAYLOAD_SIZE =
    PROJECT_CONTROL_CHUNK_HEADER_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY
    ) * PROJECT_AUTOMATION_ENTRY_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY
    ) * PROJECT_CONTROL_CURVE_RECORD_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_CURVE_POINT_CAPACITY
    ) * PROJECT_CONTROL_CURVE_POINT_SIZE;

inline constexpr uint32_t PROJECT_MODULATION_GRAPH_MAX_PAYLOAD_SIZE =
    PROJECT_CONTROL_CHUNK_HEADER_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_MODULATOR_CAPACITY
    ) * PROJECT_MODULATOR_SOURCE_DIRECTORY_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_MODULATOR_CAPACITY
    ) * PROJECT_MODULATOR_SOURCE_PAYLOAD_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_MODULATION_BINDING_CAPACITY
    ) * PROJECT_MODULATION_BINDING_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_MODULATION_TRIGGER_CAPACITY
    ) * PROJECT_MODULATION_TRIGGER_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_MODULATION_DESTINATION_SCALE_CAPACITY
    ) * PROJECT_MODULATION_DESTINATION_SCALE_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_MODULATOR_CAPACITY
    ) * PROJECT_CONTROL_CURVE_RECORD_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_CURVE_POINT_CAPACITY
    ) * PROJECT_CONTROL_CURVE_POINT_SIZE;

/** Both chunks share one 32768-point budget in the authored domain. */
inline constexpr uint32_t PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE =
    2U * PROJECT_CONTROL_CHUNK_HEADER_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY
    ) * PROJECT_AUTOMATION_ENTRY_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_MODULATOR_CAPACITY
    ) * PROJECT_MODULATOR_SOURCE_DIRECTORY_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_MODULATOR_CAPACITY
    ) * PROJECT_MODULATOR_SOURCE_PAYLOAD_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_MODULATION_BINDING_CAPACITY
    ) * PROJECT_MODULATION_BINDING_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_MODULATION_TRIGGER_CAPACITY
    ) * PROJECT_MODULATION_TRIGGER_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_MODULATION_DESTINATION_SCALE_CAPACITY
    ) * PROJECT_MODULATION_DESTINATION_SCALE_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_CURVE_LIVE_CAPACITY
    ) * PROJECT_CONTROL_CURVE_RECORD_SIZE +
    static_cast<uint32_t>(
        core::state::modulation::PROJECT_CURVE_POINT_CAPACITY
    ) * PROJECT_CONTROL_CURVE_POINT_SIZE;

static_assert(PROJECT_AUTOMATION_MAX_PAYLOAD_SIZE == 134688U);
static_assert(PROJECT_MODULATION_GRAPH_MAX_PAYLOAD_SIZE == 154912U);
static_assert(PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE == 158528U);

}  // namespace core::persistence::project_control_codec

#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/macro/MacroAutomationState.hpp"
#include "state/macro/MacroPagesState.hpp"

namespace core::persistence::project_snapshot_codec {

inline constexpr uint8_t PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR = 1;
inline constexpr uint8_t PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR = 2;
inline constexpr uint8_t PROJECT_MACRO_AUTOMATION_CHUNK_VERSION_MINOR = 3;

#pragma pack(push, 1)
struct ProjectMacroStatePayload {
    uint8_t activeTrack = 0;
    uint8_t reserved0 = 0;
    uint16_t trackEnabledMask =
        core::state::macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;
    std::array<core::state::macro::MacroTrackData, core::state::macro::TRACK_COUNT> tracks{};
};
#pragma pack(pop)

#pragma pack(push, 1)
struct ProjectMacroAutomationPayloadHeader {
    uint8_t entryCount = 0;
    uint8_t reserved0 = 0;
    uint16_t pointCount = 0;
    uint32_t reserved1 = 0;
};
#pragma pack(pop)

struct ProjectMacroAutomationEntryPayload {
    core::state::macro::MacroAutomationSlotAddress address{};
    core::state::macro::MacroAutomationSlotState state{};
};

inline constexpr uint32_t PROJECT_MACRO_AUTOMATION_MAX_PAYLOAD_SIZE =
    sizeof(ProjectMacroAutomationPayloadHeader) +
    static_cast<uint32_t>(core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY) *
        sizeof(ProjectMacroAutomationEntryPayload) +
    static_cast<uint32_t>(core::state::macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY) *
        sizeof(core::state::macro::MacroPackedCurvePoint);

static_assert(std::is_trivially_copyable_v<ProjectMacroStatePayload>,
              "ProjectMacroStatePayload must remain trivially copyable");
static_assert(sizeof(ProjectMacroStatePayload) == 15428,
              "Unexpected ProjectMacroStatePayload size");
static_assert(sizeof(ProjectMacroAutomationPayloadHeader) == 8,
              "Unexpected ProjectMacroAutomationPayloadHeader size");
static_assert(std::is_trivially_copyable_v<ProjectMacroAutomationPayloadHeader>,
              "ProjectMacroAutomationPayloadHeader must remain trivially copyable");
static_assert(std::is_trivially_copyable_v<ProjectMacroAutomationEntryPayload>,
              "ProjectMacroAutomationEntryPayload must remain trivially copyable");

}  // namespace core::persistence::project_snapshot_codec

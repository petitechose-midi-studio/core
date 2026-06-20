#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/macro/MacroPagesState.hpp"

namespace core::persistence::project_snapshot_codec {

inline constexpr uint8_t PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR = 1;
inline constexpr uint8_t PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR = 1;

#pragma pack(push, 1)
struct ProjectMacroStatePayload {
    uint8_t activeTrack = 0;
    uint8_t reserved0 = 0;
    uint16_t trackEnabledMask =
        core::state::macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;
    std::array<core::state::macro::MacroTrackData, core::state::macro::TRACK_COUNT> tracks{};
};
#pragma pack(pop)

static_assert(std::is_trivially_copyable_v<ProjectMacroStatePayload>,
              "ProjectMacroStatePayload must remain trivially copyable");
static_assert(sizeof(ProjectMacroStatePayload) == 14404,
              "Unexpected ProjectMacroStatePayload size");

}  // namespace core::persistence::project_snapshot_codec

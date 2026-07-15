#pragma once

#include <cstdint>
#include <type_traits>

namespace core::state::modulation {

inline constexpr uint8_t PROJECT_MODULATION_TRACK_COUNT = 16;
inline constexpr uint8_t PROJECT_MODULATION_PAGE_COUNT = 16;
inline constexpr uint8_t PROJECT_MODULATION_MACRO_COUNT = 8;
inline constexpr uint16_t PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY =
    PROJECT_MODULATION_TRACK_COUNT * PROJECT_MODULATION_MACRO_COUNT;

enum class ModulationDestinationKind : uint8_t {
    MACRO_SLOT = 0,
};

/** Persisted logical destination. Physical MIDI routing remains downstream. */
struct ModulationDestination {
    ModulationDestinationKind kind = ModulationDestinationKind::MACRO_SLOT;
    uint8_t track = 0;
    uint8_t page = 0;
    uint8_t macro = 0;
};

constexpr bool modulationDestinationValid(const ModulationDestination& destination) {
    return destination.kind == ModulationDestinationKind::MACRO_SLOT &&
           destination.track < PROJECT_MODULATION_TRACK_COUNT &&
           destination.page < PROJECT_MODULATION_PAGE_COUNT &&
           destination.macro < PROJECT_MODULATION_MACRO_COUNT;
}

constexpr bool operator==(const ModulationDestination& lhs,
                          const ModulationDestination& rhs) {
    return lhs.kind == rhs.kind &&
           lhs.track == rhs.track &&
           lhs.page == rhs.page &&
           lhs.macro == rhs.macro;
}

constexpr bool operator!=(const ModulationDestination& lhs,
                          const ModulationDestination& rhs) {
    return !(lhs == rhs);
}

constexpr uint16_t modulationDestinationStableAddress(
    const ModulationDestination& destination
) {
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(destination.track) * PROJECT_MODULATION_PAGE_COUNT +
         destination.page) * PROJECT_MODULATION_MACRO_COUNT +
        destination.macro
    );
}

static_assert(sizeof(ModulationDestination) == 4U);
static_assert(std::is_trivially_copyable_v<ModulationDestination>);

}  // namespace core::state::modulation

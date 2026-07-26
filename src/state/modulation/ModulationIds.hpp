#pragma once

#include <cstdint>
#include <type_traits>

namespace core::state::modulation {

struct ModulatorId {
    uint32_t value = 0;
};

struct ModulationBindingId {
    uint32_t value = 0;
};

struct ProjectCurveId {
    uint32_t value = 0;
};

constexpr bool valid(ModulatorId id) { return id.value != 0U; }
constexpr bool valid(ModulationBindingId id) { return id.value != 0U; }
constexpr bool valid(ProjectCurveId id) { return id.value != 0U; }

constexpr bool operator==(ModulatorId lhs, ModulatorId rhs) {
    return lhs.value == rhs.value;
}
constexpr bool operator!=(ModulatorId lhs, ModulatorId rhs) {
    return !(lhs == rhs);
}
constexpr bool operator==(ModulationBindingId lhs, ModulationBindingId rhs) {
    return lhs.value == rhs.value;
}
constexpr bool operator!=(ModulationBindingId lhs, ModulationBindingId rhs) {
    return !(lhs == rhs);
}
constexpr bool operator==(ProjectCurveId lhs, ProjectCurveId rhs) {
    return lhs.value == rhs.value;
}
constexpr bool operator!=(ProjectCurveId lhs, ProjectCurveId rhs) {
    return !(lhs == rhs);
}

static_assert(sizeof(ModulatorId) == sizeof(uint32_t));
static_assert(sizeof(ModulationBindingId) == sizeof(uint32_t));
static_assert(sizeof(ProjectCurveId) == sizeof(uint32_t));
static_assert(std::is_trivially_copyable_v<ModulatorId>);
static_assert(std::is_trivially_copyable_v<ModulationBindingId>);
static_assert(std::is_trivially_copyable_v<ProjectCurveId>);

}  // namespace core::state::modulation

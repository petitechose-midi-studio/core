#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/modulation/ModulationDestination.hpp"
#include "state/modulation/ModulationIds.hpp"

namespace core::state::modulation {

inline constexpr uint16_t PROJECT_CURVE_POINT_CAPACITY = 32768;
inline constexpr uint16_t PROJECT_CURVE_LIVE_CAPACITY = 256;
inline constexpr uint16_t PROJECT_CURVE_RECORD_CAPACITY = 320;
inline constexpr uint16_t PROJECT_AUTOMATION_ENTRY_CAPACITY = 128;

enum class ProjectCurveInterpolation : uint8_t {
    LINEAR = 0,
};

enum class ProjectCurveValueDomain : uint8_t {
    ABSOLUTE_UNIPOLAR = 0,
    BIPOLAR,
};

enum class ProjectCurveOrigin : uint8_t {
    NATIVE = 0,
    CONVERTED_MEAN,
    CONVERTED_FIRST,
    CONVERTED_MIN,
};

struct ProjectPackedCurvePoint {
    uint16_t tick = 0;
    int16_t value = 0;
};

/** Immutable while shared. A unique owner may replace its payload atomically. */
struct ProjectCurveRecord {
    ProjectCurveId id{};
    uint16_t pointOffset = 0;
    uint16_t pointCount = 0;
    uint16_t sourceDurationTicks = 1;
    uint16_t durationTicks = 1;
    uint16_t windowOffsetTicks = 0;
    uint16_t referenceCount = 0;
    ProjectCurveInterpolation interpolation = ProjectCurveInterpolation::LINEAR;
    ProjectCurveValueDomain valueDomain = ProjectCurveValueDomain::BIPOLAR;
    uint8_t flags = 0;
    ProjectCurveOrigin origin = ProjectCurveOrigin::NATIVE;
};

struct ProjectCurveArena {
    uint32_t nextCurveId = 1;
    uint16_t recordCount = 0;
    uint16_t pointCount = 0;
    std::array<ProjectCurveRecord, PROJECT_CURVE_RECORD_CAPACITY> records{};
    std::array<ProjectPackedCurvePoint, PROJECT_CURVE_POINT_CAPACITY> points{};

    void clear() {
        nextCurveId = 1;
        recordCount = 0;
        pointCount = 0;
        records.fill(ProjectCurveRecord{});
        points.fill(ProjectPackedCurvePoint{});
    }
};

/** Reserved now so the shared arena sizing cannot regress during MAUT lift. */
inline constexpr uint8_t PROJECT_AUTOMATION_CURVE_FLAG_ENABLED = 0x01U;

struct ProjectAutomationCurveEntry {
    ModulationDestination destination{};
    ProjectCurveId curveId{};
    uint8_t flags = 0;
    std::array<uint8_t, 3> reserved{};
};

struct ProjectAutomationCurveDirectory {
    uint16_t entryCount = 0;
    uint16_t reserved = 0;
    std::array<
        ProjectAutomationCurveEntry,
        PROJECT_AUTOMATION_ENTRY_CAPACITY
    > entries{};

    void clear() {
        entryCount = 0;
        reserved = 0;
        entries.fill(ProjectAutomationCurveEntry{});
    }
};

struct ProjectCurveSpec {
    uint16_t sourceDurationTicks = 1;
    uint16_t durationTicks = 1;
    uint16_t windowOffsetTicks = 0;
    ProjectCurveInterpolation interpolation = ProjectCurveInterpolation::LINEAR;
    ProjectCurveValueDomain valueDomain = ProjectCurveValueDomain::BIPOLAR;
    ProjectCurveOrigin origin = ProjectCurveOrigin::NATIVE;
};

static_assert(sizeof(ProjectPackedCurvePoint) == 4U);
static_assert(sizeof(ProjectCurveRecord) == 20U);
static_assert(sizeof(ProjectCurveArena) == 137480U);
static_assert(sizeof(ProjectAutomationCurveEntry) == 12U);
static_assert(sizeof(ProjectAutomationCurveDirectory) == 1540U);
static_assert(std::is_trivially_copyable_v<ProjectPackedCurvePoint>);
static_assert(std::is_trivially_copyable_v<ProjectCurveRecord>);
static_assert(std::is_trivially_copyable_v<ProjectCurveArena>);
static_assert(std::is_trivially_copyable_v<ProjectAutomationCurveEntry>);

}  // namespace core::state::modulation

#pragma once

#include <array>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/modulation/ProjectRecordedShapeTake.hpp"

namespace core::state::modulation {

enum class ProjectRecordedShapeCaptureMode : uint8_t {
    NONE = 0U,
    CREATE_UNASSIGNED,
    CREATE_ASSIGNED,
    REPLACE_EXISTING,
};

enum class ProjectRecordedShapeCaptureStatus : uint8_t {
    IDLE = 0U,
    ARMED,
    RECORDING,
    REDUCED,
    COMMITTED,
    NO_CHANGE,
    CANCELLED,
    INVALIDATED,
    SCRATCH_UNAVAILABLE,
    COMMIT_FAILED,
};

enum class ProjectRecordedShapeCaptureClock : uint8_t {
    MONOTONIC_FALLBACK = 0U,
    PROJECT_TRANSPORT,
};

/**
 * One allocation-free live-audition projection.
 *
 * CREATE_ASSIGNED identifies its provisional edge explicitly. A source edit
 * identifies the stable Project source instead, so a runtime adapter can
 * project the same temporary value through every existing destination.
 */
struct ProjectRecordedShapeAuditionDescriptor {
    ProjectRecordedShapeCaptureMode mode =
        ProjectRecordedShapeCaptureMode::NONE;
    ModulatorId sourceId{};
    ModulationDestination destination{};
    uint32_t authoredRevision = 0U;
    int16_t sourceValueQ15 = 0;
    int16_t amountQ15 = 0;
    uint16_t positionQ16 = 0U;
};

using PublishProjectRecordedShapeAuditionFn = void (*)(
    void* context,
    const ProjectRecordedShapeAuditionDescriptor& descriptor
);
using ClearProjectRecordedShapeAuditionFn = void (*)(void* context);

/**
 * Shared, session-only capture state owned by MacroUiState in PSRAM.
 *
 * The two large scratch owners are allocated lazily in external RAM and kept
 * for reuse. No curve-sized buffer is embedded in ordinary RAM. The remaining
 * members are cold transaction facts used to fail closed if the authored
 * Project or transport changes while a gesture is active.
 */
struct ProjectRecordedShapeCaptureState {
    static constexpr uint16_t PACKED_POINT_CAPACITY =
        ProjectRecordedShapeTake::SAMPLE_CAPACITY;

    core::app::ExtmemUniquePtr<ProjectRecordedShapeTake> take{};
    core::app::ExtmemUniqueArray<ProjectPackedCurvePoint> packedPoints{};

    core::state::macro::MacroAutomationSlotAddress address{};
    core::state::macro::MacroDestinationActivationPlan destinationPlan{};
    ModulatorSourceState expectedSource{};
    ProjectCurveRecord expectedCurve{};
    ModulatorId sourceId{};
    ModulationDestination destination{};

    std::array<char, PROJECT_MODULATOR_NAME_CAPACITY> sourceName{};
    uint32_t authoredRevision = 0U;
    uint32_t startedAtMs = 0U;
    uint32_t startedMusicalTick = 0U;
    uint32_t transportGeneration = 0U;
    uint32_t activationMusicalTick = 0U;
    uint64_t expectedCurvePointHash = 14695981039346656037ULL;
    /** Monotone cold revision mirrored by MacroUiState at handler boundaries. */
    uint32_t revision = 0U;
    uint32_t publishedScratchRevision = 0U;
    int32_t rawEncoderOrigin = 0;
    int32_t rawEncoderAppliedQ15 = 0;
    int16_t amountQ15 = 0;
    uint16_t durationTicks = 0U;
    uint16_t expectedSourceIndex = 0U;
    uint16_t expectedCurveIndex = 0U;
    uint16_t publishedPositionQ16 = 0U;
    uint8_t accent = 0U;
    ProjectRecordedShapeCaptureMode mode =
        ProjectRecordedShapeCaptureMode::NONE;
    ProjectRecordedShapeCaptureStatus status =
        ProjectRecordedShapeCaptureStatus::IDLE;
    ProjectRecordedShapeCaptureClock clock =
        ProjectRecordedShapeCaptureClock::MONOTONIC_FALLBACK;
    ProjectModulationStatus lastProjectStatus =
        ProjectModulationStatus::NO_CHANGE;
    bool enabled = true;
    bool expectedCurvePointHashValid = false;
    bool auditionPublished = false;
    bool rawEncoderConfigured = false;
    bool publishedPositionValid = false;

    void* auditionContext = nullptr;
    PublishProjectRecordedShapeAuditionFn publishAudition = nullptr;
    ClearProjectRecordedShapeAuditionFn clearAudition = nullptr;

    [[nodiscard]] bool ensureScratch();
    [[nodiscard]] bool active() const {
        return mode != ProjectRecordedShapeCaptureMode::NONE;
    }
    void publishStatus(ProjectRecordedShapeCaptureStatus next);
    void clearPublishedAudition();
    void endSession(ProjectRecordedShapeCaptureStatus terminal,
                    ProjectModulationStatus projectStatus);
    void reset();
};

static_assert(sizeof(ProjectRecordedShapeAuditionDescriptor) <= 32U);
// Curve-sized take and packed points remain lazy PSRAM allocations outside
// this compact transaction descriptor.
static_assert(sizeof(ProjectRecordedShapeCaptureState) <= 272U);

}  // namespace core::state::modulation

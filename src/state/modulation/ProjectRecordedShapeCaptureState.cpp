#include "state/modulation/ProjectRecordedShapeCaptureState.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

namespace core::state::modulation {

FLASHMEM bool ProjectRecordedShapeCaptureState::ensureScratch() {
    if (!take) {
        take = core::app::makeExtmemUnique<ProjectRecordedShapeTake>();
    }
    if (!packedPoints) {
        packedPoints = core::app::makeExtmemUniqueArrayForOverwrite<
            ProjectPackedCurvePoint
        >(PACKED_POINT_CAPACITY);
    }
    if (take && packedPoints) return true;
    take.reset();
    packedPoints.reset();
    publishStatus(ProjectRecordedShapeCaptureStatus::SCRATCH_UNAVAILABLE);
    lastProjectStatus = ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED;
    return false;
}

FLASHMEM void ProjectRecordedShapeCaptureState::publishStatus(
    ProjectRecordedShapeCaptureStatus next
) {
    const uint32_t scratchRevision = take ? take->scratchCurveRevision : 0U;
    uint16_t positionQ16 = 0U;
    const bool positionValid = take && take->writePositionQ16(positionQ16);
    if (status == next && publishedScratchRevision == scratchRevision &&
        publishedPositionValid == positionValid &&
        (!positionValid || publishedPositionQ16 == positionQ16)) {
        return;
    }
    status = next;
    publishedScratchRevision = scratchRevision;
    publishedPositionQ16 = positionQ16;
    publishedPositionValid = positionValid;
    ++revision;
    if (revision == 0U) revision = 1U;
}

FLASHMEM void ProjectRecordedShapeCaptureState::clearPublishedAudition() {
    if (auditionPublished && clearAudition != nullptr) {
        clearAudition(auditionContext);
    }
    auditionPublished = false;
}

FLASHMEM void ProjectRecordedShapeCaptureState::endSession(
    ProjectRecordedShapeCaptureStatus terminal,
    ProjectModulationStatus projectStatus
) {
    clearPublishedAudition();
    if (take) take->reset();
    address = {};
    destinationPlan = {};
    expectedSource = {};
    expectedCurve = {};
    sourceId = {};
    destination = {};
    sourceName.fill('\0');
    authoredRevision = 0U;
    startedAtMs = 0U;
    startedMusicalTick = 0U;
    transportGeneration = 0U;
    activationMusicalTick = 0U;
    expectedCurvePointHash = 14695981039346656037ULL;
    rawEncoderOrigin = 0;
    rawEncoderAppliedQ15 = 0;
    amountQ15 = 0;
    durationTicks = 0U;
    expectedSourceIndex = 0U;
    expectedCurveIndex = 0U;
    publishedScratchRevision = 0U;
    publishedPositionQ16 = 0U;
    accent = 0U;
    mode = ProjectRecordedShapeCaptureMode::NONE;
    clock = ProjectRecordedShapeCaptureClock::MONOTONIC_FALLBACK;
    enabled = true;
    expectedCurvePointHashValid = false;
    rawEncoderConfigured = false;
    publishedPositionValid = false;
    auditionContext = nullptr;
    publishAudition = nullptr;
    clearAudition = nullptr;
    lastProjectStatus = projectStatus;
    publishStatus(terminal);
}

FLASHMEM void ProjectRecordedShapeCaptureState::reset() {
    endSession(
        ProjectRecordedShapeCaptureStatus::IDLE,
        ProjectModulationStatus::NO_CHANGE
    );
}

}  // namespace core::state::modulation

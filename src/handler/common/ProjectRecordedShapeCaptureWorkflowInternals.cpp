#include "handler/common/ProjectRecordedShapeCaptureWorkflowInternals.hpp"

#include <cmath>
#include <cstddef>

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"

namespace core::handler::recorded_shape_capture_detail {

namespace modulation = core::state::modulation;
namespace macro = core::state::macro;

FLASHMEM void markProjectMutatedFromCoreState(void* context) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state != nullptr) state->markProjectMutated();
}

FLASHMEM bool sameSource(
    const modulation::ModulatorSourceState& lhs,
    const modulation::ModulatorSourceState& rhs
) {
    return lhs.id == rhs.id && lhs.name == rhs.name &&
        lhs.kind == rhs.kind && lhs.flags == rhs.flags &&
        lhs.accent == rhs.accent && lhs.schemaVersion == rhs.schemaVersion &&
        lhs.parameters.raw == rhs.parameters.raw;
}

FLASHMEM bool sameCurve(
    const modulation::ProjectCurveRecord& lhs,
    const modulation::ProjectCurveRecord& rhs
) {
    return lhs.id == rhs.id && lhs.pointOffset == rhs.pointOffset &&
        lhs.pointCount == rhs.pointCount &&
        lhs.sourceDurationTicks == rhs.sourceDurationTicks &&
        lhs.durationTicks == rhs.durationTicks &&
        lhs.windowOffsetTicks == rhs.windowOffsetTicks &&
        lhs.referenceCount == rhs.referenceCount &&
        lhs.interpolation == rhs.interpolation &&
        lhs.valueDomain == rhs.valueDomain && lhs.flags == rhs.flags &&
        lhs.origin == rhs.origin;
}

FLASHMEM bool sameActivationPlan(
    const macro::MacroDestinationActivationPlan& lhs,
    const macro::MacroDestinationActivationPlan& rhs
) {
    return lhs.address.track == rhs.address.track &&
        lhs.address.page == rhs.address.page &&
        lhs.address.macro == rhs.address.macro &&
        lhs.expectedTrackEnabledMask == rhs.expectedTrackEnabledMask &&
        lhs.expectedPageEnabledMask == rhs.expectedPageEnabledMask &&
        lhs.createTrack == rhs.createTrack &&
        lhs.createPage == rhs.createPage &&
        lhs.createMacro == rhs.createMacro && lhs.valid == rhs.valid;
}

FLASHMEM modulation::ProjectCurveSpec curveSpec(
    const modulation::ProjectCurveRecord& record
) {
    return {
        .sourceDurationTicks = record.sourceDurationTicks,
        .durationTicks = record.durationTicks,
        .windowOffsetTicks = record.windowOffsetTicks,
        .interpolation = record.interpolation,
        .valueDomain = record.valueDomain,
        .origin = record.origin,
    };
}

FLASHMEM const modulation::ProjectPackedCurvePoint* curvePoints(
    const modulation::ProjectCurveArena& arena,
    const modulation::ProjectCurveRecord& record
) {
    const uint32_t end = static_cast<uint32_t>(record.pointOffset) +
        record.pointCount;
    if (record.pointCount == 0U || end > arena.pointCount ||
        end > modulation::PROJECT_CURVE_POINT_CAPACITY) {
        return nullptr;
    }
    return arena.points.data() + record.pointOffset;
}

FLASHMEM bool pointHash(
    const modulation::ProjectPackedCurvePoint* points,
    uint16_t count,
    uint64_t& out
) {
    if (points == nullptr || count == 0U) return false;
    uint64_t hash = 14695981039346656037ULL;
    const auto* bytes = reinterpret_cast<const uint8_t*>(points);
    const size_t size = static_cast<size_t>(count) * sizeof(points[0U]);
    for (size_t index = 0U; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    out = hash;
    return true;
}

FLASHMEM modulation::ProjectModulationResult resultWith(
    modulation::ProjectModulationStatus status
) {
    modulation::ProjectModulationResult result{};
    result.status = status;
    return result;
}

FLASHMEM float validTempo(float value) {
    return std::isfinite(value) && value > 0.0F ? value : 120.0F;
}

}  // namespace core::handler::recorded_shape_capture_detail

#include "state/modulation/ProjectControlMacroOps.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::modulation {

FLASHMEM bool readProjectControlCurvePoint(
    const ProjectControlState& control,
    ProjectCurveId curveId,
    uint16_t pointIndex,
    bool signedOutput,
    ProjectControlCurvePoint& out
) {
    const auto* record = findProjectCurve(control.authored.curves, curveId);
    if (record == nullptr || pointIndex >= record->pointCount) return false;
    const auto& point = control.authored.curves.points[
        static_cast<uint16_t>(record->pointOffset + pointIndex)
    ];
    out.beat = static_cast<float>(point.tick) /
        static_cast<float>(PROJECT_CONTROL_TICKS_PER_BEAT);
    const float unpacked = static_cast<float>(point.value) / 32767.0f;
    out.value = signedOutput
        ? std::clamp(unpacked, -1.0f, 1.0f)
        : std::clamp(unpacked, 0.0f, 1.0f);
    return true;
}

FLASHMEM ProjectControlCurveWindowSummary
projectControlCurveWindowSummary(
    const ProjectControlState& control,
    ProjectCurveId curveId
) {
    ProjectControlCurveWindowSummary summary{};
    const auto* record = findProjectCurve(control.authored.curves, curveId);
    if (record == nullptr || record->pointCount == 0U) return summary;
    summary.active = true;
    summary.sourceDurationTicks = record->sourceDurationTicks;
    summary.durationTicks = record->durationTicks;
    summary.windowOffsetTicks = record->windowOffsetTicks;
    summary.firstPointTick = control.authored.curves.points[
        record->pointOffset
    ].tick;
    summary.lastPointTick = control.authored.curves.points[
        static_cast<uint16_t>(record->pointOffset + record->pointCount - 1U)
    ].tick;
    summary.pointCount = record->pointCount;
    summary.wraps = static_cast<uint32_t>(record->windowOffsetTicks) +
        record->durationTicks > record->sourceDurationTicks;
    return summary;
}

namespace {

FLASHMEM float evaluateProjectControlCurveRecordImpl(
    const ProjectControlState& control,
    const ProjectCurveRecord& record,
    float elapsedBeat,
    float fallback
) {
    if (record.pointCount == 0U ||
        static_cast<uint32_t>(record.pointOffset) + record.pointCount >
            control.authored.curves.pointCount) {
        return fallback;
    }

    const float safeBeat = std::isfinite(elapsedBeat)
        ? std::max(elapsedBeat, 0.0f)
        : 0.0f;
    const float elapsedTicks = safeBeat *
        static_cast<float>(PROJECT_CONTROL_TICKS_PER_BEAT);
    const uint32_t elapsedWhole = static_cast<uint32_t>(std::min<double>(
        std::floor(elapsedTicks),
        static_cast<double>(std::numeric_limits<uint32_t>::max())
    ));
    const float elapsedFraction = elapsedTicks - std::floor(elapsedTicks);
    const uint16_t duration = std::max<uint16_t>(record.durationTicks, 1U);
    const uint16_t sourceDuration = std::max<uint16_t>(
        record.sourceDurationTicks,
        1U
    );
    float sourceTick = static_cast<float>(
        (static_cast<uint32_t>(record.windowOffsetTicks) +
         (elapsedWhole % duration)) % sourceDuration
    ) + elapsedFraction;
    if (sourceTick >= sourceDuration) sourceTick -= sourceDuration;

    const auto unpack = [&record](int16_t packed) {
        const float value = static_cast<float>(packed) / 32767.0f;
        return record.valueDomain == ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR
            ? std::clamp(value, 0.0f, 1.0f)
            : std::clamp(value, -1.0f, 1.0f);
    };
    const auto& arena = control.authored.curves;
    const uint16_t firstIndex = record.pointOffset;
    const uint16_t lastIndex = static_cast<uint16_t>(
        record.pointOffset + record.pointCount - 1U
    );
    const auto& first = arena.points[firstIndex];
    if (record.pointCount == 1U || sourceTick <= first.tick) {
        return unpack(first.value);
    }
    const auto& last = arena.points[lastIndex];
    if (sourceTick >= last.tick) return unpack(last.value);

    uint16_t low = 1U;
    uint16_t high = record.pointCount;
    while (low < high) {
        const uint16_t middle = static_cast<uint16_t>(
            low + (high - low) / 2U
        );
        if (arena.points[record.pointOffset + middle].tick < sourceTick) {
            low = static_cast<uint16_t>(middle + 1U);
        } else {
            high = middle;
        }
    }
    const auto& right = arena.points[record.pointOffset + low];
    const auto& left = arena.points[record.pointOffset + low - 1U];
    const float leftValue = unpack(left.value);
    const float rightValue = unpack(right.value);
    const uint16_t span = static_cast<uint16_t>(right.tick - left.tick);
    if (span == 0U) return rightValue;
    const float alpha = std::clamp(
        (sourceTick - left.tick) / static_cast<float>(span),
        0.0f,
        1.0f
    );
    return leftValue + (rightValue - leftValue) * alpha;
}

}  // namespace

FLASHMEM float evaluateProjectControlCurve(
    const ProjectControlState& control,
    ProjectCurveId curveId,
    float elapsedBeat,
    float fallback
) {
    const auto* record = findProjectCurve(control.authored.curves, curveId);
    return record == nullptr
        ? fallback
        : evaluateProjectControlCurveRecordImpl(
            control,
            *record,
            elapsedBeat,
            fallback
        );
}

FLASHMEM float evaluateProjectControlCurveRecord(
    const ProjectControlState& control,
    ProjectCurveId curveId,
    uint16_t recordIndex,
    float elapsedBeat,
    float fallback
) {
    if (recordIndex < control.authored.curves.recordCount) {
        const auto& record = control.authored.curves.records[recordIndex];
        if (record.id == curveId) {
            return evaluateProjectControlCurveRecordImpl(
                control,
                record,
                elapsedBeat,
                fallback
            );
        }
    }
    return evaluateProjectControlCurve(
        control,
        curveId,
        elapsedBeat,
        fallback
    );
}

}  // namespace core::state::modulation

#include "ui/modulation/ModulatorSparklineModel.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "ui/modulation/ModulatorAdsrUiModel.hpp"

namespace core::ui::modulation::sparkline {
namespace {

using namespace core::state::modulation;
namespace adsr_ui = core::ui::modulation::adsr;

constexpr uint32_t FNV_OFFSET_BASIS = 2166136261U;
constexpr uint32_t FNV_PRIME = 16777619U;

void hashByte(uint32_t& hash, uint8_t value) {
    hash = (hash ^ value) * FNV_PRIME;
}

FLASHMEM void hashU16(uint32_t& hash, uint16_t value) {
    hashByte(hash, static_cast<uint8_t>(value));
    hashByte(hash, static_cast<uint8_t>(value >> 8U));
}

void hashU32(uint32_t& hash, uint32_t value) {
    hashU16(hash, static_cast<uint16_t>(value));
    hashU16(hash, static_cast<uint16_t>(value >> 16U));
}

FLASHMEM uint16_t quantize(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<uint16_t>(clamped * 65535.0f + 0.5f);
}

const ModulatorSourceState* sourceFor(
    const ms::ui::KeyValueSparkline& descriptor
) {
    const auto* control = static_cast<const ProjectControlState*>(
        descriptor.context
    );
    if (!control) return nullptr;
    return findProjectModulator(
        control->authored.modulation,
        ModulatorId{descriptor.identity}
    );
}

FLASHMEM bool sourceUsesPositiveDomain(
    const ProjectControlState& control,
    const ModulatorSourceState& source
) {
    if (source.kind == ModulatorKind::ADSR) return true;
    if (source.kind == ModulatorKind::LFO) return false;
    const auto* curve = findProjectCurve(
        control.authored.curves,
        source.parameters.recordedCurveId
    );
    return curve != nullptr && curve->valueDomain ==
        ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
}

FLASHMEM uint32_t sourceGeometryRevisionImpl(
    const ProjectControlState& control,
    const ModulatorSourceState& source
) {
    uint32_t hash = FNV_OFFSET_BASIS;
    hashU32(hash, source.id.value);
    hashByte(hash, static_cast<uint8_t>(source.kind));
    if (source.kind == ModulatorKind::LFO) {
        hashByte(hash, static_cast<uint8_t>(source.parameters.lfo.shape));
        hashU16(hash, static_cast<uint16_t>(source.parameters.lfo.phaseQ15));
    } else if (source.kind == ModulatorKind::ADSR) {
        const auto& adsr = source.parameters.adsr;
        hashU16(hash, adsr.delay);
        hashU16(hash, adsr.attack);
        hashU16(hash, adsr.hold);
        hashU16(hash, adsr.decay);
        hashU16(hash, adsr.release);
        hashU16(hash, adsr.sustainQ15);
        hashU16(hash, adsr.smooth);
        // Retrigger changes runtime behaviour, not the authored silhouette.
        hashU16(hash, static_cast<uint16_t>(
            adsr.traits & ~PROJECT_MODULATOR_ADSR_RETRIGGER_MASK
        ));
    } else if (source.kind == ModulatorKind::RECORDED_SHAPE) {
        const auto* curve = findProjectCurve(
            control.authored.curves,
            source.parameters.recordedCurveId
        );
        hashU32(hash, source.parameters.recordedCurveId.value);
        if (curve == nullptr ||
            static_cast<uint32_t>(curve->pointOffset) + curve->pointCount >
                control.authored.curves.pointCount) {
            return hash;
        }
        hashU16(hash, curve->pointCount);
        hashU16(hash, curve->sourceDurationTicks);
        hashU16(hash, curve->durationTicks);
        hashU16(hash, curve->windowOffsetTicks);
        hashByte(hash, static_cast<uint8_t>(curve->interpolation));
        hashByte(hash, static_cast<uint8_t>(curve->valueDomain));
        hashByte(hash, curve->flags);
        hashByte(hash, static_cast<uint8_t>(curve->origin));
        for (uint16_t index = 0U; index < curve->pointCount; ++index) {
            const auto& point = control.authored.curves.points[
                static_cast<uint16_t>(curve->pointOffset + index)
            ];
            hashU16(hash, point.tick);
            hashU16(hash, static_cast<uint16_t>(point.value));
        }
    }
    return hash == 0U ? 1U : hash;
}

bool sampleSource(
    const ms::ui::KeyValueSparkline& descriptor,
    uint16_t positionQ16,
    uint16_t previousPositionQ16,
    bool hasPrevious,
    ms::ui::KeyValueSparklineSample& out
) {
    out = {};
    const auto* control = static_cast<const ProjectControlState*>(
        descriptor.context
    );
    const auto* source = sourceFor(descriptor);
    if (!control || !source) return false;

    float value = 0.0f;
    bool positive = false;
    if (source->kind == ModulatorKind::LFO) {
        const uint16_t phaseQ16 = projectLfoShapePositionQ16(
            positionQ16,
            source->parameters.lfo.phaseQ15
        );
        value = evaluateProjectLfoShape(
            source->parameters.lfo.shape,
            static_cast<float>(phaseQ16) / 65535.0f
        );
        if (hasPrevious && source->parameters.lfo.shape ==
                ModulatorLfoShape::SQUARE) {
            const uint16_t previousPhaseQ16 = projectLfoShapePositionQ16(
                previousPositionQ16,
                source->parameters.lfo.phaseQ15
            );
            out.discontinuityBefore = phaseQ16 < previousPhaseQ16 ||
                (previousPhaseQ16 < 32768U && phaseQ16 >= 32768U);
        }
    } else if (source->kind == ModulatorKind::ADSR) {
        positive = true;
        value = adsr_ui::previewValue(
            source->parameters.adsr,
            adsr_ui::previewBoundaries(source->parameters.adsr),
            positionQ16
        );
    } else if (source->kind == ModulatorKind::RECORDED_SHAPE) {
        const auto* curve = findProjectCurve(
            control->authored.curves,
            source->parameters.recordedCurveId
        );
        if (!curve || curve->pointCount == 0U) return false;
        positive = curve->valueDomain ==
            ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
        const uint32_t tick =
            (static_cast<uint32_t>(positionQ16) * curve->durationTicks) /
            65535U;
        value = evaluateProjectControlCurve(
            *control,
            source->parameters.recordedCurveId,
            static_cast<float>(tick) /
                static_cast<float>(PROJECT_CONTROL_TICKS_PER_BEAT),
            0.0f
        );
    } else {
        return false;
    }
    out.valueQ16 = quantize(positive ? value : value * 0.5f + 0.5f);
    return true;
}

bool sampleRuntimeMarker(
    const ms::ui::KeyValueSparkline& descriptor,
    uint32_t nowMs,
    ms::ui::KeyValueSparklineMarker& out
) {
    out = {};
    const auto* control = static_cast<const ProjectControlState*>(
        descriptor.context
    );
    const auto* source = sourceFor(descriptor);
    if (!control || !source) return false;
    if ((source->flags & PROJECT_MODULATOR_FLAG_ENABLED) == 0U) return true;

    const auto time = extrapolateProjectControlTime(
        control->timeTelemetry,
        nowMs
    );
    ProjectModulatorRuntimeProjection projection{};
    if (descriptor.runtimeIndex >= control->plan.sourceCount ||
        control->plan.sources[descriptor.runtimeIndex].id != source->id ||
        !projectModulatorRuntimeProjectionAtIndex(
            control->plan,
            control->authored.curves,
            control->runtime,
            time,
            descriptor.runtimeIndex,
            projection
        )) {
        return true;
    }

    uint16_t positionQ16 = projection.positionQ16;
    if (source->kind == ModulatorKind::ADSR) {
        if (!adsr_ui::runtimeMarkerPosition(
                adsr_ui::previewBoundaries(source->parameters.adsr),
                projection.adsrStage,
                projection.stageProgressQ16,
                positionQ16
            )) {
            return true;
        }
    } else if (source->kind == ModulatorKind::LFO) {
        positionQ16 = projectLfoPreviewPositionQ16(
            projection.positionQ16,
            source->parameters.lfo.phaseQ15
        );
    } else if (!projection.positionKnown) {
        return true;
    }

    const bool positive = sourceUsesPositiveDomain(*control, *source);
    out = {
        .positionQ16 = positionQ16,
        .valueQ16 = quantize(
            positive ? projection.value : projection.value * 0.5f + 0.5f
        ),
        .visible = true,
    };
    return true;
}

}  // namespace

FLASHMEM uint32_t sourceGeometryRevision(
    const ProjectControlState& control,
    const ModulatorSourceState& source
) {
    return sourceGeometryRevisionImpl(control, source);
}

FLASHMEM ms::ui::KeyValueSparkline buildSource(
    const ProjectControlState& control,
    const ModulatorSourceState& source
) {
    if (source.kind == ModulatorKind::RECORDED_SHAPE) {
        const auto* curve = findProjectCurve(
            control.authored.curves,
            source.parameters.recordedCurveId
        );
        if (!curve || curve->pointCount == 0U) return {};
    } else if (source.kind != ModulatorKind::LFO &&
               source.kind != ModulatorKind::ADSR) {
        return {};
    }
    uint16_t runtimeIndex = 0U;
    while (runtimeIndex < control.plan.sourceCount &&
           control.plan.sources[runtimeIndex].id != source.id) {
        ++runtimeIndex;
    }
    const uint16_t runtimeProjectionIndex = runtimeIndex <
            control.plan.sourceCount
        ? runtimeIndex
        : static_cast<uint16_t>(UINT16_MAX);
    return {
        .context = &control,
        .identity = source.id.value,
        .geometryRevision = sourceGeometryRevisionImpl(control, source),
        .runtimeIndex = runtimeProjectionIndex,
        .enabled = true,
        .centerLine = !sourceUsesPositiveDomain(control, source),
        .sampleProvider = &sampleSource,
        .markerProvider = &sampleRuntimeMarker,
    };
}

}  // namespace core::ui::modulation::sparkline

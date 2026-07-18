#include "ui/modulation/ModulatorSparklineModel.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "ui/modulation/ModulatorAdsrUiModel.hpp"

namespace core::ui::modulation::sparkline {
namespace {

using namespace core::state::modulation;
namespace adsr_ui = core::ui::modulation::adsr;

constexpr uint8_t SAMPLE_COUNT = static_cast<uint8_t>(
    ms::ui::KEY_VALUE_SPARKLINE_SAMPLE_COUNT
);
static_assert(SAMPLE_COUNT == 12U);

const std::array<std::array<uint8_t, SAMPLE_COUNT>, 5> LFO_SAMPLES PROGMEM{{
    {{128, 197, 244, 255, 221, 164, 91, 34, 1, 11, 58, 128}},
    {{128, 179, 230, 255, 204, 153, 102, 51, 0, 26, 77, 128}},
    {{0, 23, 46, 70, 93, 116, 139, 162, 185, 209, 232, 255}},
    {{255, 232, 209, 185, 162, 139, 116, 93, 70, 46, 23, 0}},
    {{255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0}},
}};

uint8_t quantize(float value) {
    return static_cast<uint8_t>(std::lround(
        std::clamp(value, 0.0f, 1.0f) * 255.0f
    ));
}

bool enabled(const ModulatorSourceState& source) {
    return (source.flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
}

ms::ui::KeyValueSparkline buildLfo(
    const ProjectControlState& control,
    const ModulatorSourceState& source
) {
    ms::ui::KeyValueSparkline out{};
    out.enabled = true;
    out.centerLine = true;
    out.liveMarker = enabled(source);
    out.sampleCount = SAMPLE_COUNT;
    out.liveValue = quantize(liveValue(control, source.id) * 0.5f + 0.5f);
    const auto shape = static_cast<uint8_t>(source.parameters.lfo.shape);
    out.samples = LFO_SAMPLES[std::min<uint8_t>(shape, 4U)];
    return out;
}

ms::ui::KeyValueSparkline buildAdsr(
    const ProjectControlState& control,
    const ModulatorSourceState& source
) {
    ms::ui::KeyValueSparkline out{};
    out.enabled = true;
    out.centerLine = false;
    out.sampleCount = SAMPLE_COUNT;
    const auto boundaries = adsr_ui::previewBoundaries(source.parameters.adsr);
    for (uint8_t index = 0U; index < out.sampleCount; ++index) {
        const uint16_t position = static_cast<uint16_t>(
            (static_cast<uint32_t>(index) * 65535U) /
            static_cast<uint32_t>(out.sampleCount - 1U)
        );
        out.samples[index] = quantize(adsr_ui::previewValue(
            source.parameters.adsr,
            boundaries,
            position
        ));
    }
    const auto* runtime = adsr_ui::runtimeState(control, source.id);
    out.liveMarker = enabled(source) && runtime != nullptr &&
        runtime->stage != ProjectModulationAdsrStage::IDLE;
    out.liveValue = quantize(liveValue(control, source.id));
    return out;
}

ms::ui::KeyValueSparkline buildRecorded(
    const ProjectControlState& control,
    const ModulatorSourceState& source
) {
    ms::ui::KeyValueSparkline out{};
    const auto curveId = source.parameters.recordedCurveId;
    const auto* curve = findProjectCurve(control.authored.curves, curveId);
    if (curve == nullptr || curve->pointCount == 0U) return out;

    const bool positive = curve->valueDomain ==
        ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
    const auto normalize = [positive](float value) {
        return positive ? value : value * 0.5f + 0.5f;
    };
    out.enabled = true;
    out.centerLine = !positive;
    out.liveMarker = enabled(source);
    out.liveValue = quantize(normalize(liveValue(control, source.id)));
    out.sampleCount = SAMPLE_COUNT;
    const uint16_t duration = std::max<uint16_t>(curve->durationTicks, 1U);
    for (uint8_t index = 0U; index < out.sampleCount; ++index) {
        const uint32_t tick =
            (static_cast<uint32_t>(index) * (duration - 1U)) /
            (out.sampleCount - 1U);
        const float beat = static_cast<float>(tick) /
            static_cast<float>(PROJECT_CONTROL_TICKS_PER_BEAT);
        out.samples[index] = quantize(normalize(evaluateProjectControlCurve(
            control,
            curveId,
            beat,
            0.0f
        )));
    }
    return out;
}

}  // namespace

FLASHMEM float liveValue(
    const ProjectControlState& control,
    ModulatorId sourceId
) {
    for (uint16_t index = 0U; index < control.plan.sourceCount; ++index) {
        if (control.plan.sources[index].id == sourceId) {
            return std::clamp(control.sourceScratch[index], -1.0f, 1.0f);
        }
    }
    return 0.0f;
}

FLASHMEM ms::ui::KeyValueSparkline buildSource(
    const ProjectControlState& control,
    const ModulatorSourceState& source
) {
    switch (source.kind) {
        case ModulatorKind::LFO:
            return buildLfo(control, source);
        case ModulatorKind::ADSR:
            return buildAdsr(control, source);
        case ModulatorKind::RECORDED_SHAPE:
            return buildRecorded(control, source);
        default:
            return {};
    }
}

}  // namespace core::ui::modulation::sparkline

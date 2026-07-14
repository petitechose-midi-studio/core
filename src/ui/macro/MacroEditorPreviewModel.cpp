#include "ui/macro/MacroEditorPreviewModel.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

namespace core::ui {
namespace {

uint8_t quantize01(float value) {
    const float clamped = core::state::macro::macroAutomationClamp01(value);
    return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
}

float sampleBeat(
    uint16_t timelineDurationTicks,
    size_t sampleIndex
) {
    const uint16_t durationTicks = timelineDurationTicks == 0
        ? core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
        : timelineDurationTicks;
    const uint32_t lastTick = durationTicks > 0U
        ? static_cast<uint32_t>(durationTicks - 1U)
        : 0U;
    const uint32_t tick = MACRO_EDITOR_PREVIEW_SAMPLE_COUNT > 1U
        ? (static_cast<uint32_t>(sampleIndex) * lastTick) /
              static_cast<uint32_t>(MACRO_EDITOR_PREVIEW_SAMPLE_COUNT - 1U)
        : 0U;
    return static_cast<float>(tick) /
           static_cast<float>(
               core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
           );
}

}  // namespace

FLASHMEM void buildMacroEditorPreviewModel(
    float staticBase,
    const core::state::macro::MacroAutomationSlotState* slot,
    const core::state::macro::MacroAutomationPointPool& pool,
    bool manualOverride,
    MacroEditorPreviewModel& model
) {
    model = {};
    const float fallback = core::state::macro::macroAutomationClamp01(staticBase);
    model.manualOverride = manualOverride;
    model.automationStored = slot != nullptr &&
        core::state::macro::macroCurveStored(slot->automation);
    model.modulationStored = slot != nullptr &&
        core::state::macro::macroCurveStored(slot->modulation);
    model.automationPlayback = model.automationStored &&
        core::state::macro::macroCurvePlaybackActive(slot->automation) &&
        !manualOverride;
    model.modulationPlayback = model.modulationStored &&
        core::state::macro::macroCurvePlaybackActive(slot->modulation);
    model.automationDrivingBase = model.automationPlayback;
    if (model.automationStored) {
        model.timelineDurationTicks = std::max<uint16_t>(
            model.timelineDurationTicks,
            slot->automation.durationTicks
        );
    }
    if (model.modulationStored) {
        model.timelineDurationTicks = std::max<uint16_t>(
            model.timelineDurationTicks,
            slot->modulation.durationTicks
        );
    }

    for (size_t i = 0; i < MACRO_EDITOR_PREVIEW_SAMPLE_COUNT; ++i) {
        const float beat = sampleBeat(model.timelineDurationTicks, i);
        float automation = fallback;
        if (model.automationStored) {
            automation = core::state::macro::macroAutomationEvaluate(
                slot->automation,
                pool,
                beat,
                fallback
            );
        }
        const float base = model.automationPlayback
            ? core::state::macro::macroAutomationClamp01(automation)
            : fallback;
        float storedModulation = 0.0f;
        if (model.modulationStored && slot->modulationDepth > 0.0f) {
            storedModulation = core::state::macro::macroModulationEvaluate(
                slot->modulation,
                pool,
                beat
            ) * core::state::macro::macroAutomationClamp01(slot->modulationDepth);
        }
        const float modulation = model.modulationPlayback ? storedModulation : 0.0f;
        const float rawOut = base + modulation;
        model.clippedLow = model.clippedLow || rawOut < 0.0f;
        model.clippedHigh = model.clippedHigh || rawOut > 1.0f;
        model.automation[i] = quantize01(automation);
        model.base[i] = quantize01(base);
        model.modulation[i] = static_cast<int16_t>(
            std::clamp(storedModulation, -1.0f, 1.0f) * 255.0f
        );
        model.out[i] = quantize01(rawOut);
    }
}

FLASHMEM MacroEditorPreviewModel buildMacroEditorPreviewModel(
    float staticBase,
    const core::state::macro::MacroAutomationSlotState* slot,
    const core::state::macro::MacroAutomationPointPool& pool,
    bool manualOverride
) {
    MacroEditorPreviewModel model{};
    buildMacroEditorPreviewModel(
        staticBase,
        slot,
        pool,
        manualOverride,
        model
    );
    return model;
}

}  // namespace core::ui

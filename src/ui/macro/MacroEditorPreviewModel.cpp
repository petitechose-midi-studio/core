#include "ui/macro/MacroEditorPreviewModel.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

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

int16_t sourceIndex(
    const core::state::modulation::ProjectModulationState& state,
    core::state::modulation::ModulatorId id
) {
    for (uint16_t index = 0; index < state.sourceCount; ++index) {
        if (state.sources[index].id == id) return static_cast<int16_t>(index);
    }
    return -1;
}

float projectSourcePreviewValue(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulatorSourceState& source,
    float beat,
    size_t sampleIndex
) {
    namespace modulation = core::state::modulation;
    if (source.kind == modulation::ModulatorKind::RECORDED_SHAPE) {
        return modulation::evaluateProjectControlCurve(
            control,
            source.parameters.recordedCurveId,
            beat,
            0.0f
        );
    }

    const float authoredPhase = static_cast<float>(
        source.parameters.lfo.phaseQ15
    ) / 32767.0f;
    float phase = authoredPhase;
    if (source.parameters.lfo.timing == modulation::ModulatorTimingMode::SYNC) {
        const float elapsedTicks = beat * static_cast<float>(
            core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
        );
        phase += elapsedTicks / static_cast<float>(
            std::max<uint32_t>(source.parameters.lfo.periodTicks, 1U)
        );
    } else if (MACRO_EDITOR_PREVIEW_SAMPLE_COUNT > 1U) {
        phase += static_cast<float>(sampleIndex) /
            static_cast<float>(MACRO_EDITOR_PREVIEW_SAMPLE_COUNT - 1U);
    }
    phase -= std::floor(phase);
    if (phase < 0.0f) phase += 1.0f;
    return modulation::evaluateProjectLfoShape(
        source.parameters.lfo.shape,
        phase
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
        if (model.modulationStored && slot->modulationDepth != 0.0f) {
            storedModulation = core::state::macro::macroModulationEvaluate(
                slot->modulation,
                pool,
                beat
            ) * std::clamp(slot->modulationDepth, -1.0f, 1.0f);
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

FLASHMEM void buildMacroEditorPreviewModel(
    float staticBase,
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    bool manualOverride,
    MacroEditorPreviewModel& model
) {
    namespace modulation = core::state::modulation;
    model = {};
    const float fallback = core::state::macro::macroAutomationClamp01(staticBase);
    model.manualOverride = manualOverride;

    modulation::ProjectControlMacroSlotView view{};
    if (!modulation::readProjectControlMacroSlot(control, address, view)) {
        for (size_t index = 0; index < MACRO_EDITOR_PREVIEW_SAMPLE_COUNT; ++index) {
            model.automation[index] = quantize01(fallback);
            model.base[index] = quantize01(fallback);
            model.out[index] = quantize01(fallback);
        }
        return;
    }
    model.automationStored = view.automationStored;
    model.automationPlayback = view.automationEnabled && !manualOverride;
    model.automationDrivingBase = model.automationPlayback;
    if (view.automationStored) {
        const auto* record = modulation::findProjectCurve(
            control.authored.curves,
            view.automationCurveId
        );
        if (record != nullptr) {
            model.timelineDurationTicks = std::max<uint16_t>(
                model.timelineDurationTicks,
                record->durationTicks
            );
        }
    }

    std::array<
        int16_t,
        modulation::PROJECT_MODULATION_BINDING_CAPACITY
    > bindingSourceIndices{};
    bindingSourceIndices.fill(-1);
    constexpr uint16_t PREVIEW_MAPPING_SHIFT = 8U;
    constexpr uint16_t PREVIEW_SOURCE_MASK = 0x00FFU;
    const auto destination = modulation::projectControlDestination(address);
    const auto& graph = control.authored.modulation;
    const float destinationScale = static_cast<float>(
        modulation::projectModulationDestinationScaleQ15(
            graph,
            destination
        )
    ) / static_cast<float>(
        modulation::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15
    );
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        const int16_t sourcePosition = sourceIndex(graph, binding.sourceId);
        if (sourcePosition < 0) continue;
        model.modulationStored = true;
        const auto& source = graph.sources[static_cast<uint16_t>(sourcePosition)];
        modulation::ModulatorNaturalDomain naturalDomain{};
        modulation::ResolvedModulationMapping mapping{};
        if (!modulation::projectModulatorNaturalDomain(
                source,
                control.authored.curves,
                naturalDomain
            ) ||
            !modulation::resolveModulationApplication(
                binding.application,
                naturalDomain,
                mapping
            )) {
            continue;
        }
        bindingSourceIndices[index] = static_cast<int16_t>(
            sourcePosition |
            (static_cast<uint16_t>(mapping) << PREVIEW_MAPPING_SHIFT)
        );
        const bool active =
            (binding.flags & modulation::PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U &&
            (source.flags & modulation::PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
        model.modulationPlayback = model.modulationPlayback || active;
        if (source.kind == modulation::ModulatorKind::RECORDED_SHAPE) {
            const auto* record = modulation::findProjectCurve(
                control.authored.curves,
                source.parameters.recordedCurveId
            );
            if (record != nullptr) {
                model.timelineDurationTicks = std::max<uint16_t>(
                    model.timelineDurationTicks,
                    record->durationTicks
                );
            }
        } else if (
            source.parameters.lfo.timing == modulation::ModulatorTimingMode::SYNC
        ) {
            model.timelineDurationTicks = std::max<uint16_t>(
                model.timelineDurationTicks,
                static_cast<uint16_t>(std::min<uint32_t>(
                    source.parameters.lfo.periodTicks,
                    UINT16_MAX
                ))
            );
        }
    }

    for (size_t sample = 0; sample < MACRO_EDITOR_PREVIEW_SAMPLE_COUNT; ++sample) {
        const float beat = sampleBeat(model.timelineDurationTicks, sample);
        const float automation = model.automationStored
            ? modulation::evaluateProjectControlCurve(
                control,
                view.automationCurveId,
                beat,
                fallback
            )
            : fallback;
        const float base = model.automationPlayback
            ? core::state::macro::macroAutomationClamp01(automation)
            : fallback;
        float storedModulation = 0.0f;
        float activeModulation = 0.0f;
        for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
            const int16_t sourceAndApplication = bindingSourceIndices[index];
            if (sourceAndApplication < 0) continue;
            const uint16_t sourcePosition = static_cast<uint16_t>(
                sourceAndApplication & PREVIEW_SOURCE_MASK
            );
            const auto& binding = graph.outputBindings[index];
            const auto& source = graph.sources[
                sourcePosition
            ];
            float sourceValue = projectSourcePreviewValue(
                control,
                source,
                beat,
                sample
            );
            const auto mapping = static_cast<
                modulation::ResolvedModulationMapping
            >(
                static_cast<uint16_t>(sourceAndApplication) >>
                PREVIEW_MAPPING_SHIFT
            );
            sourceValue = modulation::applyResolvedModulationMapping(
                sourceValue,
                mapping
            );
            const float contribution = sourceValue *
                (static_cast<float>(binding.amountQ15) / 32767.0f);
            storedModulation += contribution;
            if ((binding.flags &
                 modulation::PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U &&
                (source.flags & modulation::PROJECT_MODULATOR_FLAG_ENABLED) != 0U) {
                activeModulation += contribution;
            }
        }
        storedModulation *= destinationScale;
        activeModulation *= destinationScale;
        const float rawOut = base + activeModulation;
        model.clippedLow = model.clippedLow || rawOut < 0.0f;
        model.clippedHigh = model.clippedHigh || rawOut > 1.0f;
        model.automation[sample] = quantize01(automation);
        model.base[sample] = quantize01(base);
        model.modulation[sample] = static_cast<int16_t>(
            std::clamp(storedModulation, -1.0f, 1.0f) * 255.0f
        );
        model.out[sample] = quantize01(rawOut);
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

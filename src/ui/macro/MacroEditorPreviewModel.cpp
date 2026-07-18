#include "ui/macro/MacroEditorPreviewModel.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::ui {
namespace {

namespace macro = core::state::macro;
namespace modulation = core::state::modulation;

struct ContributionSample {
    float stored = 0.0f;
    float active = 0.0f;
    bool discontinuityBefore = false;
};

struct AdsrPreviewBoundaries {
    uint16_t attackEndQ16 = 0U;
    uint16_t decayEndQ16 = 0U;
    uint16_t sustainEndQ16 = 0U;
};

AdsrPreviewBoundaries adsrPreviewBoundaries(
    const modulation::ModulatorAdsrParameters& parameters
) {
    uint32_t attack = parameters.attack;
    uint32_t decay = parameters.decay;
    uint32_t release = parameters.release;
    const uint32_t moving = attack + decay + release;
    uint32_t sustain = std::max<uint32_t>(1U, moving / 4U);
    if (moving == 0U) {
        attack = decay = release = sustain = 1U;
    }
    const uint64_t total = attack + decay + sustain + release;
    return {
        .attackEndQ16 = static_cast<uint16_t>(attack * 65535ULL / total),
        .decayEndQ16 = static_cast<uint16_t>(
            (attack + decay) * 65535ULL / total
        ),
        .sustainEndQ16 = static_cast<uint16_t>(
            (attack + decay + sustain) * 65535ULL / total
        ),
    };
}

float adsrSegmentProgress(uint16_t position, uint16_t begin, uint16_t end) {
    if (end <= begin) return 1.0f;
    return std::clamp(
        static_cast<float>(position - begin) /
            static_cast<float>(end - begin),
        0.0f,
        1.0f
    );
}

float adsrPreviewValue(
    const modulation::ModulatorAdsrParameters& parameters,
    uint16_t positionQ16
) {
    const auto boundaries = adsrPreviewBoundaries(parameters);
    const float sustain = std::clamp(
        static_cast<float>(parameters.sustainQ15) /
            static_cast<float>(
                modulation::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15
            ),
        0.0f,
        1.0f
    );
    if (positionQ16 < boundaries.attackEndQ16) {
        return modulation::evaluateProjectAdsrProgress(
            parameters.curve,
            adsrSegmentProgress(positionQ16, 0U, boundaries.attackEndQ16)
        );
    }
    if (positionQ16 < boundaries.decayEndQ16) {
        const float shaped = modulation::evaluateProjectAdsrProgress(
            parameters.curve,
            adsrSegmentProgress(
                positionQ16,
                boundaries.attackEndQ16,
                boundaries.decayEndQ16
            )
        );
        return 1.0f + (sustain - 1.0f) * shaped;
    }
    if (positionQ16 < boundaries.sustainEndQ16) return sustain;
    const float shaped = modulation::evaluateProjectAdsrProgress(
        parameters.curve,
        adsrSegmentProgress(
            positionQ16,
            boundaries.sustainEndQ16,
            65535U
        )
    );
    return sustain * (1.0f - shaped);
}

uint16_t quantizeUnipolar(float value) {
    return static_cast<uint16_t>(std::lround(
        std::clamp(value, 0.0f, 1.0f) * 65535.0f
    ));
}

int16_t quantizeBipolar(float value) {
    const float clamped = std::clamp(value, -1.0f, 1.0f);
    const float scaled = clamped * 32767.0f;
    return static_cast<int16_t>(std::lround(scaled));
}

float normalizedPosition(uint16_t positionQ16) {
    return static_cast<float>(positionQ16) / 65535.0f;
}

float elapsedBeat(uint16_t durationTicks, uint16_t positionQ16) {
    const uint32_t duration = std::max<uint16_t>(durationTicks, 1U);
    const float tick = normalizedPosition(positionQ16) *
        static_cast<float>(duration > 0U ? duration - 1U : 0U);
    return tick / static_cast<float>(macro::MACRO_AUTOMATION_TICKS_PER_BEAT);
}

float wrapPhase(float phase) {
    phase -= std::floor(phase);
    return phase < 0.0f ? phase + 1.0f : phase;
}

bool crossesSquareEdge(float previous, float current) {
    return current < previous ||
        (previous < 0.5f && current >= 0.5f);
}

float sourcePreviewValue(
    const MacroEditorPreviewModel& model,
    const modulation::ModulatorSourceState& source,
    uint16_t positionQ16,
    bool naturalTimeline,
    float& phase
) {
    if (source.kind == modulation::ModulatorKind::ADSR) {
        phase = normalizedPosition(positionQ16);
        return adsrPreviewValue(source.parameters.adsr, positionQ16);
    }
    if (source.kind == modulation::ModulatorKind::RECORDED_SHAPE) {
        phase = normalizedPosition(positionQ16);
        const auto* curve = modulation::findProjectCurve(
            model.control->authored.curves,
            source.parameters.recordedCurveId
        );
        if (curve == nullptr) return 0.0f;
        const float beat = naturalTimeline
            ? phase * static_cast<float>(curve->durationTicks) /
                static_cast<float>(macro::MACRO_AUTOMATION_TICKS_PER_BEAT)
            : elapsedBeat(model.timelineDurationTicks, positionQ16);
        return modulation::evaluateProjectControlCurve(
            *model.control,
            source.parameters.recordedCurveId,
            beat,
            0.0f
        );
    }

    const float authored = static_cast<float>(source.parameters.lfo.phaseQ15) /
        32767.0f;
    if (naturalTimeline ||
        source.parameters.lfo.timing == modulation::ModulatorTimingMode::FREE) {
        phase = wrapPhase(normalizedPosition(positionQ16) + authored);
    } else {
        const float tick = normalizedPosition(positionQ16) *
            static_cast<float>(std::max<uint16_t>(
                model.timelineDurationTicks,
                1U
            ));
        phase = wrapPhase(
            tick / static_cast<float>(std::max<uint32_t>(
                source.parameters.lfo.periodTicks,
                1U
            )) + authored
        );
    }
    return modulation::evaluateProjectLfoShape(
        source.parameters.lfo.shape,
        phase
    );
}

ContributionSample sampleProjectContribution(
    const MacroEditorPreviewModel& model,
    const modulation::ModulationBindingState& binding,
    uint16_t positionQ16,
    uint16_t previousPositionQ16,
    bool hasPrevious,
    bool naturalTimeline,
    const modulation::ModulatorSourceState* knownSource = nullptr
) {
    ContributionSample result{};
    const auto& graph = model.control->authored.modulation;
    const auto* source = knownSource != nullptr &&
            knownSource->id == binding.sourceId
        ? knownSource
        : modulation::findProjectModulator(graph, binding.sourceId);
    if (source == nullptr) return result;
    modulation::ModulatorNaturalDomain naturalDomain{};
    modulation::ResolvedModulationMapping mapping{};
    if (!modulation::projectModulatorNaturalDomain(
            *source,
            model.control->authored.curves,
            naturalDomain
        ) ||
        !modulation::resolveModulationApplication(
            binding.application,
            naturalDomain,
            mapping
        )) {
        return result;
    }
    float phase = 0.0f;
    float sourceValue = sourcePreviewValue(
        model,
        *source,
        positionQ16,
        naturalTimeline,
        phase
    );
    sourceValue = modulation::applyResolvedModulationMapping(
        sourceValue,
        mapping
    );
    result.stored = sourceValue *
        (static_cast<float>(binding.amountQ15) / 32767.0f);
    const bool active =
        (binding.flags & modulation::PROJECT_MODULATION_BINDING_FLAG_ENABLED) !=
            0U &&
        (source->flags & modulation::PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
    result.active = active ? result.stored : 0.0f;
    if (hasPrevious && source->kind == modulation::ModulatorKind::LFO &&
        source->parameters.lfo.shape == modulation::ModulatorLfoShape::SQUARE) {
        float previousPhase = 0.0f;
        (void)sourcePreviewValue(
            model,
            *source,
            previousPositionQ16,
            naturalTimeline,
            previousPhase
        );
        result.discontinuityBefore = crossesSquareEdge(previousPhase, phase);
    }
    return result;
}

float sampleProjectAutomation(
    const MacroEditorPreviewModel& model,
    const modulation::ProjectControlMacroSlotView& view,
    uint16_t positionQ16
) {
    if (!view.automationStored) return model.staticBase;
    return modulation::evaluateProjectControlCurve(
        *model.control,
        view.automationCurveId,
        elapsedBeat(model.automationDurationTicks, positionQ16),
        model.staticBase
    );
}

void selectFirstBinding(MacroEditorPreviewModel& model) {
    if (model.control == nullptr || modulation::valid(model.focusedBindingId)) {
        return;
    }
    const auto destination = modulation::projectControlDestination(model.address);
    const auto& graph = model.control->authored.modulation;
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].destination == destination) {
            model.focusedBindingId = graph.outputBindings[index].id;
            model.focusedBindingIndex = index;
            return;
        }
    }
}

void cacheFocusedSourceIndices(MacroEditorPreviewModel& model) {
    if (model.control == nullptr ||
        model.focusedBindingIndex >=
            model.control->authored.modulation.outputBindingCount) {
        return;
    }
    const auto& graph = model.control->authored.modulation;
    const auto& binding = graph.outputBindings[model.focusedBindingIndex];
    if (binding.id != model.focusedBindingId) return;
    for (uint16_t index = 0U; index < graph.sourceCount; ++index) {
        if (graph.sources[index].id == binding.sourceId) {
            model.focusedSourceIndex = index;
            break;
        }
    }
    for (uint16_t index = 0U; index < model.control->plan.sourceCount; ++index) {
        if (model.control->plan.sources[index].id == binding.sourceId) {
            model.focusedRuntimeSourceIndex = index;
            break;
        }
    }
}

}  // namespace

FLASHMEM void buildMacroEditorPreviewModel(
    float staticBase,
    const macro::MacroAutomationSlotState* slot,
    const macro::MacroAutomationPointPool& pool,
    bool manualOverride,
    MacroEditorPreviewModel& model
) {
    model = {};
    model.backend = MacroEditorPreviewModel::Backend::LEGACY_SLOT;
    model.legacySlot = slot;
    model.legacyPool = &pool;
    model.staticBase = macro::macroAutomationClamp01(staticBase);
    model.manualOverride = manualOverride;
    model.automationStored = slot != nullptr &&
        macro::macroCurveStored(slot->automation);
    model.modulationStored = slot != nullptr &&
        macro::macroCurveStored(slot->modulation);
    model.automationPlayback = model.automationStored &&
        macro::macroCurvePlaybackActive(slot->automation) && !manualOverride;
    model.modulationPlayback = model.modulationStored &&
        macro::macroCurvePlaybackActive(slot->modulation);
    model.automationDrivingBase = model.automationPlayback;
    if (model.automationStored) {
        model.automationDurationTicks = std::max<uint16_t>(
            slot->automation.durationTicks,
            1U
        );
        model.timelineDurationTicks = std::max<uint16_t>(
            model.timelineDurationTicks,
            model.automationDurationTicks
        );
    }
    if (model.modulationStored) {
        model.modulationDurationTicks = std::max<uint16_t>(
            slot->modulation.durationTicks,
            1U
        );
        model.timelineDurationTicks = std::max<uint16_t>(
            model.timelineDurationTicks,
            model.modulationDurationTicks
        );
    }
}

FLASHMEM void buildMacroEditorPreviewModel(
    float staticBase,
    const modulation::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    bool manualOverride,
    core::state::modulation::ModulationBindingId focusedBindingId,
    const MacroEditorLiveValue& live,
    MacroEditorPreviewModel& model
) {
    model = {};
    model.backend = MacroEditorPreviewModel::Backend::PROJECT_CONTROL;
    model.control = &control;
    model.address = address;
    model.focusedBindingId = focusedBindingId;
    model.live = live;
    model.staticBase = macro::macroAutomationClamp01(staticBase);
    model.manualOverride = manualOverride;

    modulation::ProjectControlMacroSlotView view{};
    if (!modulation::readProjectControlMacroSlot(control, address, view)) {
        return;
    }
    model.automationStored = view.automationStored;
    model.automationPlayback = view.automationEnabled && !manualOverride;
    model.automationDrivingBase = model.automationPlayback;
    if (view.automationStored) {
        const auto* curve = modulation::findProjectCurve(
            control.authored.curves,
            view.automationCurveId
        );
        if (curve != nullptr) {
            model.automationDurationTicks = std::max<uint16_t>(
                curve->durationTicks,
                1U
            );
            model.timelineDurationTicks = std::max<uint16_t>(
                model.timelineDurationTicks,
                model.automationDurationTicks
            );
        }
    }

    const auto destination = modulation::projectControlDestination(address);
    const auto& graph = control.authored.modulation;
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        if (binding.id == model.focusedBindingId) {
            model.focusedBindingIndex = index;
        }
        const auto* source = modulation::findProjectModulator(
            graph,
            binding.sourceId
        );
        if (source == nullptr) continue;
        model.modulationStored = true;
        model.modulationPlayback = model.modulationPlayback || (
            (binding.flags &
             modulation::PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U &&
            (source->flags & modulation::PROJECT_MODULATOR_FLAG_ENABLED) != 0U
        );
        if (source->kind == modulation::ModulatorKind::RECORDED_SHAPE) {
            const auto* curve = modulation::findProjectCurve(
                control.authored.curves,
                source->parameters.recordedCurveId
            );
            if (curve != nullptr) {
                model.modulationDurationTicks = std::max<uint16_t>(
                    model.modulationDurationTicks,
                    curve->durationTicks
                );
                model.timelineDurationTicks = std::max<uint16_t>(
                    model.timelineDurationTicks,
                    curve->durationTicks
                );
            }
        } else if (source->kind == modulation::ModulatorKind::LFO &&
                   source->parameters.lfo.timing ==
                       modulation::ModulatorTimingMode::SYNC) {
            const uint16_t duration = static_cast<uint16_t>(
                std::min<uint32_t>(
                    source->parameters.lfo.periodTicks,
                    UINT16_MAX
                )
            );
            model.modulationDurationTicks = std::max<uint16_t>(
                model.modulationDurationTicks,
                duration
            );
            model.timelineDurationTicks = std::max<uint16_t>(
                model.timelineDurationTicks,
                duration
            );
        }
    }
    selectFirstBinding(model);
    cacheFocusedSourceIndices(model);
}

FLASHMEM void buildMacroEditorPreviewModel(
    float staticBase,
    const modulation::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    bool manualOverride,
    MacroEditorPreviewModel& model
) {
    buildMacroEditorPreviewModel(
        staticBase,
        control,
        address,
        manualOverride,
        {},
        {},
        model
    );
}

FLASHMEM MacroEditorPreviewModel buildMacroEditorPreviewModel(
    float staticBase,
    const macro::MacroAutomationSlotState* slot,
    const macro::MacroAutomationPointPool& pool,
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

FLASHMEM bool sampleMacroEditorPreview(
    const MacroEditorPreviewModel& model,
    MacroEditorPreviewFocus focus,
    uint16_t positionQ16,
    uint16_t previousPositionQ16,
    bool hasPrevious,
    MacroEditorPreviewSample& out
) {
    float automation = model.staticBase;
    float base = model.staticBase;
    float storedModulation = 0.0f;
    float activeModulation = 0.0f;
    bool discontinuity = false;

    if (model.backend == MacroEditorPreviewModel::Backend::LEGACY_SLOT) {
        if (model.legacyPool == nullptr) return false;
        if (model.automationStored && model.legacySlot != nullptr) {
            automation = macro::macroAutomationEvaluate(
                model.legacySlot->automation,
                *model.legacyPool,
                elapsedBeat(model.automationDurationTicks, positionQ16),
                model.staticBase
            );
        }
        base = model.automationPlayback ? automation : model.staticBase;
        if (model.modulationStored && model.legacySlot != nullptr) {
            storedModulation = macro::macroModulationEvaluate(
                model.legacySlot->modulation,
                *model.legacyPool,
                elapsedBeat(model.modulationDurationTicks, positionQ16)
            ) * std::clamp(
                model.legacySlot->modulationDepth,
                -1.0f,
                1.0f
            );
            activeModulation = model.modulationPlayback
                ? storedModulation
                : 0.0f;
        }
    } else if (
        model.backend == MacroEditorPreviewModel::Backend::PROJECT_CONTROL &&
        model.control != nullptr
    ) {
        modulation::ProjectControlMacroSlotView view{};
        (void)modulation::readProjectControlMacroSlot(
            *model.control,
            model.address,
            view
        );
        automation = sampleProjectAutomation(model, view, positionQ16);
        base = model.automationPlayback ? automation : model.staticBase;
        if (focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR &&
            model.live.valid) {
            base = model.live.base;
        }
        const auto destination = modulation::projectControlDestination(model.address);
        const float scale = static_cast<float>(
            modulation::projectModulationDestinationScaleQ15(
                model.control->authored.modulation,
                destination
            )
        ) / static_cast<float>(
            modulation::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15
        );
        const auto& graph = model.control->authored.modulation;
        const auto* focusedSource =
            model.focusedSourceIndex < graph.sourceCount
            ? &graph.sources[model.focusedSourceIndex]
            : nullptr;
        const uint16_t begin =
            focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR &&
            model.focusedBindingIndex < graph.outputBindingCount
            ? model.focusedBindingIndex
            : 0U;
        const uint16_t end =
            focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR &&
            model.focusedBindingIndex < graph.outputBindingCount
            ? static_cast<uint16_t>(model.focusedBindingIndex + 1U)
            : graph.outputBindingCount;
        for (uint16_t index = begin;
             focus != MacroEditorPreviewFocus::AUTOMATION && index < end;
             ++index) {
            const auto& binding = graph.outputBindings[index];
            if (binding.destination != destination) continue;
            if (focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR &&
                binding.id != model.focusedBindingId) {
                continue;
            }
            const auto contribution = sampleProjectContribution(
                model,
                binding,
                positionQ16,
                previousPositionQ16,
                hasPrevious,
                focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR,
                focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR
                    ? focusedSource
                    : nullptr
            );
            storedModulation += contribution.stored;
            activeModulation += contribution.active;
            discontinuity = discontinuity ||
                contribution.discontinuityBefore;
        }
        storedModulation *= scale;
        activeModulation *= scale;
    } else {
        return false;
    }

    if (focus == MacroEditorPreviewFocus::AUTOMATION) {
        base = automation;
        activeModulation = 0.0f;
    }
    const float rawOut = base + activeModulation;
    out = {
        .automationQ16 = quantizeUnipolar(automation),
        .baseQ16 = quantizeUnipolar(base),
        .modulationQ15 = quantizeBipolar(storedModulation),
        .outQ16 = quantizeUnipolar(rawOut),
        .clippedLow = rawOut < 0.0f,
        .clippedHigh = rawOut > 1.0f,
        .discontinuityBefore = discontinuity,
    };
    return true;
}

}  // namespace core::ui

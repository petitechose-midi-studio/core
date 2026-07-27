#include "ui/macro/MacroEditorPreviewModel.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/modulation/ProjectRecordedShapeCaptureState.hpp"
#include "ui/modulation/ModulatorAdsrUiModel.hpp"

namespace core::ui {
namespace {

namespace macro = core::state::macro;
namespace state_mod = core::state::modulation;
namespace adsr_ui = core::ui::modulation::adsr;

struct ContributionSample {
    float stored = 0.0f;
    float active = 0.0f;
    bool discontinuityBefore = false;
};

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

uint16_t sourcePhasePositionQ16(
    uint16_t timelineDurationTicks,
    uint16_t sourceDurationTicks,
    uint16_t positionQ16
) {
    const uint32_t timeline = std::max<uint16_t>(timelineDurationTicks, 1U);
    const uint32_t source = std::max<uint16_t>(sourceDurationTicks, 1U);
    const uint64_t elapsedQ16 = static_cast<uint64_t>(positionQ16) * timeline;
    const uint64_t sourceQ16 = source * 65535ULL;
    return static_cast<uint16_t>(sourceQ16 == 0U
        ? 0U
        : (elapsedQ16 % sourceQ16) / source);
}

float wrapPhase(float phase) {
    phase -= std::floor(phase);
    return phase < 0.0f ? phase + 1.0f : phase;
}

uint16_t freePeriodTicks(uint32_t periodMs, float tempoBpm) {
    const double tempo = std::clamp<double>(tempoBpm, 1.0, 999.0);
    const double ticks = static_cast<double>(periodMs) * tempo *
        static_cast<double>(macro::MACRO_AUTOMATION_TICKS_PER_BEAT) /
        60000.0;
    return static_cast<uint16_t>(std::clamp<long>(
        std::lround(ticks),
        1L,
        static_cast<long>(UINT16_MAX)
    ));
}

uint16_t sourceTimelineDurationTicks(
    const MacroEditorPreviewModel& model,
    const state_mod::ModulatorSourceState& source
) {
    if (source.kind == state_mod::ModulatorKind::RECORDED_SHAPE) {
        const auto* curve = state_mod::findProjectCurve(
            model.control->authored.curves,
            source.parameters.recordedCurveId
        );
        return curve != nullptr
            ? std::max<uint16_t>(curve->durationTicks, 1U)
            : 0U;
    }
    if (source.kind != state_mod::ModulatorKind::LFO) return 0U;
    if (source.parameters.lfo.timing == state_mod::ModulatorTimingMode::FREE) {
        return freePeriodTicks(
            source.parameters.lfo.freePeriodMs,
            model.timelineTempoBpm
        );
    }
    return static_cast<uint16_t>(std::clamp<uint32_t>(
        source.parameters.lfo.periodTicks,
        1U,
        UINT16_MAX
    ));
}

bool crossesSquareEdge(float previous, float current) {
    return current < previous ||
        (previous < 0.5f && current >= 0.5f);
}

float sourcePreviewValue(
    const MacroEditorPreviewModel& model,
    const state_mod::ModulatorSourceState& source,
    uint16_t positionQ16,
    bool naturalTimeline,
    float& phase
) {
    if (source.kind == state_mod::ModulatorKind::ADSR) {
        phase = normalizedPosition(positionQ16);
        const auto boundaries = adsr_ui::previewBoundaries(
            source.parameters.adsr
        );
        return adsr_ui::previewValue(
            source.parameters.adsr,
            boundaries,
            positionQ16
        );
    }
    if (source.kind == state_mod::ModulatorKind::RECORDED_SHAPE) {
        phase = normalizedPosition(positionQ16);
        const auto* curve = state_mod::findProjectCurve(
            model.control->authored.curves,
            source.parameters.recordedCurveId
        );
        if (curve == nullptr) return 0.0f;
        const float beat = naturalTimeline
            ? phase * static_cast<float>(curve->durationTicks) /
                static_cast<float>(macro::MACRO_AUTOMATION_TICKS_PER_BEAT)
            : elapsedBeat(model.timelineDurationTicks, positionQ16);
        return state_mod::evaluateProjectControlCurve(
            *model.control,
            source.parameters.recordedCurveId,
            beat,
            0.0f
        );
    }

    const float authored = static_cast<float>(source.parameters.lfo.phaseQ15) /
        32767.0f;
    if (naturalTimeline) {
        phase = wrapPhase(normalizedPosition(positionQ16) + authored);
    } else {
        const float tick = normalizedPosition(positionQ16) *
            static_cast<float>(std::max<uint16_t>(
                model.timelineDurationTicks,
                1U
            ));
        phase = wrapPhase(
            tick / static_cast<float>(std::max<uint32_t>(
                sourceTimelineDurationTicks(model, source),
                1U
            )) + authored
        );
    }
    return state_mod::evaluateProjectLfoShape(
        source.parameters.lfo.shape,
        phase
    );
}

ContributionSample sampleResolvedProjectContribution(
    const MacroEditorPreviewModel& model,
    const state_mod::ModulatorSourceState& source,
    state_mod::ResolvedModulationMapping mapping,
    int16_t amountQ15,
    uint8_t bindingFlags,
    uint16_t positionQ16,
    uint16_t previousPositionQ16,
    bool hasPrevious,
    bool naturalTimeline,
    bool capturedSource
) {
    ContributionSample result{};
    float phase = 0.0f;
    float sourceValue = 0.0f;
    if (capturedSource) {
        const auto& capture = *model.recordedShapeCapture;
        int16_t valueQ15 = 0;
        const uint16_t capturePositionQ16 = naturalTimeline
            ? positionQ16
            : sourcePhasePositionQ16(
                model.timelineDurationTicks,
                capture.durationTicks,
                positionQ16
            );
        if (!capture.take->samplePreviewValue(
                capturePositionQ16,
                valueQ15
            )) {
            return result;
        }
        sourceValue = static_cast<float>(valueQ15) / 32767.0f;
        phase = normalizedPosition(capturePositionQ16);
    } else {
        sourceValue = sourcePreviewValue(
            model,
            source,
            positionQ16,
            naturalTimeline,
            phase
        );
    }
    sourceValue = state_mod::applyResolvedModulationMapping(
        sourceValue,
        mapping
    );
    result.stored = sourceValue *
        (static_cast<float>(amountQ15) / 32767.0f);
    const bool active =
        (bindingFlags &
         state_mod::PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U &&
        (source.flags & state_mod::PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
    result.active = active ? result.stored : 0.0f;
    if (!capturedSource && hasPrevious &&
        source.kind == state_mod::ModulatorKind::LFO &&
        source.parameters.lfo.shape == state_mod::ModulatorLfoShape::SQUARE) {
        float previousPhase = 0.0f;
        (void)sourcePreviewValue(
            model,
            source,
            previousPositionQ16,
            naturalTimeline,
            previousPhase
        );
        result.discontinuityBefore = crossesSquareEdge(previousPhase, phase);
    }
    return result;
}

ContributionSample sampleProjectContribution(
    const MacroEditorPreviewModel& model,
    const state_mod::ModulationBindingState& binding,
    uint16_t positionQ16,
    uint16_t previousPositionQ16,
    bool hasPrevious,
    bool naturalTimeline,
    const state_mod::ModulatorSourceState* knownSource = nullptr
) {
    ContributionSample result{};
    const auto& graph = model.control->authored.modulation;
    const auto* source = knownSource != nullptr &&
            knownSource->id == binding.sourceId
        ? knownSource
        : state_mod::findProjectModulator(graph, binding.sourceId);
    if (source == nullptr) return result;
    const auto* capture = model.recordedShapeCapture;
    const bool capturedSource = capture != nullptr &&
        capture->mode ==
            state_mod::ProjectRecordedShapeCaptureMode::REPLACE_EXISTING &&
        capture->sourceId == binding.sourceId && capture->take != nullptr;
    state_mod::ModulatorNaturalDomain naturalDomain{};
    state_mod::ResolvedModulationMapping mapping{};
    if (capturedSource) {
        naturalDomain = state_mod::ModulatorNaturalDomain::CENTERED;
    } else if (!state_mod::projectModulatorNaturalDomain(
                   *source,
                   model.control->authored.curves,
                   naturalDomain
               )) {
        return result;
    }
    if (!state_mod::resolveModulationApplication(
            binding.application,
            naturalDomain,
            mapping
        )) {
        return result;
    }
    return sampleResolvedProjectContribution(
        model,
        *source,
        mapping,
        binding.amountQ15,
        binding.flags,
        positionQ16,
        previousPositionQ16,
        hasPrevious,
        naturalTimeline,
        capturedSource
    );
}

ContributionSample sampleRuntimeProjectContribution(
    const MacroEditorPreviewModel& model,
    const state_mod::ProjectModulationRuntimeBinding& binding,
    uint16_t positionQ16,
    uint16_t previousPositionQ16,
    bool hasPrevious,
    bool naturalTimeline
) {
    const auto& graph = model.control->authored.modulation;
    if (binding.sourceIndex >= graph.sourceCount) return {};
    const auto& source = graph.sources[binding.sourceIndex];
    return sampleResolvedProjectContribution(
        model,
        source,
        binding.mapping,
        binding.amountQ15,
        binding.flags,
        positionQ16,
        previousPositionQ16,
        hasPrevious,
        naturalTimeline,
        false
    );
}

ContributionSample sampleProvisionalRecordedShape(
    const MacroEditorPreviewModel& model,
    uint16_t positionQ16,
    bool naturalTimeline
) {
    ContributionSample result{};
    const auto* capture = model.recordedShapeCapture;
    if (capture == nullptr || capture->take == nullptr ||
        capture->mode !=
            state_mod::ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED ||
        capture->destination !=
            state_mod::projectControlDestination(model.address)) {
        return result;
    }
    int16_t sourceValueQ15 = 0;
    const uint16_t capturePositionQ16 = naturalTimeline
        ? positionQ16
        : sourcePhasePositionQ16(
            model.timelineDurationTicks,
            capture->durationTicks,
            positionQ16
        );
    if (!capture->take->samplePreviewValue(
            capturePositionQ16,
            sourceValueQ15
        )) {
        return result;
    }
    result.stored =
        (static_cast<float>(sourceValueQ15) / 32767.0f) *
        (static_cast<float>(capture->amountQ15) / 32767.0f);
    result.active = capture->enabled ? result.stored : 0.0f;
    return result;
}

float sampleProjectAutomation(
    const MacroEditorPreviewModel& model,
    uint16_t positionQ16
) {
    float takeValue = 0.0f;
    if (model.activeTake != nullptr &&
        model.activeTake->sampleFixedPreviewValue(
            model.activeTakeMacro,
            sourcePhasePositionQ16(
                model.timelineDurationTicks,
                model.automationDurationTicks,
                positionQ16
            ),
            takeValue
        )) {
        return takeValue;
    }
    if (!model.automationStored) return model.staticBase;
    return state_mod::evaluateProjectControlCurveRecord(
        *model.control,
        model.automationCurveId,
        model.automationCurveRecordIndex,
        elapsedBeat(model.timelineDurationTicks, positionQ16),
        model.staticBase
    );
}

FLASHMEM void selectFirstBinding(MacroEditorPreviewModel& model) {
    if (model.control == nullptr || state_mod::valid(model.focusedBindingId)) {
        return;
    }
    const auto destination = state_mod::projectControlDestination(model.address);
    const auto& graph = model.control->authored.modulation;
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].destination == destination) {
            model.focusedBindingId = graph.outputBindings[index].id;
            model.focusedBindingIndex = index;
            return;
        }
    }
}

FLASHMEM void cacheFocusedSourceIndices(MacroEditorPreviewModel& model) {
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
}

FLASHMEM void cacheRuntimePlanIndices(MacroEditorPreviewModel& model) {
    if (model.control == nullptr ||
        model.control->compiledRevision == 0U ||
        model.control->compiledRevision !=
            model.control->authoredRevision) {
        return;
    }
    const auto& plan = model.control->plan;
    for (uint16_t index = 0U; index < plan.destinationCount; ++index) {
        const auto& destination = plan.destinations[index];
        if (destination.destination != model.destination) continue;
        model.runtimeDestinationIndex = index;
        model.destinationScaleQ15 = destination.destinationScaleQ15;
        model.planCompiledRevision = model.control->compiledRevision;
        model.planContextHash = model.control->runtimeContextHash;
        for (uint16_t relative = 0U;
             relative < destination.bindingCount;
             ++relative) {
            const uint16_t order = static_cast<uint16_t>(
                destination.firstBinding + relative
            );
            if (order >= plan.bindingCount) break;
            const uint16_t bindingIndex = plan.bindingOrder[order];
            if (bindingIndex < plan.bindingCount &&
                plan.bindings[bindingIndex].id ==
                    model.focusedBindingId) {
                model.focusedRuntimeBindingIndex = bindingIndex;
                break;
            }
        }
        return;
    }
}

}  // namespace

FLASHMEM void buildMacroEditorPreviewModel(
    float staticBase,
    const state_mod::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    bool manualOverride,
    core::state::modulation::ModulationBindingId focusedBindingId,
    MacroEditorPreviewModel& model,
    float timelineTempoBpm
) {
    model = {};
    model.backend = MacroEditorPreviewModel::Backend::PROJECT_CONTROL;
    model.control = &control;
    model.address = address;
    model.authoredRevision = control.authoredRevision;
    model.destination = state_mod::projectControlDestination(address);
    model.destinationScaleQ15 =
        state_mod::projectModulationDestinationScaleQ15(
            control.authored.modulation,
            model.destination
        );
    model.focusedBindingId = focusedBindingId;
    model.staticBase = macro::macroAutomationClamp01(staticBase);
    model.manualOverride = manualOverride;
    model.timelineTempoBpm = std::isfinite(timelineTempoBpm)
        ? std::clamp(timelineTempoBpm, 1.0f, 999.0f)
        : 120.0f;
    model.timelineDurationTicks = 0U;
    model.modulationDurationTicks = 0U;

    state_mod::ProjectControlMacroDestinationView view{};
    if (!state_mod::readProjectControlMacroDestination(
            control,
            address,
            view
        )) {
        model.modulationDurationTicks =
            macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
        model.timelineDurationTicks =
            macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
        return;
    }
    model.automationStored = view.automation.stored();
    model.automationCurveId = view.automation.id;
    model.automationPlayback =
        view.automation.stored() && view.automation.enabled && !manualOverride;
    model.automationDrivingBase = model.automationPlayback;
    if (view.automation.stored()) {
        const auto* curve = state_mod::findProjectCurve(
            control.authored.curves,
            view.automation.id
        );
        if (curve != nullptr) {
            model.automationCurveRecordIndex = static_cast<uint16_t>(
                curve - control.authored.curves.records.data()
            );
            model.automationDurationTicks = std::max<uint16_t>(
                curve->durationTicks,
                1U
            );
            if (model.automationPlayback) {
                model.timelineHasActiveSource = true;
                model.timelineDurationTicks = std::max<uint16_t>(
                    model.timelineDurationTicks,
                    model.automationDurationTicks
                );
            }
        }
    }

    const auto destination = model.destination;
    const auto& graph = control.authored.modulation;
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        if (binding.id == model.focusedBindingId) {
            model.focusedBindingIndex = index;
        }
        const auto* source = state_mod::findProjectModulator(
            graph,
            binding.sourceId
        );
        if (source == nullptr) continue;
        model.modulationStored = true;
        model.modulationPlayback = model.modulationPlayback || (
            (binding.flags &
             state_mod::PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U &&
            (source->flags & state_mod::PROJECT_MODULATOR_FLAG_ENABLED) != 0U
        );
        const uint16_t duration = sourceTimelineDurationTicks(model, *source);
        if (duration > 0U) {
            model.modulationDurationTicks = std::max<uint16_t>(
                model.modulationDurationTicks,
                duration
            );
            const bool sourceActive =
                (binding.flags &
                 state_mod::PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U &&
                (source->flags &
                 state_mod::PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
            if (sourceActive) {
                model.timelineHasActiveSource = true;
                model.timelineDurationTicks = std::max<uint16_t>(
                    model.timelineDurationTicks,
                    duration
                );
            }
        }
    }
    if (model.modulationDurationTicks == 0U) {
        model.modulationDurationTicks =
            macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
    }
    if (model.timelineDurationTicks == 0U) {
        model.timelineDurationTicks =
            macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
    }
    selectFirstBinding(model);
    cacheFocusedSourceIndices(model);
    cacheRuntimePlanIndices(model);
}

FLASHMEM void buildMacroEditorPreviewModel(
    float staticBase,
    const state_mod::ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    bool manualOverride,
    MacroEditorPreviewModel& model,
    float timelineTempoBpm
) {
    buildMacroEditorPreviewModel(
        staticBase,
        control,
        address,
        manualOverride,
        {},
        model,
        timelineTempoBpm
    );
}

FLASHMEM void attachMacroAutomationTakePreview(
    const macro::MacroAutomationTakeState& take,
    uint8_t macro,
    MacroEditorPreviewModel& model
) {
    if (model.backend != MacroEditorPreviewModel::Backend::PROJECT_CONTROL ||
        take.phase != macro::MacroAutomationTakePhase::RECORDING ||
        !take.fixedLength() || !take.activeFor(macro) ||
        take.durationTicks == 0U) {
        return;
    }
    model.activeTake = &take;
    model.activeTakeMacro = macro;
    model.automationDurationTicks = take.durationTicks;
    model.timelineDurationTicks = model.timelineHasActiveSource
        ? std::max<uint16_t>(model.timelineDurationTicks, take.durationTicks)
        : take.durationTicks;
    model.timelineHasActiveSource = true;
    model.automationStored = true;
    model.automationPlayback = true;
    model.automationDrivingBase = true;
    model.manualOverride = false;
}

FLASHMEM void attachProjectRecordedShapeCapturePreview(
    const state_mod::ProjectRecordedShapeCaptureState& capture,
    MacroEditorPreviewModel& model
) {
    if (model.backend != MacroEditorPreviewModel::Backend::PROJECT_CONTROL ||
        model.control == nullptr || !capture.active() ||
        capture.take == nullptr ||
        capture.take->phase !=
            state_mod::ProjectRecordedShapeTakePhase::RECORDING ||
        capture.durationTicks == 0U) {
        return;
    }

    const auto destination = state_mod::projectControlDestination(model.address);
    bool applies = capture.mode ==
            state_mod::ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED &&
        capture.destination == destination;
    if (capture.mode ==
            state_mod::ProjectRecordedShapeCaptureMode::REPLACE_EXISTING &&
        state_mod::valid(capture.sourceId)) {
        const auto& graph = model.control->authored.modulation;
        for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
            const auto& binding = graph.outputBindings[index];
            if (binding.destination == destination &&
                binding.sourceId == capture.sourceId) {
                applies = true;
                break;
            }
        }
    }
    if (!applies) return;

    model.recordedShapeCapture = &capture;
    model.modulationStored = true;
    if (capture.mode ==
        state_mod::ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED) {
        model.modulationPlayback = model.modulationPlayback ||
            (capture.enabled && capture.amountQ15 != 0);
    }
    model.modulationDurationTicks = std::max<uint16_t>(
        model.modulationDurationTicks,
        capture.durationTicks
    );
    model.timelineDurationTicks = model.timelineHasActiveSource
        ? std::max<uint16_t>(model.timelineDurationTicks, capture.durationTicks)
        : capture.durationTicks;
    model.timelineHasActiveSource = true;
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

    if (
        model.backend == MacroEditorPreviewModel::Backend::PROJECT_CONTROL &&
        model.control != nullptr
    ) {
        automation = sampleProjectAutomation(model, positionQ16);
        base = model.automationPlayback ? automation : model.staticBase;
        const auto destination = model.destination;
        const bool authoredModelCurrent =
            model.authoredRevision == model.control->authoredRevision;
        const uint16_t destinationScaleQ15 = authoredModelCurrent
            ? model.destinationScaleQ15
            : state_mod::projectModulationDestinationScaleQ15(
                model.control->authored.modulation,
                destination
            );
        const float scale = static_cast<float>(destinationScaleQ15) /
            static_cast<float>(
            state_mod::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15
        );
        const auto& graph = model.control->authored.modulation;
        const bool provisionalFocused =
            model.recordedShapeCapture != nullptr &&
            model.recordedShapeCapture->mode == state_mod::
                ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED &&
            focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR;
        const auto& plan = model.control->plan;
        const bool runtimePlanCurrent =
            authoredModelCurrent &&
            model.recordedShapeCapture == nullptr &&
            model.runtimeDestinationIndex < plan.destinationCount &&
            model.planCompiledRevision != 0U &&
            model.planCompiledRevision ==
                model.control->authoredRevision &&
            model.planCompiledRevision ==
                model.control->compiledRevision &&
            model.planContextHash ==
                model.control->runtimeContextHash &&
            plan.destinations[model.runtimeDestinationIndex].destination ==
                destination;
        if (runtimePlanCurrent) {
            const auto sampleBinding =
                [&](uint16_t bindingIndex) {
                    if (bindingIndex >= plan.bindingCount) return;
                    const auto contribution =
                        sampleRuntimeProjectContribution(
                            model,
                            plan.bindings[bindingIndex],
                            positionQ16,
                            previousPositionQ16,
                            hasPrevious,
                            focus ==
                                MacroEditorPreviewFocus::FOCUSED_MODULATOR
                        );
                    storedModulation += contribution.stored;
                    activeModulation += contribution.active;
                    discontinuity = discontinuity ||
                        contribution.discontinuityBefore;
                };
            if (focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR) {
                if (!provisionalFocused) {
                    sampleBinding(model.focusedRuntimeBindingIndex);
                }
            } else {
                const auto& runtimeDestination =
                    plan.destinations[model.runtimeDestinationIndex];
                for (uint16_t relative = 0U;
                     relative < runtimeDestination.bindingCount;
                     ++relative) {
                    const uint16_t order = static_cast<uint16_t>(
                        runtimeDestination.firstBinding + relative
                    );
                    if (order >= plan.bindingCount) break;
                    sampleBinding(plan.bindingOrder[order]);
                }
            }
        } else {
            const auto* focusedSource =
                model.focusedSourceIndex < graph.sourceCount
                ? &graph.sources[model.focusedSourceIndex]
                : nullptr;
            const bool focusedBindingCacheCurrent =
                authoredModelCurrent &&
                model.focusedBindingIndex < graph.outputBindingCount &&
                graph.outputBindings[model.focusedBindingIndex].id ==
                    model.focusedBindingId;
            const uint16_t begin =
                focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR &&
                !provisionalFocused &&
                focusedBindingCacheCurrent
                ? model.focusedBindingIndex
                : 0U;
            const uint16_t end =
                provisionalFocused
                ? 0U
                : (focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR &&
                       focusedBindingCacheCurrent
                    ? static_cast<uint16_t>(
                        model.focusedBindingIndex + 1U
                    )
                    : graph.outputBindingCount);
            for (uint16_t index = begin; index < end; ++index) {
                const auto& binding = graph.outputBindings[index];
                if (binding.destination != destination) continue;
                if (focus ==
                        MacroEditorPreviewFocus::FOCUSED_MODULATOR &&
                    binding.id != model.focusedBindingId) {
                    continue;
                }
                const auto contribution = sampleProjectContribution(
                    model,
                    binding,
                    positionQ16,
                    previousPositionQ16,
                    hasPrevious,
                    focus ==
                        MacroEditorPreviewFocus::FOCUSED_MODULATOR,
                    focus ==
                            MacroEditorPreviewFocus::FOCUSED_MODULATOR
                        ? focusedSource
                        : nullptr
                );
                storedModulation += contribution.stored;
                activeModulation += contribution.active;
                discontinuity = discontinuity ||
                    contribution.discontinuityBefore;
            }
        }
        if (model.recordedShapeCapture != nullptr &&
            model.recordedShapeCapture->mode == state_mod::
                ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED) {
            const auto contribution = sampleProvisionalRecordedShape(
                model,
                positionQ16,
                focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR
            );
            storedModulation += contribution.stored;
            activeModulation += contribution.active;
        }
        storedModulation *= scale;
        activeModulation *= scale;
    } else {
        return false;
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

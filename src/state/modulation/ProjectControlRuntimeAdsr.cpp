#include "state/modulation/ProjectControlRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ModulatorEnvelopeTiming.hpp"
#include "state/modulation/ProjectControlRuntimeInternal.hpp"

namespace core::state::modulation {
namespace project_control_runtime_detail {

bool triggerIdentityMatches(
    const ModulationTriggerFilter& configured,
    const ModulationTriggerRef& incoming
) {
    if (configured.kind != incoming.kind ||
        configured.track != incoming.track) {
        return false;
    }
    return configured.kind != ModulationTriggerKind::TRACK_NOTE ||
        (incoming.data <= 127U && incoming.data >= configured.noteMin &&
         incoming.data <= configured.noteMax);
}

int16_t packQ15(float value) {
    const float scaled = std::clamp(value, -1.0f, 1.0f) * Q15_SCALE;
    // The Cortex-M hot path does not need a libm lround call for an already
    // bounded value. Truncation after a signed half-unit offset retains the
    // same round-half-away-from-zero contract over the complete Q15 range.
    return static_cast<int16_t>(
        scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f
    );
}

float unpackQ15(int16_t value) {
    return std::clamp(static_cast<float>(value) / Q15_SCALE, -1.0f, 1.0f);
}

float adsrSustainLevel(const ProjectModulationRuntimeSource& source) {
    return std::clamp(
        static_cast<float>(source.parameters.adsr.sustainQ15) /
            ADSR_SUSTAIN_SCALE,
        0.0f,
        1.0f
    );
}

void anchorAdsrStage(
    ProjectModulationRuntimeAdsrState& state,
    ProjectModulationAdsrStage stage,
    float startLevel,
    const ProjectControlTimeSnapshot& time,
    ModulatorTimingMode timing
) {
    state.stage = stage;
    state.stageStartLevelQ15 = packQ15(std::clamp(startLevel, 0.0f, 1.0f));
    if (timing == ModulatorTimingMode::SYNC) {
        state.stageAnchor = time.musicalTick;
        state.stageAnchorFractionQ16 = time.musicalTickFractionQ16;
    } else {
        state.stageAnchor = time.monotonicMs;
        state.stageAnchorFractionQ16 = 0U;
    }
}

bool adsrStageCompleteAndProgress(
    const ProjectModulationRuntimeAdsrState& state,
    const ProjectControlTimeSnapshot& time,
    ModulatorTimingMode timing,
    uint32_t duration,
    float& progress
) {
    if (timing == ModulatorTimingMode::FREE) {
        const uint32_t elapsed = time.monotonicMs - state.stageAnchor;
        if (elapsed >= duration) return true;
        progress = static_cast<float>(elapsed) /
            static_cast<float>(duration);
        return false;
    }
    uint32_t ticks = 0U;
    uint16_t fraction = 0U;
    elapsedMusicalTime(
        time,
        state.stageAnchor,
        state.stageAnchorFractionQ16,
        ticks,
        fraction
    );
    if (ticks >= duration) return true;
    progress = (
        static_cast<float>(ticks) +
        static_cast<float>(fraction) / Q16_SCALE
    ) / static_cast<float>(duration);
    return false;
}

void advanceAdsrAnchor(
    ProjectModulationRuntimeAdsrState& state,
    uint32_t duration
) {
    state.stageAnchor += duration;
}

ModulatorEnvelopeTimeParameter adsrStageTimeParameter(
    ProjectModulationAdsrStage stage
) {
    switch (stage) {
        case ProjectModulationAdsrStage::DELAY:
            return ModulatorEnvelopeTimeParameter::DELAY;
        case ProjectModulationAdsrStage::ATTACK:
            return ModulatorEnvelopeTimeParameter::ATTACK;
        case ProjectModulationAdsrStage::HOLD:
            return ModulatorEnvelopeTimeParameter::HOLD;
        case ProjectModulationAdsrStage::DECAY:
            return ModulatorEnvelopeTimeParameter::DECAY;
        case ProjectModulationAdsrStage::RELEASE:
            return ModulatorEnvelopeTimeParameter::RELEASE;
        case ProjectModulationAdsrStage::IDLE:
        case ProjectModulationAdsrStage::SUSTAIN:
        default:
            return ModulatorEnvelopeTimeParameter::SMOOTH;
    }
}

uint32_t adsrStageDuration(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationAdsrStage stage
) {
    if (stage == ProjectModulationAdsrStage::IDLE ||
        stage == ProjectModulationAdsrStage::SUSTAIN) {
        return 0U;
    }
    const auto parameter = adsrStageTimeParameter(stage);
    const uint16_t base = modulatorEnvelopeDuration(
        source.parameters.adsr,
        parameter
    );
    if (source.traits.adsr.timing == ModulatorTimingMode::FREE) return base;
    return resolveModulatorEnvelopeSyncTicks(
        base,
        modulatorAdsrFeel(source.parameters.adsr.traits, parameter)
    );
}

ProjectModulationAdsrStage nextAdsrStage(
    ProjectModulationAdsrStage stage
) {
    switch (stage) {
        case ProjectModulationAdsrStage::DELAY:
            return ProjectModulationAdsrStage::ATTACK;
        case ProjectModulationAdsrStage::ATTACK:
            return ProjectModulationAdsrStage::HOLD;
        case ProjectModulationAdsrStage::HOLD:
            return ProjectModulationAdsrStage::DECAY;
        case ProjectModulationAdsrStage::DECAY:
            return ProjectModulationAdsrStage::SUSTAIN;
        case ProjectModulationAdsrStage::RELEASE:
            return ProjectModulationAdsrStage::IDLE;
        case ProjectModulationAdsrStage::IDLE:
        case ProjectModulationAdsrStage::SUSTAIN:
        default:
            return stage;
    }
}

float adsrStageTarget(
    const ProjectModulationRuntimeSource& source,
    const ProjectModulationRuntimeAdsrState& state,
    ProjectModulationAdsrStage stage
) {
    switch (stage) {
        case ProjectModulationAdsrStage::DELAY:
            return std::clamp(
                unpackQ15(state.stageStartLevelQ15),
                0.0f,
                1.0f
            );
        case ProjectModulationAdsrStage::ATTACK:
        case ProjectModulationAdsrStage::HOLD:
            return 1.0f;
        case ProjectModulationAdsrStage::DECAY:
            return adsrSustainLevel(source);
        case ProjectModulationAdsrStage::RELEASE:
        case ProjectModulationAdsrStage::IDLE:
            return 0.0f;
        case ProjectModulationAdsrStage::SUSTAIN:
        default:
            return adsrSustainLevel(source);
    }
}

float advanceAdsrRawToTime(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationRuntimeAdsrState& state,
    const ProjectControlTimeSnapshot& time
) {
    constexpr uint8_t MAX_IMMEDIATE_TRANSITIONS = 8U;
    const auto timing = source.traits.adsr.timing;
    for (uint8_t transition = 0U;
         transition < MAX_IMMEDIATE_TRANSITIONS;
         ++transition) {
        if (state.stage == ProjectModulationAdsrStage::IDLE) return 0.0f;
        if (state.stage == ProjectModulationAdsrStage::SUSTAIN) {
            return adsrSustainLevel(source);
        }

        const auto stage = state.stage;
        const uint32_t duration = adsrStageDuration(source, stage);
        const float target = adsrStageTarget(source, state, stage);
        float progress = 0.0f;
        if (duration == 0U || adsrStageCompleteAndProgress(
                state,
                time,
                timing,
                duration,
                progress
            )) {
            state.stageStartLevelQ15 = packQ15(target);
            advanceAdsrAnchor(state, duration);
            state.stage = nextAdsrStage(stage);
            continue;
        }

        const float start = std::clamp(
            unpackQ15(state.stageStartLevelQ15),
            0.0f,
            1.0f
        );
        const float shaped = evaluateProjectAdsrProgress(
            source.traits.adsr.curve,
            progress
        );
        return std::clamp(start + (target - start) * shaped, 0.0f, 1.0f);
    }
    return adsrStageTarget(source, state, state.stage);
}

bool adsrAcceptedNote(
    const ProjectModulationRuntimeAdsrState& state,
    uint8_t note
) {
    if (note > 127U) return false;
    return (state.acceptedNotes[note >> 5U] &
            (1UL << (note & 31U))) != 0U;
}

bool addAdsrAcceptedNote(
    ProjectModulationRuntimeAdsrState& state,
    uint8_t note
) {
    if (note > 127U) return false;
    const uint32_t mask = 1UL << (note & 31U);
    auto& word = state.acceptedNotes[note >> 5U];
    if ((word & mask) != 0U) return false;
    word |= mask;
    if (state.heldNoteCount < 128U) ++state.heldNoteCount;
    return true;
}

bool removeAdsrAcceptedNote(
    ProjectModulationRuntimeAdsrState& state,
    uint8_t note
) {
    if (note > 127U) return false;
    const uint32_t mask = 1UL << (note & 31U);
    auto& word = state.acceptedNotes[note >> 5U];
    if ((word & mask) == 0U) return false;
    word &= ~mask;
    if (state.heldNoteCount > 0U) --state.heldNoteCount;
    return true;
}

void clearAdsrAcceptedNotes(ProjectModulationRuntimeAdsrState& state) {
    state.acceptedNotes = {};
    state.heldNoteCount = 0U;
}

int16_t advanceAdsrSmoothQ15(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationRuntimeAdsrState& state,
    const ProjectControlTimeSnapshot& time,
    uint32_t previousMs,
    uint32_t previousMusicalTick,
    uint16_t previousMusicalFractionQ16,
    float rawValue
) {
    const int16_t target = packQ15(std::clamp(rawValue, 0.0f, 1.0f));
    const uint16_t base = source.parameters.adsr.smooth;
    if (base == 0U) {
        state.smoothedLevelQ15 = target;
        return target;
    }

    uint64_t elapsed = 0U;
    uint64_t timeConstant = 0U;
    if (source.traits.adsr.timing == ModulatorTimingMode::FREE) {
        elapsed = time.monotonicMs - previousMs;
        timeConstant = base;
    } else {
        uint32_t ticks = 0U;
        uint16_t fraction = 0U;
        elapsedMusicalTime(
            time,
            previousMusicalTick,
            previousMusicalFractionQ16,
            ticks,
            fraction
        );
        elapsed = (static_cast<uint64_t>(ticks) << 16U) | fraction;
        timeConstant = static_cast<uint64_t>(
            resolveModulatorEnvelopeSyncTicks(
                base,
                modulatorAdsrFeel(
                    source.parameters.adsr.traits,
                    ModulatorEnvelopeTimeParameter::SMOOTH
                )
            )
        ) << 16U;
    }
    if (timeConstant == 0U) {
        state.smoothedLevelQ15 = target;
        return target;
    }
    if (elapsed == 0U || state.smoothedLevelQ15 == target) {
        return state.smoothedLevelQ15;
    }

    const int32_t previous = state.smoothedLevelQ15;
    const int32_t difference = static_cast<int32_t>(target) - previous;
    const uint64_t magnitude = static_cast<uint64_t>(
        difference < 0 ? -difference : difference
    );
    const uint64_t denominator = timeConstant + elapsed;
    uint64_t step = (magnitude * elapsed + denominator / 2U) /
        denominator;
    if (step == 0U) step = 1U;
    const int32_t signedStep = difference < 0
        ? -static_cast<int32_t>(step)
        : static_cast<int32_t>(step);
    int32_t next = previous + signedStep;
    if ((difference > 0 && next > target) ||
        (difference < 0 && next < target)) {
        next = target;
    }
    state.smoothedLevelQ15 = static_cast<int16_t>(next);
    return state.smoothedLevelQ15;
}

void applyProjectAdsrTriggerEvent(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationRuntimeAdsrState& state,
    const ProjectControlTimeSnapshot& time,
    const ProjectModulationTriggerEvent& event,
    float& value
) {
    if (event.edge == ProjectModulationTriggerEdge::GATE_ON) {
        if (event.trigger.data > 127U) return;
        const bool gateWasClosed = state.heldNoteCount == 0U;
        (void)addAdsrAcceptedNote(state, event.trigger.data);
        if (source.traits.adsr.retrigger ==
                ModulatorAdsrRetriggerMode::RETRIGGER ||
            gateWasClosed) {
            anchorAdsrStage(
                state,
                ProjectModulationAdsrStage::DELAY,
                value,
                time,
                source.traits.adsr.timing
            );
            if (source.parameters.adsr.delay == 0U) {
                value = advanceAdsrRawToTime(source, state, time);
            }
        }
        return;
    }

    if (event.edge != ProjectModulationTriggerEdge::GATE_OFF ||
        !removeAdsrAcceptedNote(state, event.trigger.data)) {
        return;
    }
    if (state.heldNoteCount == 0U) {
        anchorAdsrStage(
            state,
            ProjectModulationAdsrStage::RELEASE,
            value,
            time,
            source.traits.adsr.timing
        );
        if (source.parameters.adsr.release == 0U) {
            value = advanceAdsrRawToTime(source, state, time);
        }
    }
}

bool sourceAcceptsTriggerEvent(
    const ProjectModulationRuntimeSource& source,
    const ProjectModulationRuntimeSourceState& state,
    const ProjectModulationTriggerEvent& event
) {
    if (source.trigger.kind != event.trigger.kind ||
        source.trigger.track != event.trigger.track) {
        return false;
    }
    if (source.trigger.kind != ModulationTriggerKind::TRACK_NOTE) return true;
    if (event.trigger.data > 127U) return false;
    if (source.kind == ModulatorKind::ADSR &&
        event.edge == ProjectModulationTriggerEdge::GATE_OFF) {
        return adsrAcceptedNote(state.payload.adsr, event.trigger.data);
    }
    return triggerIdentityMatches(source.trigger, event.trigger) &&
        event.velocity >= source.triggerVelocityMin &&
        event.velocity <= source.triggerVelocityMax;
}

void releaseAdsrAfterDroppedTrigger(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationRuntimeAdsrState& state,
    const ProjectControlTimeSnapshot& time,
    float& value
) {
    if (state.heldNoteCount == 0U) return;
    clearAdsrAcceptedNotes(state);
    anchorAdsrStage(
        state,
        ProjectModulationAdsrStage::RELEASE,
        value,
        time,
        source.traits.adsr.timing
    );
    if (source.parameters.adsr.release == 0U) {
        value = advanceAdsrRawToTime(source, state, time);
    }
}

bool routeProjectTriggerFrame(
    const ProjectModulationRuntimePlan& plan,
    const ProjectControlTimeSnapshot& time,
    const ProjectModulationTriggerFrame* triggers,
    ProjectControlRuntimeState& state,
    float* sourceValues
) {
    if (triggers == nullptr) return true;
    if (triggers->droppedEventCount != 0U) {
        for (uint16_t sourceIndex = 0U;
             sourceIndex < plan.sourceCount;
             ++sourceIndex) {
            const auto& source = plan.sources[sourceIndex];
            if (source.kind != ModulatorKind::ADSR) continue;
            releaseAdsrAfterDroppedTrigger(
                source,
                state.sources[sourceIndex].payload.adsr,
                time,
                sourceValues[sourceIndex]
            );
        }
    }
    if (triggers->count == 0U) return true;
    for (uint16_t eventIndex = 0U;
         eventIndex < triggers->count;
         ++eventIndex) {
        const auto& event = triggers->events[eventIndex];
        if (event.trigger.track >= PROJECT_MODULATION_TRACK_COUNT ||
            event.trigger.channel >= 16U ||
            static_cast<uint8_t>(event.trigger.kind) >=
                PROJECT_MODULATION_TRIGGER_KIND_COUNT) {
            continue;
        }
        const uint16_t bucket = projectModulationTriggerBucketIndex(
            event.trigger
        );
        const uint16_t start = plan.triggerBucketOffset[bucket];
        const uint16_t end = plan.triggerBucketOffset[bucket + 1U];
        if (start > end || end > plan.triggerRouteCount) return false;
        for (uint16_t route = start; route < end; ++route) {
            const uint16_t sourceIndex = plan.triggerSourceOrder[route];
            if (sourceIndex >= plan.sourceCount) return false;
            const auto& source = plan.sources[sourceIndex];
            auto& sourceState = state.sources[sourceIndex];
            if (!sourceAcceptsTriggerEvent(source, sourceState, event)) {
                continue;
            }
            if (source.kind == ModulatorKind::ADSR) {
                applyProjectAdsrTriggerEvent(
                    source,
                    sourceState.payload.adsr,
                    time,
                    event,
                    sourceValues[sourceIndex]
                );
            } else if (
                source.kind == ModulatorKind::LFO &&
                source.traits.lfo.retrigger ==
                    ModulatorRetriggerPolicy::EXPLICIT_TRIGGER &&
                event.edge != ProjectModulationTriggerEdge::GATE_OFF
            ) {
                sourceState.payload.lfo.explicitMusicalAnchorTick =
                    time.musicalTick;
                sourceState.payload.lfo.explicitMonotonicAnchorMs =
                    time.monotonicMs;
                sourceState.payload.lfo.explicitMusicalAnchorFractionQ16 =
                    time.musicalTickFractionQ16;
                sourceState.payload.lfo.explicitlyTriggered = true;
            }
        }
    }
    return true;
}

}  // namespace project_control_runtime_detail

using namespace project_control_runtime_detail;

}  // namespace core::state::modulation

#include "state/modulation/ProjectModulationDomainOps.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectModulationDomainOpsInternal.hpp"

namespace core::state::modulation {

using namespace project_modulation_detail;
FLASHMEM bool validProjectModulationDomain(
    const ProjectModulationState& state,
    const ProjectCurveArena& arena,
    const ProjectAutomationCurveDirectory* automation
) {
    if (state.sourceCount > PROJECT_MODULATOR_CAPACITY ||
        state.outputBindingCount > PROJECT_MODULATION_BINDING_CAPACITY ||
        state.triggerBindingCount > PROJECT_MODULATION_TRIGGER_CAPACITY ||
        state.destinationScaleCount >
            PROJECT_MODULATION_DESTINATION_SCALE_CAPACITY ||
        arena.recordCount > PROJECT_CURVE_LIVE_CAPACITY ||
        arena.recordCount > PROJECT_CURVE_RECORD_CAPACITY ||
        arena.pointCount > PROJECT_CURVE_POINT_CAPACITY ||
        (automation != nullptr &&
         (automation->entryCount > PROJECT_AUTOMATION_ENTRY_CAPACITY ||
          automation->reserved != 0U))) {
        return false;
    }

    uint16_t previousScaleAddress = 0U;
    for (uint16_t index = 0; index < state.destinationScaleCount; ++index) {
        const auto& entry = state.destinationScales[index];
        const uint16_t address = modulationDestinationStableAddress(
            entry.destination
        );
        if (!modulationDestinationValid(entry.destination) ||
            entry.scaleQ15 == PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15 ||
            !destinationHasBinding(state, entry.destination) ||
            (index > 0U && address <= previousScaleAddress)) {
            return false;
        }
        previousScaleAddress = address;
    }

    if (automation != nullptr) {
        for (uint16_t entry = 0; entry < automation->entryCount; ++entry) {
            const auto& current = automation->entries[entry];
            if (!modulationDestinationValid(current.destination) ||
                !valid(current.curveId) ||
                (current.flags & ~PROJECT_AUTOMATION_CURVE_FLAG_ENABLED) != 0U ||
                !allZeroBytes(
                    current.reserved.data(),
                    current.reserved.size()
                )) {
                return false;
            }
            for (uint16_t prior = 0; prior < entry; ++prior) {
                if (automation->entries[prior].destination ==
                    current.destination) {
                    return false;
                }
            }
        }
    }

    for (uint16_t index = 0; index < state.sourceCount; ++index) {
        const auto& source = state.sources[index];
        const uint8_t expectedSchema = source.kind == ModulatorKind::ADSR
            ? PROJECT_MODULATOR_ADSR_SCHEMA_VERSION
            : PROJECT_MODULATOR_SOURCE_SCHEMA_VERSION;
        if (!valid(source.id) || !validSourceName(source) ||
            source.schemaVersion != expectedSchema ||
            (source.flags & ~SOURCE_FLAGS) != 0U ||
            static_cast<uint8_t>(source.kind) >
                static_cast<uint8_t>(ModulatorKind::ADSR)) {
            return false;
        }
        for (uint16_t prior = 0; prior < index; ++prior) {
            if (state.sources[prior].id == source.id) return false;
        }
        if (state.nextSourceId != 0 &&
            source.id.value >= state.nextSourceId) {
            return false;
        }
        if (source.kind == ModulatorKind::LFO) {
            if (!validLfoParameters(source.parameters.lfo)) {
                return false;
            }
        } else if (source.kind == ModulatorKind::ADSR) {
            if (!validAdsrParameters(source.parameters.adsr) ||
                !parameterTailZero(
                    source.parameters,
                    sizeof(ModulatorAdsrParameters)
                )) {
                return false;
            }
        } else if (source.kind == ModulatorKind::RECORDED_SHAPE) {
            if (!valid(source.parameters.recordedCurveId) ||
                curveIndex(arena, source.parameters.recordedCurveId) < 0 ||
                !parameterTailZero(
                    source.parameters,
                    sizeof(ProjectCurveId)
                )) {
                return false;
            }
        } else {
            return false;
        }
    }

    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        const auto& binding = state.outputBindings[index];
        const auto* source = findProjectModulator(state, binding.sourceId);
        if (!valid(binding.id) || source == nullptr ||
            !modulationDestinationValid(binding.destination) ||
            binding.amountQ15 == std::numeric_limits<int16_t>::min() ||
            static_cast<uint8_t>(binding.application) >
                static_cast<uint8_t>(ModulationApplication::FROM_BASE) ||
            binding.transfer != ModulationTransfer::LINEAR ||
            (binding.flags & ~BINDING_FLAGS) != 0U ||
            binding.reserved != 0U ||
            (state.nextBindingId != 0 &&
             binding.id.value >= state.nextBindingId)) {
            return false;
        }
        for (uint16_t prior = 0; prior < index; ++prior) {
            const auto& other = state.outputBindings[prior];
            if (other.id == binding.id ||
                (other.sourceId == binding.sourceId &&
                 other.destination == binding.destination)) {
                return false;
            }
        }
    }

    for (uint16_t index = 0; index < state.triggerBindingCount; ++index) {
        const auto& trigger = state.triggerBindings[index];
        if (!valid(trigger.id) ||
            findProjectModulator(state, trigger.sourceId) == nullptr ||
            !validTriggerFilter(trigger.trigger) ||
            !validVelocityRange(trigger.velocityMin, trigger.velocityMax) ||
            (trigger.flags & ~TRIGGER_FLAGS) != 0U ||
            trigger.reserved != 0U ||
            (state.nextBindingId != 0 &&
             trigger.id.value >= state.nextBindingId)) {
            return false;
        }
        for (uint16_t prior = 0; prior < state.outputBindingCount; ++prior) {
            if (state.outputBindings[prior].id == trigger.id) return false;
        }
        for (uint16_t prior = 0; prior < index; ++prior) {
            if (state.triggerBindings[prior].id == trigger.id ||
                state.triggerBindings[prior].sourceId == trigger.sourceId) {
                return false;
            }
        }
    }

    uint32_t coveredPointCount = 0;
    for (uint16_t index = 0; index < arena.recordCount; ++index) {
        const auto& curve = arena.records[index];
        if (!valid(curve.id) || curve.referenceCount == 0 ||
            curve.pointCount == 0 || curve.flags != 0U ||
            static_cast<uint32_t>(curve.pointOffset) + curve.pointCount >
                arena.pointCount ||
            (arena.nextCurveId != 0 && curve.id.value >= arena.nextCurveId)) {
            return false;
        }
        for (uint16_t prior = 0; prior < index; ++prior) {
            const auto& other = arena.records[prior];
            if (other.id == curve.id) return false;
            const uint32_t begin = curve.pointOffset;
            const uint32_t end = begin + curve.pointCount;
            const uint32_t otherBegin = other.pointOffset;
            const uint32_t otherEnd = otherBegin + other.pointCount;
            if (begin < otherEnd && otherBegin < end) return false;
        }
        ProjectCurveSpec spec{};
        spec.sourceDurationTicks = curve.sourceDurationTicks;
        spec.durationTicks = curve.durationTicks;
        spec.windowOffsetTicks = curve.windowOffsetTicks;
        spec.interpolation = curve.interpolation;
        spec.valueDomain = curve.valueDomain;
        spec.origin = curve.origin;
        if (!validProjectCurveSpec(
                spec,
                arena.points.data() + curve.pointOffset,
                curve.pointCount
            )) {
            return false;
        }
        uint16_t references = 0;
        bool referencedByRecordedShape = false;
        bool referencedByAutomation = false;
        for (uint16_t source = 0; source < state.sourceCount; ++source) {
            if (state.sources[source].kind == ModulatorKind::RECORDED_SHAPE &&
                state.sources[source].parameters.recordedCurveId == curve.id) {
                ++references;
                referencedByRecordedShape = true;
            }
        }
        if (automation != nullptr) {
            for (uint16_t entry = 0; entry < automation->entryCount; ++entry) {
                if (automation->entries[entry].curveId == curve.id) {
                    if (curve.valueDomain != ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR ||
                        curve.origin != ProjectCurveOrigin::NATIVE) {
                        return false;
                    }
                    ++references;
                    referencedByAutomation = true;
                }
            }
        }
        // The two chunks own disjoint curve directories. Sharing across them
        // would duplicate identity during decode even if values remained equal.
        if (referencedByRecordedShape && referencedByAutomation) return false;
        if (references != curve.referenceCount) return false;
        coveredPointCount += curve.pointCount;
    }
    if (coveredPointCount != arena.pointCount) return false;
    if (automation != nullptr) {
        for (uint16_t entry = 0; entry < automation->entryCount; ++entry) {
            if (curveIndex(arena, automation->entries[entry].curveId) < 0) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace core::state::modulation

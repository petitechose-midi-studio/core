#include "state/modulation/ProjectModulationRuntimePlan.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <config/PlatformCompat.hpp>

namespace core::state::modulation {

namespace {

FLASHMEM bool destinationActiveInContext(
    const ModulationDestination& destination,
    const ProjectModulationCompileContext& context
) {
    return (context.enabledTrackMask & (1U << destination.track)) != 0 &&
           context.activePage[destination.track] == destination.page &&
           (context.activeMacroMask[destination.track] &
            (1U << destination.macro)) != 0;
}

FLASHMEM bool bindingActiveInContext(
    const ModulationBindingState& binding,
    const ProjectModulationCompileContext& context
) {
    return destinationActiveInContext(binding.destination, context);
}

FLASHMEM bool automationActiveInContext(
    const ProjectAutomationCurveEntry& entry,
    const ProjectModulationCompileContext& context
) {
    return destinationActiveInContext(entry.destination, context);
}

FLASHMEM int16_t sourceIndex(
    const ProjectModulationState& state,
    ModulatorId id
) {
    for (uint16_t index = 0; index < state.sourceCount; ++index) {
        if (state.sources[index].id == id) return static_cast<int16_t>(index);
    }
    return -1;
}

FLASHMEM int16_t curveIndex(
    const ProjectCurveArena& arena,
    ProjectCurveId id
) {
    for (uint16_t index = 0; index < arena.recordCount; ++index) {
        if (arena.records[index].id == id) return static_cast<int16_t>(index);
    }
    return -1;
}

FLASHMEM uint32_t contextHash(
    const ProjectModulationCompileContext& context
) {
    uint32_t hash = 2166136261U;
    const auto append = [&hash](uint8_t value) {
        hash ^= value;
        hash *= 16777619U;
    };
    append(static_cast<uint8_t>(context.enabledTrackMask & 0xFFU));
    append(static_cast<uint8_t>(context.enabledTrackMask >> 8U));
    for (uint8_t track = 0; track < PROJECT_MODULATION_TRACK_COUNT; ++track) {
        append(context.activePage[track]);
        append(context.activeMacroMask[track]);
    }
    return hash;
}

FLASHMEM int16_t runtimeDestinationIndex(
    const ProjectModulationRuntimePlan& plan,
    const ModulationDestination& destination
) {
    for (uint16_t index = 0; index < plan.destinationCount; ++index) {
        if (plan.destinations[index].destination == destination) {
            return static_cast<int16_t>(index);
        }
    }
    return -1;
}

FLASHMEM bool bindingComesBefore(
    const ProjectModulationRuntimePlan& plan,
    uint16_t lhsIndex,
    uint16_t rhsIndex
) {
    const auto& lhs = plan.bindings[lhsIndex];
    const auto& rhs = plan.bindings[rhsIndex];
    const uint16_t lhsAddress =
        plan.destinations[lhs.destinationIndex].stableAddress;
    const uint16_t rhsAddress =
        plan.destinations[rhs.destinationIndex].stableAddress;
    return lhsAddress < rhsAddress ||
           (lhsAddress == rhsAddress && lhs.id.value < rhs.id.value);
}

}  // namespace

FLASHMEM bool validProjectModulationCompileContext(
    const ProjectModulationCompileContext& context
) {
    for (uint8_t track = 0; track < PROJECT_MODULATION_TRACK_COUNT; ++track) {
        if (context.activePage[track] >= PROJECT_MODULATION_PAGE_COUNT) {
            return false;
        }
    }
    return true;
}

FLASHMEM uint32_t projectModulationCompileContextHash(
    const ProjectModulationCompileContext& context
) {
    return contextHash(context);
}

FLASHMEM ProjectModulationCompileResult compileRuntimePlan(
    const ProjectModulationState& state,
    const ProjectCurveArena& arena,
    const ProjectAutomationCurveDirectory* automation,
    const ProjectModulationCompileContext& context,
    ProjectModulationRuntimePlan& out
) {
    if (!validProjectModulationCompileContext(context)) {
        return {ProjectModulationCompileStatus::INVALID_CONTEXT};
    }
    if (!validProjectModulationDomain(state, arena, automation)) {
        return {ProjectModulationCompileStatus::INVALID_DOMAIN};
    }

    uint16_t activeBindingCount = 0;
    uint16_t inactiveBindingCount = 0;
    uint16_t activeAutomationCount = 0;
    uint16_t inactiveAutomationCount = 0;
    uint16_t destinationCount = 0;
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        const auto& binding = state.outputBindings[index];
        if (!bindingActiveInContext(binding, context)) {
            ++inactiveBindingCount;
            continue;
        }
        ++activeBindingCount;
        bool firstForDestination = true;
        for (uint16_t prior = 0; prior < index; ++prior) {
            if (bindingActiveInContext(state.outputBindings[prior], context) &&
                state.outputBindings[prior].destination == binding.destination) {
                firstForDestination = false;
                break;
            }
        }
        if (firstForDestination) ++destinationCount;
    }

    if (automation != nullptr) {
        for (uint16_t index = 0; index < automation->entryCount; ++index) {
            const auto& entry = automation->entries[index];
            if (!automationActiveInContext(entry, context)) {
                ++inactiveAutomationCount;
                continue;
            }
            ++activeAutomationCount;
            bool firstForDestination = true;
            for (uint16_t bindingIndex = 0;
                 bindingIndex < state.outputBindingCount;
                 ++bindingIndex) {
                const auto& binding = state.outputBindings[bindingIndex];
                if (bindingActiveInContext(binding, context) &&
                    binding.destination == entry.destination) {
                    firstForDestination = false;
                    break;
                }
            }
            for (uint16_t prior = 0;
                 firstForDestination && prior < index;
                 ++prior) {
                if (automationActiveInContext(automation->entries[prior], context) &&
                    automation->entries[prior].destination == entry.destination) {
                    firstForDestination = false;
                }
            }
            if (firstForDestination) ++destinationCount;
        }
    }
    if (state.sourceCount > PROJECT_MODULATOR_CAPACITY ||
        activeBindingCount > PROJECT_MODULATION_BINDING_CAPACITY ||
        destinationCount > PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY) {
        return {
            ProjectModulationCompileStatus::CAPACITY_EXCEEDED,
            state.sourceCount,
            activeBindingCount,
            destinationCount,
            inactiveBindingCount,
            activeAutomationCount,
            inactiveAutomationCount,
        };
    }

    // Every possible failure has been resolved. Publishing can now be direct.
    out = {};
    out.sourceCount = state.sourceCount;
    out.bindingCount = activeBindingCount;
    out.destinationCount = 0;
    out.inactiveBindingCount = inactiveBindingCount;
    out.automationCount = activeAutomationCount;
    out.inactiveAutomationCount = inactiveAutomationCount;
    out.contextHash = contextHash(context);

    for (uint16_t index = 0; index < state.sourceCount; ++index) {
        const auto& source = state.sources[index];
        auto& runtime = out.sources[index];
        runtime.id = source.id;
        runtime.kind = source.kind;
        runtime.flags = source.flags;
        if (source.kind == ModulatorKind::RECORDED_SHAPE) {
            runtime.curveRecordIndex = static_cast<uint16_t>(
                curveIndex(arena, source.parameters.recordedCurveId)
            );
        } else {
            runtime.periodTicks = source.parameters.lfo.periodTicks;
            runtime.freePeriodMs = source.parameters.lfo.freePeriodMs;
            runtime.phaseQ15 = source.parameters.lfo.phaseQ15;
            runtime.shape = source.parameters.lfo.shape;
            runtime.retrigger = source.parameters.lfo.retrigger;
            runtime.timing = source.parameters.lfo.timing;
        }
        for (uint16_t triggerIndex = 0;
             triggerIndex < state.triggerBindingCount;
             ++triggerIndex) {
            const auto& trigger = state.triggerBindings[triggerIndex];
            if (trigger.sourceId != source.id) continue;
            runtime.trigger = trigger.trigger;
            runtime.triggerFlags = trigger.flags;
            break;
        }
    }

    // Insert logical destinations in stable-address order.
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        const auto& binding = state.outputBindings[index];
        if (!bindingActiveInContext(binding, context) ||
            runtimeDestinationIndex(out, binding.destination) >= 0) {
            continue;
        }
        const uint16_t address = modulationDestinationStableAddress(
            binding.destination
        );
        uint16_t insertion = 0;
        while (insertion < out.destinationCount &&
               out.destinations[insertion].stableAddress < address) {
            ++insertion;
        }
        for (uint16_t cursor = out.destinationCount;
             cursor > insertion;
             --cursor) {
            out.destinations[cursor] = out.destinations[cursor - 1U];
        }
        auto& destination = out.destinations[insertion];
        destination = {};
        destination.destination = binding.destination;
        destination.stableAddress = address;
        destination.minimum = 0.0f;
        destination.maximum = 1.0f;
        ++out.destinationCount;
    }

    // Absolute Automation participates in the same logical destination pass,
    // including targets that currently have no relative assignments.
    if (automation != nullptr) {
        for (uint16_t index = 0; index < automation->entryCount; ++index) {
            const auto& entry = automation->entries[index];
            if (!automationActiveInContext(entry, context)) continue;
            int16_t destinationIndex = runtimeDestinationIndex(
                out,
                entry.destination
            );
            if (destinationIndex < 0) {
                const uint16_t address = modulationDestinationStableAddress(
                    entry.destination
                );
                uint16_t insertion = 0;
                while (insertion < out.destinationCount &&
                       out.destinations[insertion].stableAddress < address) {
                    ++insertion;
                }
                for (uint16_t cursor = out.destinationCount;
                     cursor > insertion;
                     --cursor) {
                    out.destinations[cursor] = out.destinations[cursor - 1U];
                }
                auto& destination = out.destinations[insertion];
                destination = {};
                destination.destination = entry.destination;
                destination.stableAddress = address;
                destination.minimum = 0.0f;
                destination.maximum = 1.0f;
                ++out.destinationCount;
                destinationIndex = static_cast<int16_t>(insertion);
            }
            auto& destination = out.destinations[
                static_cast<uint16_t>(destinationIndex)
            ];
            destination.automationCurveRecordIndex = static_cast<uint16_t>(
                curveIndex(arena, entry.curveId)
            );
            if ((entry.flags & PROJECT_AUTOMATION_CURVE_FLAG_ENABLED) != 0U) {
                destination.flags = static_cast<uint8_t>(
                    destination.flags |
                    PROJECT_CONTROL_RUNTIME_DESTINATION_FLAG_AUTOMATION_ENABLED
                );
            }
        }
    }

    uint16_t runtimeBindingCount = 0;
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        const auto& binding = state.outputBindings[index];
        if (!bindingActiveInContext(binding, context)) continue;
        auto& runtime = out.bindings[runtimeBindingCount];
        runtime.id = binding.id;
        runtime.sourceIndex = static_cast<uint16_t>(
            sourceIndex(state, binding.sourceId)
        );
        runtime.destinationIndex = static_cast<uint16_t>(
            runtimeDestinationIndex(out, binding.destination)
        );
        runtime.amountQ15 = binding.amountQ15;
        runtime.slewMs = binding.slewMs;
        runtime.inputRange = binding.inputRange;
        runtime.transfer = binding.transfer;
        runtime.flags = binding.flags;
        out.bindingOrder[runtimeBindingCount] = runtimeBindingCount;
        ++runtimeBindingCount;
    }

    // Stable insertion sort: deterministic accumulation independent of edit order.
    for (uint16_t index = 1; index < out.bindingCount; ++index) {
        const uint16_t value = out.bindingOrder[index];
        uint16_t cursor = index;
        while (cursor > 0 && bindingComesBefore(
                out,
                value,
                out.bindingOrder[cursor - 1U]
            )) {
            out.bindingOrder[cursor] = out.bindingOrder[cursor - 1U];
            --cursor;
        }
        out.bindingOrder[cursor] = value;
    }
    for (uint16_t order = 0; order < out.bindingCount; ++order) {
        const auto& binding = out.bindings[out.bindingOrder[order]];
        auto& destination = out.destinations[binding.destinationIndex];
        if (destination.bindingCount == 0) destination.firstBinding = order;
        ++destination.bindingCount;
    }

    return {
        ProjectModulationCompileStatus::OK,
        out.sourceCount,
        out.bindingCount,
        out.destinationCount,
        out.inactiveBindingCount,
        out.automationCount,
        out.inactiveAutomationCount,
    };
}

FLASHMEM ProjectModulationCompileResult compileProjectModulationRuntimePlan(
    const ProjectModulationState& state,
    const ProjectCurveArena& arena,
    const ProjectModulationCompileContext& context,
    ProjectModulationRuntimePlan& out
) {
    return compileRuntimePlan(state, arena, nullptr, context, out);
}

FLASHMEM ProjectModulationCompileResult compileProjectControlRuntimePlan(
    const ProjectControlDomainState& state,
    const ProjectModulationCompileContext& context,
    ProjectModulationRuntimePlan& out
) {
    return compileRuntimePlan(
        state.modulation,
        state.curves,
        &state.automation,
        context,
        out
    );
}

FLASHMEM ProjectModulationResolveResult resolveProjectModulationDestination(
    const ProjectModulationRuntimePlan& plan,
    uint16_t destinationIndex,
    const float* bipolarSourceValues,
    float baseValue
) {
    ProjectModulationResolveResult resolved{};
    if (destinationIndex >= plan.destinationCount ||
        bipolarSourceValues == nullptr) {
        return resolved;
    }
    if (!std::isfinite(baseValue)) baseValue = 0.0f;

    const auto& destination = plan.destinations[destinationIndex];
    float modulation = 0.0f;
    for (uint16_t relative = 0;
         relative < destination.bindingCount;
         ++relative) {
        const uint16_t order = static_cast<uint16_t>(
            destination.firstBinding + relative
        );
        if (order >= plan.bindingCount) return resolved;
        const auto& binding = plan.bindings[plan.bindingOrder[order]];
        if (binding.sourceIndex >= plan.sourceCount ||
            (binding.flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) == 0U ||
            (plan.sources[binding.sourceIndex].flags &
             PROJECT_MODULATOR_FLAG_ENABLED) == 0U) {
            continue;
        }
        float sourceValue = bipolarSourceValues[binding.sourceIndex];
        if (!std::isfinite(sourceValue)) sourceValue = 0.0f;
        sourceValue = std::clamp(sourceValue, -1.0f, 1.0f);
        if (binding.inputRange == ModulationInputRange::UNIPOLAR) {
            sourceValue = (sourceValue + 1.0f) * 0.5f;
        }
        const float amount = static_cast<float>(binding.amountQ15) / 32767.0f;
        modulation += sourceValue * amount;
        ++resolved.contributionCount;
    }

    const float raw = baseValue + modulation;
    resolved.value = std::clamp(raw, destination.minimum, destination.maximum);
    resolved.modulation = modulation;
    resolved.clipped = resolved.value != raw;
    resolved.valid = true;
    return resolved;
}

}  // namespace core::state::modulation

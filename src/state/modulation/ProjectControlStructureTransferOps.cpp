#include "state/modulation/ProjectControlStructureTransferOps.hpp"

#include <array>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::modulation {

namespace {

using SourceRemapTable =
    std::array<ModulatorId, PROJECT_MODULATOR_CAPACITY>;

FLASHMEM bool mapSourceDestination(
    const ProjectControlStructureTransferPlan& plan,
    const ModulationDestination& source,
    ModulationDestination& target
) {
    for (uint8_t index = 0U; index < plan.count; ++index) {
        const auto& entry = plan.entries[index];
        if (source.track != entry.sourceTrack ||
            (!entry.wholeTrack && source.page != entry.sourcePage)) {
            continue;
        }
        target = source;
        target.track = entry.targetTrack;
        target.page = entry.wholeTrack
            ? source.page
            : entry.targetPage;
        return true;
    }
    return false;
}

FLASHMEM bool targetDestinationOwned(
    const ProjectControlStructureTransferPlan& plan,
    const ModulationDestination& destination
) {
    for (uint8_t index = 0U; index < plan.count; ++index) {
        const auto& entry = plan.entries[index];
        if (destination.track != entry.targetTrack) continue;
        if (entry.wholeTrack || destination.page == entry.targetPage) {
            return true;
        }
    }
    return false;
}

FLASHMEM bool mapSourceTriggerTrack(
    const ProjectControlStructureTransferPlan& plan,
    uint8_t sourceTrack,
    uint8_t& targetTrack
) {
    bool found = false;
    uint8_t mapped = sourceTrack;
    for (uint8_t index = 0U; index < plan.count; ++index) {
        const auto& entry = plan.entries[index];
        if (entry.sourceTrack != sourceTrack) continue;
        if (found && mapped != entry.targetTrack) return false;
        mapped = entry.targetTrack;
        found = true;
    }
    if (!found) return false;
    targetTrack = mapped;
    return true;
}

FLASHMEM ProjectCurveSpec curveSpec(
    const ProjectCurveRecord& record
) {
    return {
        .sourceDurationTicks = record.sourceDurationTicks,
        .durationTicks = record.durationTicks,
        .windowOffsetTicks = record.windowOffsetTicks,
        .interpolation = record.interpolation,
        .valueDomain = record.valueDomain,
        .origin = record.origin,
    };
}

FLASHMEM bool clearMappedDestinations(
    ProjectControlDomainState& target,
    const ProjectControlStructureTransferPlan& plan
) {
    for (uint16_t cursor = 0U;
         cursor < target.automation.entryCount;) {
        const auto destination =
            target.automation.entries[cursor].destination;
        if (!targetDestinationOwned(plan, destination)) {
            ++cursor;
            continue;
        }
        if (!removeProjectAutomationCurve(
                target.automation,
                target.curves,
                destination
            ).changed()) {
            return false;
        }
    }

    // Sources whose complete output footprint belongs to the replaced
    // destination are structural children of that footprint. Remove them
    // before the edges so repeated Paste cannot accumulate orphaned clones.
    // Shared Project sources survive and only lose the mapped edges below.
    for (uint16_t sourceCursor = 0U;
         sourceCursor < target.modulation.sourceCount;) {
        const auto sourceId =
            target.modulation.sources[sourceCursor].id;
        bool hasOwnedOutput = false;
        bool hasOutsideOutput = false;
        for (uint16_t bindingIndex = 0U;
             bindingIndex <
                 target.modulation.outputBindingCount;
             ++bindingIndex) {
            const auto& binding =
                target.modulation.outputBindings[bindingIndex];
            if (binding.sourceId != sourceId) continue;
            if (targetDestinationOwned(
                    plan,
                    binding.destination
                )) {
                hasOwnedOutput = true;
            } else {
                hasOutsideOutput = true;
            }
        }
        if (hasOwnedOutput && !hasOutsideOutput) {
            if (!deleteProjectModulator(
                    target.modulation,
                    target.curves,
                    sourceId
                ).changed()) {
                return false;
            }
            continue;
        }
        ++sourceCursor;
    }

    for (uint16_t cursor = 0U;
         cursor < target.modulation.outputBindingCount;) {
        const auto binding =
            target.modulation.outputBindings[cursor];
        if (!targetDestinationOwned(plan, binding.destination)) {
            ++cursor;
            continue;
        }
        if (!removeProjectModulationBinding(
                target.modulation,
                binding.id
            ).changed()) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool copyAutomation(
    ProjectControlDomainState& target,
    const ProjectControlDomainState& source,
    const ProjectControlStructureTransferPlan& plan
) {
    for (uint16_t index = 0U;
         index < source.automation.entryCount;
         ++index) {
        const auto& entry = source.automation.entries[index];
        ModulationDestination destination{};
        if (!mapSourceDestination(
                plan,
                entry.destination,
                destination
            )) {
            continue;
        }
        const auto* record = findProjectCurve(
            source.curves,
            entry.curveId
        );
        if (record == nullptr || record->pointCount == 0U ||
            static_cast<uint32_t>(record->pointOffset) +
                    record->pointCount >
                source.curves.pointCount) {
            return false;
        }
        const auto result = setProjectAutomationCurve(
            target.automation,
            target.curves,
            destination,
            curveSpec(*record),
            source.curves.points.data() + record->pointOffset,
            record->pointCount,
            (entry.flags &
             PROJECT_AUTOMATION_CURVE_FLAG_ENABLED) != 0U
        );
        if (!result.changed() &&
            result.status != ProjectModulationStatus::NO_CHANGE) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool sourceIsLocalToTransfer(
    const ProjectControlDomainState& source,
    const ProjectControlStructureTransferPlan& plan,
    ModulatorId sourceId
) {
    bool hasMappedOutput = false;
    for (uint16_t index = 0U;
         index < source.modulation.outputBindingCount;
         ++index) {
        const auto& binding =
            source.modulation.outputBindings[index];
        if (binding.sourceId != sourceId) continue;
        ModulationDestination mapped{};
        if (!mapSourceDestination(
                plan,
                binding.destination,
                mapped
            )) {
            return false;
        }
        hasMappedOutput = true;
    }
    if (!hasMappedOutput) return false;

    const auto* trigger = findProjectModulationTriggerForSource(
        source.modulation,
        sourceId
    );
    if (trigger == nullptr ||
        trigger->trigger.kind !=
            ModulationTriggerKind::TRACK_NOTE) {
        return true;
    }
    uint8_t mappedTrack = trigger->trigger.track;
    return mapSourceTriggerTrack(
        plan,
        trigger->trigger.track,
        mappedTrack
    );
}

FLASHMEM bool cloneSource(
    ProjectControlDomainState& target,
    const ProjectControlDomainState& source,
    const ModulatorSourceState& original,
    ModulatorId& cloned
) {
    ProjectModulationResult result{};
    const bool enabled =
        (original.flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
    switch (original.kind) {
        case ModulatorKind::LFO:
            result = createLfoModulator(
                target.modulation,
                ModulatorLfoDraft{
                    .name = original.name.data(),
                    .parameters = original.parameters.lfo,
                    .accent = original.accent,
                    .enabled = enabled,
                }
            );
            break;
        case ModulatorKind::ADSR:
            result = createAdsrModulator(
                target.modulation,
                ModulatorAdsrDraft{
                    .name = original.name.data(),
                    .parameters = original.parameters.adsr,
                    .accent = original.accent,
                    .enabled = enabled,
                }
            );
            break;
        case ModulatorKind::RECORDED_SHAPE: {
            const auto* record = findProjectCurve(
                source.curves,
                original.parameters.recordedCurveId
            );
            if (record == nullptr || record->pointCount == 0U ||
                static_cast<uint32_t>(record->pointOffset) +
                        record->pointCount >
                    source.curves.pointCount) {
                return false;
            }
            result = createRecordedShapeModulator(
                target.modulation,
                target.curves,
                RecordedShapeDraft{
                    .name = original.name.data(),
                    .curve = curveSpec(*record),
                    .points =
                        source.curves.points.data() +
                        record->pointOffset,
                    .pointCount = record->pointCount,
                    .accent = original.accent,
                    .enabled = enabled,
                }
            );
            break;
        }
        default:
            return false;
    }
    if (!result.changed()) return false;
    cloned = result.sourceId;
    return true;
}

FLASHMEM bool copyLocalTrigger(
    ProjectControlDomainState& target,
    const ProjectControlDomainState& source,
    const ProjectControlStructureTransferPlan& plan,
    ModulatorId sourceId,
    ModulatorId targetId
) {
    const auto* trigger = findProjectModulationTriggerForSource(
        source.modulation,
        sourceId
    );
    if (trigger == nullptr) return true;
    auto filter = trigger->trigger;
    if (filter.kind == ModulationTriggerKind::TRACK_NOTE &&
        !mapSourceTriggerTrack(
            plan,
            filter.track,
            filter.track
        )) {
        return false;
    }
    const auto result = addProjectModulationTrigger(
        target.modulation,
        ModulationTriggerDraft{
            .sourceId = targetId,
            .trigger = filter,
            .velocityMin = trigger->velocityMin,
            .velocityMax = trigger->velocityMax,
            .enabled =
                (trigger->flags &
                 PROJECT_MODULATION_TRIGGER_FLAG_ENABLED) != 0U,
        }
    );
    return result.changed();
}

FLASHMEM bool resolveSource(
    ProjectControlDomainState& target,
    const ProjectControlDomainState& source,
    const ProjectControlStructureTransferPlan& plan,
    ModulatorId sourceId,
    SourceRemapTable& remaps,
    ModulatorId& resolved
) {
    const auto* original = findProjectModulator(
        source.modulation,
        sourceId
    );
    if (original == nullptr) return false;
    const auto sourceIndex = static_cast<uint16_t>(
        original - source.modulation.sources.data()
    );
    if (sourceIndex >= source.modulation.sourceCount ||
        sourceIndex >= remaps.size()) {
        return false;
    }
    if (valid(remaps[sourceIndex])) {
        resolved = remaps[sourceIndex];
        return true;
    }

    const bool local =
        sourceIsLocalToTransfer(source, plan, sourceId);
    ModulatorId targetId = sourceId;
    if (local) {
        if (!cloneSource(target, source, *original, targetId) ||
            !copyLocalTrigger(
                target,
                source,
                plan,
                sourceId,
                targetId
            )) {
            return false;
        }
    } else if (findProjectModulator(
                   target.modulation,
                   sourceId
               ) == nullptr) {
        // A shared source is a stable Project link, not an implicit clone. If
        // it disappeared after Copy, the clipboard is stale.
        return false;
    }
    remaps[sourceIndex] = targetId;
    resolved = targetId;
    return true;
}

FLASHMEM bool copyModulation(
    ProjectControlDomainState& target,
    const ProjectControlDomainState& source,
    const ProjectControlStructureTransferPlan& plan
) {
    auto remaps = core::app::makeExtmemUnique<SourceRemapTable>();
    if (!remaps) return false;
    for (uint16_t index = 0U;
         index < source.modulation.outputBindingCount;
         ++index) {
        const auto& binding =
            source.modulation.outputBindings[index];
        ModulationDestination destination{};
        if (!mapSourceDestination(
                plan,
                binding.destination,
                destination
            )) {
            continue;
        }
        ModulatorId sourceId{};
        if (!resolveSource(
                target,
                source,
                plan,
                binding.sourceId,
                *remaps,
                sourceId
            )) {
            return false;
        }
        const auto result = addProjectModulationBinding(
            target.modulation,
            ModulationBindingDraft{
                .sourceId = sourceId,
                .destination = destination,
                .amountQ15 = binding.amountQ15,
                .application = binding.application,
                .transfer = binding.transfer,
                .slewMs = binding.slewMs,
                .enabled =
                    (binding.flags &
                     PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U,
            }
        );
        if (!result.changed()) return false;
        const uint16_t scale =
            projectModulationDestinationScaleQ15(
                source.modulation,
                binding.destination
            );
        if (scale !=
            PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15) {
            const auto scaleResult =
                setProjectModulationDestinationScale(
                    target.modulation,
                    destination,
                    scale
                );
            if (!scaleResult.changed() &&
                scaleResult.status !=
                    ProjectModulationStatus::NO_CHANGE) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

FLASHMEM bool ProjectControlStructureTransferPlan::valid() const {
    if (count == 0U || count > entries.size()) return false;
    uint16_t targetTrackMask = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        const auto& entry = entries[index];
        if (entry.sourceTrack >= PROJECT_MODULATION_TRACK_COUNT ||
            entry.targetTrack >= PROJECT_MODULATION_TRACK_COUNT ||
            (!entry.wholeTrack &&
             (entry.sourcePage >= PROJECT_MODULATION_PAGE_COUNT ||
              entry.targetPage >= PROJECT_MODULATION_PAGE_COUNT))) {
            return false;
        }
        if (entry.wholeTrack) {
            const uint16_t bit = static_cast<uint16_t>(
                1U << entry.targetTrack
            );
            if ((targetTrackMask & bit) != 0U) return false;
            targetTrackMask = static_cast<uint16_t>(
                targetTrackMask | bit
            );
        }
        for (uint8_t previous = 0U;
             previous < index;
             ++previous) {
            const auto& other = entries[previous];
            if (entry.sourceTrack == other.sourceTrack &&
                entry.wholeTrack == other.wholeTrack &&
                (entry.wholeTrack ||
                 entry.sourcePage == other.sourcePage)) {
                return false;
            }
            if (entry.targetTrack == other.targetTrack &&
                (entry.wholeTrack || other.wholeTrack ||
                 entry.targetPage == other.targetPage)) {
                return false;
            }
        }
    }
    return true;
}

FLASHMEM bool replaceProjectControlStructureInDomain(
    ProjectControlDomainState& target,
    const ProjectControlDomainState& source,
    const ProjectControlStructureTransferPlan& plan
) {
    if (!plan.valid() ||
        !validProjectModulationDomain(
            source.modulation,
            source.curves,
            &source.automation
        ) ||
        !validProjectModulationDomain(
            target.modulation,
            target.curves,
            &target.automation
        )) {
        return false;
    }
    return clearMappedDestinations(target, plan) &&
           copyAutomation(target, source, plan) &&
           copyModulation(target, source, plan) &&
           validProjectModulationDomain(
               target.modulation,
               target.curves,
               &target.automation
           );
}

}  // namespace core::state::modulation

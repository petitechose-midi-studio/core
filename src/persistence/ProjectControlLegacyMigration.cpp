#include "persistence/ProjectControlLegacyMigration.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/LegacyMacroAutomationPersistenceCodec.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::persistence::project_control_migration {

namespace {

namespace legacy_codec =
    core::persistence::macro_automation_legacy_codec;
namespace macro = core::state::macro;
namespace modulation = core::state::modulation;

FLASHMEM modulation::ProjectCurveOrigin mapOrigin(
    macro::MacroModulationOrigin origin
) {
    switch (origin) {
        case macro::MacroModulationOrigin::CONVERTED_MEAN:
            return modulation::ProjectCurveOrigin::CONVERTED_MEAN;
        case macro::MacroModulationOrigin::CONVERTED_FIRST:
            return modulation::ProjectCurveOrigin::CONVERTED_FIRST;
        case macro::MacroModulationOrigin::CONVERTED_MIN:
            return modulation::ProjectCurveOrigin::CONVERTED_MIN;
        case macro::MacroModulationOrigin::NATIVE:
        default:
            return modulation::ProjectCurveOrigin::NATIVE;
    }
}

FLASHMEM modulation::ModulationDestination destinationFor(
    const macro::MacroAutomationSlotAddress& address
) {
    return {
        modulation::ModulationDestinationKind::MACRO_SLOT,
        address.track,
        address.page,
        address.macro,
    };
}

FLASHMEM uint16_t stableAddress(
    const macro::MacroAutomationSlotAddress& address
) {
    return modulation::modulationDestinationStableAddress(
        destinationFor(address)
    );
}

FLASHMEM void writeTwoDigits(
    std::array<char, modulation::PROJECT_MODULATOR_NAME_CAPACITY>& name,
    uint8_t offset,
    uint8_t value
) {
    name[offset] = static_cast<char>('0' + (value / 10U));
    name[static_cast<uint8_t>(offset + 1U)] =
        static_cast<char>('0' + (value % 10U));
}

FLASHMEM void writeLegacySourceName(
    std::array<char, modulation::PROJECT_MODULATOR_NAME_CAPACITY>& name,
    const macro::MacroAutomationSlotAddress& address
) {
    name.fill('\0');
    name[0] = 'T';
    writeTwoDigits(name, 1U, static_cast<uint8_t>(address.track + 1U));
    name[3] = ' ';
    name[4] = 'P';
    writeTwoDigits(name, 5U, static_cast<uint8_t>(address.page + 1U));
    name[7] = ' ';
    name[8] = 'M';
    name[9] = static_cast<char>('1' + address.macro);
}

FLASHMEM int16_t canonicalPointValue(int16_t value, bool absolute) {
    if (absolute) return std::max<int16_t>(0, value);
    return value == INT16_MIN ? static_cast<int16_t>(-32767) : value;
}

FLASHMEM modulation::ProjectCurveId appendCurve(
    modulation::ProjectControlDomainState& target,
    const macro::MacroAutomationBankState& legacy,
    const macro::MacroAutomationCurveRef& source,
    modulation::ProjectCurveValueDomain valueDomain,
    modulation::ProjectCurveOrigin origin
) {
    const modulation::ProjectCurveId id{target.curves.nextCurveId++};
    auto& record = target.curves.records[target.curves.recordCount++];
    record = {};
    record.id = id;
    record.pointOffset = target.curves.pointCount;
    record.pointCount = source.pointCount;
    record.sourceDurationTicks = source.sourceDurationTicks;
    record.durationTicks = source.durationTicks;
    record.windowOffsetTicks = source.windowOffsetTicks;
    record.referenceCount = 1U;
    record.interpolation = modulation::ProjectCurveInterpolation::LINEAR;
    record.valueDomain = valueDomain;
    record.origin = origin;

    const bool absolute = valueDomain ==
        modulation::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
    for (uint16_t index = 0; index < source.pointCount; ++index) {
        const auto& oldPoint = legacy.pointPool.points[
            static_cast<uint16_t>(source.pointOffset + index)
        ];
        target.curves.points[target.curves.pointCount++] = {
            oldPoint.tick,
            canonicalPointValue(oldPoint.value, absolute),
        };
    }
    return id;
}

FLASHMEM int16_t depthToQ15(float depth) {
    const long rounded = std::lround(
        std::clamp(depth, 0.0f, 1.0f) * 32767.0f
    );
    return static_cast<int16_t>(std::clamp<long>(rounded, 0L, 32767L));
}

FLASHMEM const macro::MacroAutomationSlotEntry* entryAtStableAddress(
    const macro::MacroAutomationBankState& legacy,
    uint16_t address
) {
    for (uint8_t index = 0; index < legacy.entryCount; ++index) {
        if (stableAddress(legacy.entries[index].address) == address) {
            return &legacy.entries[index];
        }
    }
    return nullptr;
}

}  // namespace

FLASHMEM Result liftLegacyMacroAutomationBankIntoPending(
    const macro::MacroAutomationBankState& legacy,
    modulation::ProjectControlDomainState& pending
) {
    if (!legacy_codec::validBank(legacy)) {
        return {.status = Status::INVALID_LEGACY_BANK};
    }
    if (legacy.entryCount > modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY ||
        legacy.entryCount > modulation::PROJECT_MODULATOR_CAPACITY ||
        static_cast<uint16_t>(legacy.entryCount) * 2U >
            modulation::PROJECT_CURVE_LIVE_CAPACITY ||
        legacy.pointPool.used > modulation::PROJECT_CURVE_POINT_CAPACITY) {
        return {.status = Status::CAPACITY_EXCEEDED};
    }

    pending.clear();

    constexpr uint16_t ADDRESS_COUNT =
        modulation::PROJECT_MODULATION_TRACK_COUNT *
        modulation::PROJECT_MODULATION_PAGE_COUNT *
        modulation::PROJECT_MODULATION_MACRO_COUNT;
    for (uint16_t address = 0; address < ADDRESS_COUNT; ++address) {
        const auto* entry = entryAtStableAddress(legacy, address);
        if (entry == nullptr) continue;
        const auto destination = destinationFor(entry->address);

        if (macro::macroCurveStored(entry->state.automation)) {
            const auto curveId = appendCurve(
                pending,
                legacy,
                entry->state.automation,
                modulation::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR,
                modulation::ProjectCurveOrigin::NATIVE
            );
            auto& automation = pending.automation.entries[
                pending.automation.entryCount++
            ];
            automation = {};
            automation.destination = destination;
            automation.curveId = curveId;
            automation.flags = macro::macroCurvePlaybackActive(
                entry->state.automation
            ) ? modulation::PROJECT_AUTOMATION_CURVE_FLAG_ENABLED : 0U;
        }

        if (macro::macroCurveStored(entry->state.modulation)) {
            const auto curveId = appendCurve(
                pending,
                legacy,
                entry->state.modulation,
                modulation::ProjectCurveValueDomain::BIPOLAR,
                mapOrigin(entry->state.modulation.modulationOrigin)
            );

            auto& source = pending.modulation.sources[
                pending.modulation.sourceCount++
            ];
            source = {};
            source.id = {pending.modulation.nextSourceId++};
            writeLegacySourceName(source.name, entry->address);
            source.reach.kind = modulation::ModulatorReachKind::MACRO;
            source.reach.track = entry->address.track;
            source.reach.page = entry->address.page;
            source.reach.macro = entry->address.macro;
            source.kind = modulation::ModulatorKind::RECORDED_SHAPE;
            source.flags = macro::macroCurvePlaybackActive(
                entry->state.modulation
            ) ? modulation::PROJECT_MODULATOR_FLAG_ENABLED : 0U;
            source.schemaVersion = 1U;
            source.parameters.recordedCurveId = curveId;

            auto& binding = pending.modulation.outputBindings[
                pending.modulation.outputBindingCount++
            ];
            binding = {};
            binding.id = {pending.modulation.nextBindingId++};
            binding.sourceId = source.id;
            binding.destination = destination;
            binding.amountQ15 = depthToQ15(entry->state.modulationDepth);
            binding.application = modulation::ModulationApplication::AROUND_BASE;
            binding.transfer = modulation::ModulationTransfer::LINEAR;
            binding.flags = modulation::PROJECT_MODULATION_BINDING_FLAG_ENABLED;
        }
    }

    if (pending.curves.pointCount != legacy.pointPool.used ||
        !modulation::validProjectModulationDomain(
            pending.modulation,
            pending.curves,
            &pending.automation
        )) {
        return {.status = Status::INVALID_MIGRATED_DOMAIN};
    }

    Result result{
        .status = Status::OK,
        .automationCount = pending.automation.entryCount,
        .sourceCount = pending.modulation.sourceCount,
        .bindingCount = pending.modulation.outputBindingCount,
        .pointCount = pending.curves.pointCount,
    };
    return result;
}

FLASHMEM Result liftLegacyMacroAutomationBank(
    const macro::MacroAutomationBankState& legacy,
    modulation::ProjectControlDomainState& out
) {
    auto pending =
        core::app::makeExtmemUnique<modulation::ProjectControlDomainState>();
    if (!pending) return {.status = Status::SCRATCH_ALLOCATION_FAILED};
    const Result result = liftLegacyMacroAutomationBankIntoPending(
        legacy,
        *pending
    );
    if (result.migrated()) out = *pending;
    return result;
}

}  // namespace core::persistence::project_control_migration

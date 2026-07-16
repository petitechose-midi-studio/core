#include "state/modulation/ProjectControlMacroOps.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::modulation {

namespace {

using PackedScratch = std::array<
    ProjectPackedCurvePoint,
    macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS
>;

static_assert(sizeof(ProjectPackedCurvePoint) == sizeof(macro::MacroPackedCurvePoint));
static_assert(alignof(ProjectPackedCurvePoint) == alignof(macro::MacroPackedCurvePoint));

bool validAddress(const macro::MacroAutomationSlotAddress& address) {
    return macro::macroAutomationAddressValid(address);
}

macro::MacroCurvePlaybackState playbackState(bool enabled) {
    return enabled
        ? macro::MacroCurvePlaybackState::ACTIVE
        : macro::MacroCurvePlaybackState::OFF;
}

macro::MacroModulationOrigin macroOrigin(ProjectCurveOrigin origin) {
    switch (origin) {
        case ProjectCurveOrigin::CONVERTED_MEAN:
            return macro::MacroModulationOrigin::CONVERTED_MEAN;
        case ProjectCurveOrigin::CONVERTED_FIRST:
            return macro::MacroModulationOrigin::CONVERTED_FIRST;
        case ProjectCurveOrigin::CONVERTED_MIN:
            return macro::MacroModulationOrigin::CONVERTED_MIN;
        case ProjectCurveOrigin::NATIVE:
        default:
            return macro::MacroModulationOrigin::NATIVE;
    }
}

ProjectCurveOrigin projectOrigin(macro::MacroModulationOrigin origin) {
    switch (origin) {
        case macro::MacroModulationOrigin::CONVERTED_MEAN:
            return ProjectCurveOrigin::CONVERTED_MEAN;
        case macro::MacroModulationOrigin::CONVERTED_FIRST:
            return ProjectCurveOrigin::CONVERTED_FIRST;
        case macro::MacroModulationOrigin::CONVERTED_MIN:
            return ProjectCurveOrigin::CONVERTED_MIN;
        case macro::MacroModulationOrigin::NATIVE:
        default:
            return ProjectCurveOrigin::NATIVE;
    }
}

void projectLegacyCurve(
    const ProjectCurveRecord& record,
    bool enabled,
    macro::MacroAutomationCurveRef& out
) {
    out = {};
    out.active = true;
    out.playbackState = playbackState(enabled);
    out.pointOffset = record.pointOffset;
    out.pointCount = record.pointCount;
    out.sourceDurationTicks = record.sourceDurationTicks;
    out.durationTicks = record.durationTicks;
    out.windowOffsetTicks = record.windowOffsetTicks;
    out.interpolation = macro::MacroAutomationInterpolation::LINEAR;
    out.modulationOrigin = macroOrigin(record.origin);
}

const ModulationBindingState* firstBindingForDestination(
    const ProjectModulationState& state,
    const ModulationDestination& destination,
    uint16_t& count
) {
    const ModulationBindingState* first = nullptr;
    count = 0;
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        const auto& binding = state.outputBindings[index];
        if (binding.destination != destination) continue;
        ++count;
        if (first == nullptr || binding.id.value < first->id.value) {
            first = &binding;
        }
    }
    return first;
}

ModulationBindingState* bindingById(
    ProjectModulationState& state,
    ModulationBindingId id
) {
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        if (state.outputBindings[index].id == id) {
            return &state.outputBindings[index];
        }
    }
    return nullptr;
}

FLASHMEM ProjectModulationFocusEntry* focusEntryFor(
    ProjectModulationFocusState& focus,
    const ModulationDestination& destination
) {
    for (auto& entry : focus.entries) {
        if (entry.active && entry.destination == destination) return &entry;
    }
    return nullptr;
}

FLASHMEM uint16_t nextFocusStamp(ProjectModulationFocusState& focus) {
    ++focus.clock;
    if (focus.clock == 0U) {
        for (auto& entry : focus.entries) entry.stamp = 0;
        focus.clock = 1U;
    }
    return focus.clock;
}

FLASHMEM ProjectModulationFocusEntry& allocateFocusEntry(
    ProjectModulationFocusState& focus,
    const ModulationDestination& destination
) {
    if (auto* current = focusEntryFor(focus, destination)) return *current;
    ProjectModulationFocusEntry* selected = &focus.entries[0];
    for (auto& entry : focus.entries) {
        if (!entry.active) {
            selected = &entry;
            break;
        }
        if (entry.stamp < selected->stamp) selected = &entry;
    }
    *selected = {};
    selected->destination = destination;
    selected->active = true;
    return *selected;
}

bool sourceHasOtherEdges(
    const ProjectModulationState& state,
    ModulatorId sourceId,
    ModulationBindingId excluded
) {
    for (uint16_t index = 0; index < state.outputBindingCount; ++index) {
        const auto& binding = state.outputBindings[index];
        if (binding.sourceId == sourceId && binding.id != excluded) return true;
    }
    for (uint16_t index = 0; index < state.triggerBindingCount; ++index) {
        if (state.triggerBindings[index].sourceId == sourceId) return true;
    }
    return false;
}

FLASHMEM bool removePrimaryModulation(
    ProjectControlDomainState& domain,
    const ProjectControlMacroSlotView& view
) {
    if (!view.modulationStored || view.legacyMutationAmbiguous) return false;
    const bool deleteSource = !sourceHasOtherEdges(
        domain.modulation,
        view.modulationSourceId,
        view.modulationBindingId
    );
    const auto removed = removeProjectModulationBinding(
        domain.modulation,
        view.modulationBindingId
    );
    if (!removed.changed()) return false;
    if (deleteSource) {
        const auto deleted = deleteProjectModulator(
            domain.modulation,
            domain.curves,
            view.modulationSourceId
        );
        if (!deleted.changed()) return false;
    }
    return true;
}

ProjectCurveSpec curveSpec(
    const macro::MacroAutomationCurveRef& source,
    ProjectCurveValueDomain valueDomain
) {
    return {
        .sourceDurationTicks = std::max<uint16_t>(source.sourceDurationTicks, 1U),
        .durationTicks = std::max<uint16_t>(source.durationTicks, 1U),
        .windowOffsetTicks = source.windowOffsetTicks,
        .interpolation = ProjectCurveInterpolation::LINEAR,
        .valueDomain = valueDomain,
        .origin = valueDomain == ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR
            ? ProjectCurveOrigin::NATIVE
            : projectOrigin(source.modulationOrigin),
    };
}

FLASHMEM bool appendRecordedShape(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationCurveRef& curve,
    float amount,
    const ProjectPackedCurvePoint* points
) {
    if (!macro::macroCurveStored(curve) || points == nullptr) return true;
    const ModulationDestination destination = projectControlDestination(address);
    RecordedShapeDraft source{};
    source.name = "Recorded Shape";
    source.reach = {
        .trackMask = 0,
        .kind = ModulatorReachKind::MACRO,
        .track = address.track,
        .page = address.page,
        .macro = address.macro,
    };
    source.curve = curveSpec(curve, ProjectCurveValueDomain::BIPOLAR);
    source.points = points;
    source.pointCount = curve.pointCount;
    source.enabled = macro::macroCurvePlaybackActive(curve);
    const auto created = createRecordedShapeModulator(
        domain.modulation,
        domain.curves,
        source
    );
    if (!created.changed()) return false;

    const long packedAmount = std::lround(
        std::clamp(amount, -1.0f, 1.0f) * 32767.0f
    );
    ModulationBindingDraft binding{};
    binding.sourceId = created.sourceId;
    binding.destination = destination;
    binding.amountQ15 = static_cast<int16_t>(
        std::clamp<long>(packedAmount, -32767L, 32767L)
    );
    binding.inputRange = ModulationInputRange::BIPOLAR;
    binding.enabled = true;
    const auto bound = addProjectModulationBinding(domain.modulation, binding);
    if (bound.changed()) return true;
    (void)deleteProjectModulator(domain.modulation, domain.curves, created.sourceId);
    return false;
}

bool readDomainMacroSlot(
    const ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    ProjectControlMacroSlotView& out
) {
    out = {};
    out.address = address;
    if (!validAddress(address)) return false;
    const auto destination = projectControlDestination(address);
    const auto* automation = findProjectAutomationCurve(
        domain.automation,
        destination
    );
    if (automation != nullptr) {
        const auto* curve = findProjectCurve(domain.curves, automation->curveId);
        if (curve == nullptr) return false;
        out.automationCurveId = curve->id;
        out.automationStored = true;
        out.automationEnabled =
            (automation->flags & PROJECT_AUTOMATION_CURVE_FLAG_ENABLED) != 0U;
        projectLegacyCurve(*curve, out.automationEnabled, out.legacy.automation);
    }

    uint16_t bindingCount = 0;
    const auto* binding = firstBindingForDestination(
        domain.modulation,
        destination,
        bindingCount
    );
    out.modulationCount = bindingCount;
    out.modulationStored = bindingCount > 0U;
    out.legacyMutationAmbiguous = bindingCount > 1U;
    for (uint16_t index = 0;
         index < domain.modulation.outputBindingCount;
         ++index) {
        const auto& candidate = domain.modulation.outputBindings[index];
        if (candidate.destination != destination ||
            (candidate.flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) == 0U) {
            continue;
        }
        const auto* candidateSource = findProjectModulator(
            domain.modulation,
            candidate.sourceId
        );
        if (candidateSource != nullptr &&
            (candidateSource->flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U) {
            ++out.activeModulationCount;
        }
    }
    if (binding != nullptr) {
        const auto* source = findProjectModulator(
            domain.modulation,
            binding->sourceId
        );
        if (source == nullptr) return false;
        out.modulationSourceId = source->id;
        out.modulationBindingId = binding->id;
        out.modulationEnabled =
            (binding->flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U &&
            (source->flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
        out.legacy.modulationDepth = std::clamp(
            static_cast<float>(binding->amountQ15) / 32767.0f,
            -1.0f,
            1.0f
        );
        if (source->kind == ModulatorKind::RECORDED_SHAPE) {
            const auto* curve = findProjectCurve(
                domain.curves,
                source->parameters.recordedCurveId
            );
            if (curve == nullptr) return false;
            out.primaryRecordedShape = true;
            out.modulationCurveId = curve->id;
            projectLegacyCurve(*curve, out.modulationEnabled, out.legacy.modulation);
        }
    }
    out.present = out.automationStored || out.modulationStored;
    return true;
}

FLASHMEM bool replaceSlotInDomain(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationSlotState& sourceState,
    const macro::MacroPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
) {
    ProjectControlMacroSlotView current{};
    if (!readDomainMacroSlot(domain, address, current) ||
        current.legacyMutationAmbiguous) {
        return false;
    }
    if (current.automationStored &&
        !removeProjectAutomationCurve(
            domain.automation,
            domain.curves,
            projectControlDestination(address)
        ).changed()) {
        return false;
    }
    if (current.modulationStored && !removePrimaryModulation(domain, current)) {
        return false;
    }

    const uint32_t required = static_cast<uint32_t>(
        sourceState.automation.pointCount
    ) + sourceState.modulation.pointCount;
    if (required > sourcePointCount ||
        (required > 0U && sourcePoints == nullptr)) {
        return false;
    }
    if (macro::macroCurveStored(sourceState.automation)) {
        const auto set = setProjectAutomationCurve(
            domain.automation,
            domain.curves,
            projectControlDestination(address),
            curveSpec(
                sourceState.automation,
                ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR
            ),
            reinterpret_cast<const ProjectPackedCurvePoint*>(sourcePoints),
            sourceState.automation.pointCount,
            macro::macroCurvePlaybackActive(sourceState.automation)
        );
        if (!set.changed()) return false;
    }
    if (macro::macroCurveStored(sourceState.modulation)) {
        const auto* modulationPoints = reinterpret_cast<
            const ProjectPackedCurvePoint*
        >(sourcePoints + sourceState.automation.pointCount);
        if (!appendRecordedShape(
                domain,
                address,
                sourceState.modulation,
                sourceState.modulationDepth,
                modulationPoints
            )) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool buildLegacyConversionBank(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    macro::MacroAutomationBankState& out
) {
    out.clear();
    macro::MacroAutomationSlotState slot{};
    uint16_t automationCount = 0;
    uint16_t modulationCount = 0;
    if (!captureProjectControlMacroSlot(
            control,
            address,
            slot,
            out.pointPool.points.data(),
            macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY,
            automationCount,
            modulationCount
        )) {
        return false;
    }
    const uint16_t total = static_cast<uint16_t>(
        automationCount + modulationCount
    );
    out.pointPool.used = total;
    if (!macro::macroAutomationSlotHasContent(slot)) return true;
    out.entryCount = 1;
    out.entries[0] = {
        .active = true,
        .address = address,
        .state = slot,
    };
    return true;
}

}  // namespace

FLASHMEM ModulationDestination projectControlDestination(
    const macro::MacroAutomationSlotAddress& address
) {
    return {
        ModulationDestinationKind::MACRO_SLOT,
        address.track,
        address.page,
        address.macro,
    };
}

FLASHMEM bool readProjectControlMacroSlot(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    ProjectControlMacroSlotView& out
) {
    return readDomainMacroSlot(control.authored, address, out);
}

FLASHMEM ModulationBindingId projectControlFocusedModulationBinding(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
) {
    if (!validAddress(address)) return {};
    const auto destination = projectControlDestination(address);
    auto& graph = control.authored.modulation;
    auto* entry = focusEntryFor(control.focus, destination);
    if (entry != nullptr) {
        const auto* binding = bindingById(graph, entry->bindingId);
        if (binding != nullptr && binding->destination == destination) {
            entry->stamp = nextFocusStamp(control.focus);
            return entry->bindingId;
        }
        *entry = {};
    }

    uint16_t count = 0;
    const auto* first = firstBindingForDestination(graph, destination, count);
    if (first == nullptr) return {};
    auto& next = allocateFocusEntry(control.focus, destination);
    next.bindingId = first->id;
    next.stamp = nextFocusStamp(control.focus);
    return next.bindingId;
}

FLASHMEM bool setProjectControlFocusedModulationBinding(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    ModulationBindingId bindingId
) {
    if (!validAddress(address) || !valid(bindingId)) return false;
    const auto destination = projectControlDestination(address);
    const auto* binding = bindingById(control.authored.modulation, bindingId);
    if (binding == nullptr || binding->destination != destination) return false;
    auto& entry = allocateFocusEntry(control.focus, destination);
    const bool changed = entry.bindingId != bindingId;
    entry.bindingId = bindingId;
    entry.stamp = nextFocusStamp(control.focus);
    return changed;
}

FLASHMEM bool replaceProjectControlMacroSlotInDomain(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationSlotState& sourceState,
    const macro::MacroPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
) {
    return validAddress(address) && replaceSlotInDomain(
        domain,
        address,
        sourceState,
        sourcePoints,
        sourcePointCount
    );
}

FLASHMEM bool setProjectControlAutomationEnabled(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    bool enabled
) {
    if (!validAddress(address)) return false;
    const auto result = setProjectAutomationEnabled(
        control.authored.automation,
        projectControlDestination(address),
        enabled
    );
    if (!result.changed()) return false;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool setProjectControlModulationEnabled(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    bool enabled
) {
    ProjectControlMacroSlotView view{};
    if (!readProjectControlMacroSlot(control, address, view) ||
        !view.modulationStored || view.legacyMutationAmbiguous) {
        return false;
    }
    auto* binding = bindingById(
        control.authored.modulation,
        view.modulationBindingId
    );
    auto* source = findProjectModulator(
        control.authored.modulation,
        view.modulationSourceId
    );
    if (binding == nullptr || source == nullptr) return false;
    const bool localSingle =
        source->reach.kind == ModulatorReachKind::MACRO &&
        !sourceHasOtherEdges(
            control.authored.modulation,
            source->id,
            binding->id
        );
    bool changed = false;
    const uint8_t bindingFlags = enabled
        ? PROJECT_MODULATION_BINDING_FLAG_ENABLED
        : 0U;
    if (binding->flags != bindingFlags) {
        binding->flags = bindingFlags;
        changed = true;
    }
    if (localSingle) {
        const uint8_t sourceFlags = enabled ? PROJECT_MODULATOR_FLAG_ENABLED : 0U;
        if (source->flags != sourceFlags) {
            source->flags = sourceFlags;
            changed = true;
        }
    }
    if (!changed) return false;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool setProjectControlModulationAmount(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    float amount
) {
    ProjectControlMacroSlotView view{};
    if (!std::isfinite(amount) ||
        !readProjectControlMacroSlot(control, address, view) ||
        !view.modulationStored || view.legacyMutationAmbiguous) {
        return false;
    }
    auto* binding = bindingById(control.authored.modulation, view.modulationBindingId);
    if (binding == nullptr) return false;
    const long rounded = std::lround(std::clamp(amount, -1.0f, 1.0f) * 32767.0f);
    const int16_t packed = static_cast<int16_t>(
        std::clamp<long>(rounded, -32767L, 32767L)
    );
    if (binding->amountQ15 == packed) return false;
    binding->amountQ15 = packed;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool clearProjectControlAutomation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
) {
    if (!validAddress(address)) return false;
    const auto removed = removeProjectAutomationCurve(
        control.authored.automation,
        control.authored.curves,
        projectControlDestination(address)
    );
    if (!removed.changed()) return false;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool clearProjectControlModulation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
) {
    ProjectControlMacroSlotView view{};
    if (!readProjectControlMacroSlot(control, address, view) ||
        !removePrimaryModulation(control.authored, view)) {
        return false;
    }
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool assignProjectControlAutomation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationLane& lane
) {
    if (!validAddress(address) || !lane.active || lane.pointCount == 0U ||
        lane.pointCount > macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS) {
        return false;
    }
    auto scratch = core::app::makeExtmemUnique<PackedScratch>();
    if (!scratch) return false;
    const uint16_t durationTicks = macro::macroAutomationTicksFromBeats(
        lane.durationBeats
    );
    uint16_t written = 0;
    for (uint16_t index = 0; index < lane.pointCount; ++index) {
        const float beat = std::isfinite(lane.points[index].beat)
            ? std::max(lane.points[index].beat, 0.0f)
            : 0.0f;
        const uint16_t tick = static_cast<uint16_t>(std::clamp<long>(
            std::lround(
                beat * static_cast<float>(macro::MACRO_AUTOMATION_TICKS_PER_BEAT)
            ),
            0L,
            durationTicks
        ));
        const int16_t value = macro::macroAutomationPackValue(
            lane.points[index].value,
            false
        );
        if (written > 0U && (*scratch)[written - 1U].tick == tick) {
            (*scratch)[written - 1U].value = value;
            continue;
        }
        (*scratch)[written++] = {tick, value};
    }
    if (written == 0U) return false;
    ProjectCurveSpec spec{};
    spec.sourceDurationTicks = std::max<uint16_t>(durationTicks, 1U);
    spec.durationTicks = std::max<uint16_t>(durationTicks, 1U);
    spec.valueDomain = ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
    spec.origin = ProjectCurveOrigin::NATIVE;
    const auto result = setProjectAutomationCurve(
        control.authored.automation,
        control.authored.curves,
        projectControlDestination(address),
        spec,
        scratch->data(),
        written,
        true
    );
    if (result.status == ProjectModulationStatus::NO_CHANGE) return true;
    if (!result.changed()) {
        return false;
    }
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool captureProjectControlMacroSlot(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    macro::MacroAutomationSlotState& outState,
    macro::MacroPackedCurvePoint* outPoints,
    uint16_t pointCapacity,
    uint16_t& automationPointCount,
    uint16_t& modulationPointCount
) {
    outState = {};
    automationPointCount = 0;
    modulationPointCount = 0;
    ProjectControlMacroSlotView view{};
    if (!readProjectControlMacroSlot(control, address, view) ||
        view.legacyMutationAmbiguous ||
        (view.modulationStored && !view.primaryRecordedShape)) {
        return false;
    }
    const uint32_t required = static_cast<uint32_t>(
        view.legacy.automation.pointCount
    ) + view.legacy.modulation.pointCount;
    if (required > pointCapacity || (required > 0U && outPoints == nullptr)) {
        return false;
    }
    outState = view.legacy;
    automationPointCount = view.legacy.automation.pointCount;
    modulationPointCount = view.legacy.modulation.pointCount;
    if (automationPointCount > 0U) {
        for (uint16_t index = 0; index < automationPointCount; ++index) {
            const auto& point = control.authored.curves.points[
                view.legacy.automation.pointOffset + index
            ];
            outPoints[index] = {
                .tick = point.tick,
                .value = point.value,
            };
        }
        outState.automation.pointOffset = 0;
    }
    if (modulationPointCount > 0U) {
        for (uint16_t index = 0; index < modulationPointCount; ++index) {
            const auto& point = control.authored.curves.points[
                view.legacy.modulation.pointOffset + index
            ];
            outPoints[automationPointCount + index] = {
                .tick = point.tick,
                .value = point.value,
            };
        }
        outState.modulation.pointOffset = automationPointCount;
    }
    return true;
}

FLASHMEM bool replaceProjectControlMacroSlot(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationSlotState& sourceState,
    const macro::MacroPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
) {
    if (!validAddress(address)) return false;
    auto pending = core::app::makeExtmemUnique<ProjectControlDomainState>();
    if (!pending) return false;
    *pending = control.authored;
    if (!replaceSlotInDomain(
            *pending,
            address,
            sourceState,
            sourcePoints,
            sourcePointCount
        ) || !validProjectModulationDomain(
            pending->modulation,
            pending->curves,
            &pending->automation
        )) {
        return false;
    }
    control.authored = *pending;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool replaceProjectControlAutomation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationCurveRef& source,
    const macro::MacroPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
) {
    if (!validAddress(address) ||
        !macro::macroAutomationCurveLifecycleValid(source)) {
        return false;
    }
    if (!macro::macroCurveStored(source)) {
        ProjectControlMacroSlotView current{};
        if (!readProjectControlMacroSlot(control, address, current)) return false;
        return !current.automationStored ||
               clearProjectControlAutomation(control, address);
    }
    if (source.pointCount == 0U || source.pointCount > sourcePointCount ||
        sourcePoints == nullptr) {
        return false;
    }
    const auto result = setProjectAutomationCurve(
        control.authored.automation,
        control.authored.curves,
        projectControlDestination(address),
        curveSpec(source, ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR),
        reinterpret_cast<const ProjectPackedCurvePoint*>(sourcePoints),
        source.pointCount,
        macro::macroCurvePlaybackActive(source)
    );
    if (result.status == ProjectModulationStatus::NO_CHANGE) return true;
    if (!result.changed()) return false;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool replaceProjectControlModulation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationCurveRef& source,
    float amount,
    const macro::MacroPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
) {
    if (!validAddress(address) || !std::isfinite(amount) ||
        !macro::macroModulationCurveLifecycleValid(source)) {
        return false;
    }
    ProjectControlMacroSlotView current{};
    if (!readProjectControlMacroSlot(control, address, current) ||
        current.legacyMutationAmbiguous) {
        return false;
    }
    if (!macro::macroCurveStored(source)) {
        return !current.modulationStored ||
               clearProjectControlModulation(control, address);
    }
    if (source.pointCount == 0U || source.pointCount > sourcePointCount ||
        sourcePoints == nullptr) {
        return false;
    }

    auto pending = core::app::makeExtmemUnique<
        ProjectControlDomainState
    >();
    if (!pending) return false;
    *pending = control.authored;
    ProjectControlMacroSlotView pendingView{};
    if (!readDomainMacroSlot(*pending, address, pendingView) ||
        pendingView.legacyMutationAmbiguous) {
        return false;
    }
    if (pendingView.modulationStored &&
        !removePrimaryModulation(*pending, pendingView)) {
        return false;
    }
    if (!appendRecordedShape(
            *pending,
            address,
            source,
            amount,
            reinterpret_cast<const ProjectPackedCurvePoint*>(sourcePoints)
        ) || !validProjectModulationDomain(
            pending->modulation,
            pending->curves,
            &pending->automation
        )) {
        return false;
    }
    control.authored = *pending;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM macro::MacroAutomationConversionPlan
preflightProjectControlConversion(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    macro::MacroAutomationConversionPolicy policy,
    float currentStaticBase
) {
    auto bank = core::app::makeExtmemUnique<
        macro::MacroAutomationBankState
    >();
    if (!bank || !buildLegacyConversionBank(control, address, *bank)) {
        macro::MacroAutomationConversionPlan rejected{};
        rejected.address = address;
        rejected.policy = policy;
        rejected.status = macro::MacroAutomationConversionStatus::INVALID_BANK;
        return rejected;
    }
    return macro::macroAutomationPreflightConversion(
        *bank,
        address,
        policy,
        currentStaticBase
    );
}

FLASHMEM bool applyProjectControlConversion(
    ProjectControlState& control,
    float& staticBase,
    const macro::MacroAutomationConversionPlan& plan,
    bool overwriteConfirmed
) {
    auto bank = core::app::makeExtmemUnique<
        macro::MacroAutomationBankState
    >();
    if (!bank || !buildLegacyConversionBank(control, plan.address, *bank)) {
        return false;
    }
    float pendingBase = staticBase;
    if (!macro::macroAutomationApplyConversion(
            *bank,
            pendingBase,
            plan,
            overwriteConfirmed
        )) {
        return false;
    }
    const auto* converted = macro::macroAutomationFindSlot(
        *bank,
        plan.address
    );
    if (converted == nullptr ||
        !replaceProjectControlMacroSlot(
            control,
            plan.address,
            *converted,
            bank->pointPool.points.data(),
            bank->pointPool.used
        )) {
        return false;
    }
    staticBase = pendingBase;
    return true;
}

FLASHMEM bool setProjectControlAutomationDurationBeats(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    float durationBeats
) {
    ProjectControlMacroSlotView view{};
    if (!readProjectControlMacroSlot(control, address, view) ||
        !view.automationStored) {
        return false;
    }
    const auto* record = findProjectCurve(
        control.authored.curves,
        view.automationCurveId
    );
    if (record == nullptr ||
        record->pointCount > macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS) {
        return false;
    }
    const uint16_t targetTicks = macro::macroAutomationTicksFromBeats(
        durationBeats
    );
    if (record->durationTicks == targetTicks) return false;
    auto points = core::app::makeExtmemUnique<PackedScratch>();
    if (!points) return false;
    std::memcpy(
        points->data(),
        control.authored.curves.points.data() + record->pointOffset,
        static_cast<size_t>(record->pointCount) * sizeof((*points)[0])
    );
    ProjectCurveSpec spec{
        .sourceDurationTicks = record->sourceDurationTicks,
        .durationTicks = targetTicks,
        .windowOffsetTicks = static_cast<uint16_t>(
            record->windowOffsetTicks %
            std::max<uint16_t>(record->sourceDurationTicks, 1U)
        ),
        .interpolation = record->interpolation,
        .valueDomain = record->valueDomain,
        .origin = record->origin,
    };
    const auto result = setProjectAutomationCurve(
        control.authored.automation,
        control.authored.curves,
        projectControlDestination(address),
        spec,
        points->data(),
        record->pointCount,
        view.automationEnabled
    );
    if (!result.changed()) return false;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool setProjectControlAutomationWindowOffsetBeats(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    float offsetBeats
) {
    ProjectControlMacroSlotView view{};
    if (!readProjectControlMacroSlot(control, address, view) ||
        !view.automationStored || !std::isfinite(offsetBeats)) {
        return false;
    }
    const auto* record = findProjectCurve(
        control.authored.curves,
        view.automationCurveId
    );
    if (record == nullptr ||
        record->pointCount > macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS) {
        return false;
    }
    const float safeBeats = std::max(offsetBeats, 0.0f);
    const uint32_t rawTicks = static_cast<uint32_t>(std::clamp<long long>(
        std::llround(
            safeBeats *
            static_cast<float>(macro::MACRO_AUTOMATION_TICKS_PER_BEAT)
        ),
        0LL,
        static_cast<long long>(UINT16_MAX)
    ));
    const uint16_t targetTicks = static_cast<uint16_t>(
        rawTicks % std::max<uint16_t>(record->sourceDurationTicks, 1U)
    );
    if (record->windowOffsetTicks == targetTicks) return false;
    auto points = core::app::makeExtmemUnique<PackedScratch>();
    if (!points) return false;
    std::memcpy(
        points->data(),
        control.authored.curves.points.data() + record->pointOffset,
        static_cast<size_t>(record->pointCount) * sizeof((*points)[0])
    );
    ProjectCurveSpec spec{
        .sourceDurationTicks = record->sourceDurationTicks,
        .durationTicks = record->durationTicks,
        .windowOffsetTicks = targetTicks,
        .interpolation = record->interpolation,
        .valueDomain = record->valueDomain,
        .origin = record->origin,
    };
    const auto result = setProjectAutomationCurve(
        control.authored.automation,
        control.authored.curves,
        projectControlDestination(address),
        spec,
        points->data(),
        record->pointCount,
        view.automationEnabled
    );
    if (!result.changed()) return false;
    control.markAuthoredMutation();
    return true;
}

FLASHMEM bool readProjectControlCurvePoint(
    const ProjectControlState& control,
    ProjectCurveId curveId,
    uint16_t pointIndex,
    bool signedOutput,
    macro::MacroCurvePoint& out
) {
    const auto* record = findProjectCurve(control.authored.curves, curveId);
    if (record == nullptr || pointIndex >= record->pointCount) return false;
    const auto& point = control.authored.curves.points[
        static_cast<uint16_t>(record->pointOffset + pointIndex)
    ];
    out.beat = static_cast<float>(point.tick) /
        static_cast<float>(macro::MACRO_AUTOMATION_TICKS_PER_BEAT);
    out.value = macro::macroAutomationUnpackValue(point.value, signedOutput);
    return true;
}

FLASHMEM macro::MacroAutomationCurveWindowSummary
projectControlCurveWindowSummary(
    const ProjectControlState& control,
    ProjectCurveId curveId
) {
    macro::MacroAutomationCurveWindowSummary summary{};
    const auto* record = findProjectCurve(control.authored.curves, curveId);
    if (record == nullptr || record->pointCount == 0U) return summary;
    summary.active = true;
    summary.sourceDurationTicks = record->sourceDurationTicks;
    summary.durationTicks = record->durationTicks;
    summary.windowOffsetTicks = record->windowOffsetTicks;
    summary.firstPointTick = control.authored.curves.points[
        record->pointOffset
    ].tick;
    summary.lastPointTick = control.authored.curves.points[
        static_cast<uint16_t>(record->pointOffset + record->pointCount - 1U)
    ].tick;
    summary.pointCount = record->pointCount;
    summary.wraps = static_cast<uint32_t>(record->windowOffsetTicks) +
        record->durationTicks > record->sourceDurationTicks;
    return summary;
}

FLASHMEM float evaluateProjectControlCurve(
    const ProjectControlState& control,
    ProjectCurveId curveId,
    float elapsedBeat,
    float fallback
) {
    const auto* record = findProjectCurve(control.authored.curves, curveId);
    if (record == nullptr || record->pointCount == 0U ||
        static_cast<uint32_t>(record->pointOffset) + record->pointCount >
            control.authored.curves.pointCount) {
        return fallback;
    }

    const float safeBeat = std::isfinite(elapsedBeat)
        ? std::max(elapsedBeat, 0.0f)
        : 0.0f;
    const float elapsedTicks = safeBeat *
        static_cast<float>(macro::MACRO_AUTOMATION_TICKS_PER_BEAT);
    const uint32_t elapsedWhole = static_cast<uint32_t>(std::min<double>(
        std::floor(elapsedTicks),
        static_cast<double>(std::numeric_limits<uint32_t>::max())
    ));
    const float elapsedFraction = elapsedTicks - std::floor(elapsedTicks);
    const uint16_t duration = std::max<uint16_t>(record->durationTicks, 1U);
    const uint16_t sourceDuration = std::max<uint16_t>(
        record->sourceDurationTicks,
        1U
    );
    float sourceTick = static_cast<float>(
        (static_cast<uint32_t>(record->windowOffsetTicks) +
         (elapsedWhole % duration)) % sourceDuration
    ) + elapsedFraction;
    if (sourceTick >= sourceDuration) sourceTick -= sourceDuration;

    const auto unpack = [record](int16_t packed) {
        const float value = static_cast<float>(packed) / 32767.0f;
        return record->valueDomain == ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR
            ? std::clamp(value, 0.0f, 1.0f)
            : std::clamp(value, -1.0f, 1.0f);
    };
    const auto& arena = control.authored.curves;
    const uint16_t firstIndex = record->pointOffset;
    const uint16_t lastIndex = static_cast<uint16_t>(
        record->pointOffset + record->pointCount - 1U
    );
    const auto& first = arena.points[firstIndex];
    if (record->pointCount == 1U || sourceTick <= first.tick) {
        return unpack(first.value);
    }
    const auto& last = arena.points[lastIndex];
    if (sourceTick >= last.tick) return unpack(last.value);

    uint16_t low = 1U;
    uint16_t high = record->pointCount;
    while (low < high) {
        const uint16_t middle = static_cast<uint16_t>(
            low + (high - low) / 2U
        );
        if (arena.points[record->pointOffset + middle].tick < sourceTick) {
            low = static_cast<uint16_t>(middle + 1U);
        } else {
            high = middle;
        }
    }
    const auto& right = arena.points[record->pointOffset + low];
    const auto& left = arena.points[record->pointOffset + low - 1U];
    const float leftValue = unpack(left.value);
    const float rightValue = unpack(right.value);
    const uint16_t span = static_cast<uint16_t>(right.tick - left.tick);
    if (span == 0U) return rightValue;
    const float alpha = std::clamp(
        (sourceTick - left.tick) / static_cast<float>(span),
        0.0f,
        1.0f
    );
    return leftValue + (rightValue - leftValue) * alpha;
}

}  // namespace core::state::modulation

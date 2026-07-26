#include "state/modulation/ProjectControlMacroOpsInternal.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::modulation::project_control_macro_detail {

bool validAddress(const macro::MacroAutomationSlotAddress& address) {
    return macro::macroAutomationAddressValid(address);
}

FLASHMEM ProjectCurveSpec curveSpec(const ProjectCurveRecord& record) {
    return {
        .sourceDurationTicks = record.sourceDurationTicks,
        .durationTicks = record.durationTicks,
        .windowOffsetTicks = record.windowOffsetTicks,
        .interpolation = record.interpolation,
        .valueDomain = record.valueDomain,
        .origin = record.origin,
    };
}

FLASHMEM void projectCurveView(
    const ProjectCurveRecord& record,
    bool enabled,
    ProjectControlCurveView& out
) {
    out = {};
    out.id = record.id;
    out.spec = curveSpec(record);
    out.pointOffset = record.pointOffset;
    out.pointCount = record.pointCount;
    out.enabled = enabled;
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

FLASHMEM bool removePrimaryModulation(
    ProjectControlDomainState& domain,
    const ProjectControlMacroDestinationView& view
) {
    if (!view.primaryModulation.present() || view.mutationAmbiguous()) {
        return false;
    }
    const auto removed = removeProjectModulationBinding(
        domain.modulation,
        view.primaryModulation.bindingId
    );
    return removed.changed();
}

FLASHMEM bool appendRecordedShape(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlCurvePayload& curve,
    float amount,
    const ProjectPackedCurvePoint* points
) {
    if (!curve.stored()) return true;
    if (points == nullptr ||
        curve.spec.valueDomain != ProjectCurveValueDomain::BIPOLAR ||
        !validProjectCurveSpec(curve.spec, points, curve.pointCount)) {
        return false;
    }
    const ModulationDestination destination = projectControlDestination(address);
    RecordedShapeDraft source{};
    source.name = "Recorded Shape";
    source.curve = curve.spec;
    source.points = points;
    source.pointCount = curve.pointCount;
    source.enabled = curve.enabled;
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
    binding.application = ModulationApplication::NATURAL;
    binding.enabled = true;
    const auto bound = addProjectModulationBinding(domain.modulation, binding);
    if (bound.changed()) return true;
    (void)deleteProjectModulator(domain.modulation, domain.curves, created.sourceId);
    return false;
}

FLASHMEM bool readDomainMacroSlot(
    const ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    ProjectControlMacroDestinationView& out
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
        const bool enabled =
            (automation->flags & PROJECT_AUTOMATION_CURVE_FLAG_ENABLED) != 0U;
        projectCurveView(*curve, enabled, out.automation);
    }

    uint16_t bindingCount = 0;
    const auto* binding = firstBindingForDestination(
        domain.modulation,
        destination,
        bindingCount
    );
    out.modulationCount = bindingCount;
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
        out.primaryModulation.sourceId = source->id;
        out.primaryModulation.bindingId = binding->id;
        out.primaryModulation.enabled =
            (binding->flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U &&
            (source->flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
        out.primaryModulation.amount = std::clamp(
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
            projectCurveView(
                *curve,
                out.primaryModulation.enabled,
                out.primaryModulation.recordedShape
            );
        }
    }
    return true;
}

FLASHMEM bool replaceSlotInDomain(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlMacroDestinationPayload& sourceState,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
) {
    ProjectControlMacroDestinationView current{};
    if (!readDomainMacroSlot(domain, address, current) ||
        current.mutationAmbiguous()) {
        return false;
    }
    if (current.automation.stored() &&
        !removeProjectAutomationCurve(
            domain.automation,
            domain.curves,
            projectControlDestination(address)
        ).changed()) {
        return false;
    }
    if (current.primaryModulation.present() &&
        !removePrimaryModulation(domain, current)) {
        return false;
    }

    const uint32_t required = static_cast<uint32_t>(
        sourceState.automation.pointCount
    ) + sourceState.recordedShape.pointCount;
    if (required > sourcePointCount ||
        (required > 0U && sourcePoints == nullptr)) {
        return false;
    }
    if (sourceState.automation.stored()) {
        if (sourceState.automation.spec.valueDomain !=
            ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR) {
            return false;
        }
        const auto set = setProjectAutomationCurve(
            domain.automation,
            domain.curves,
            projectControlDestination(address),
            sourceState.automation.spec,
            sourcePoints,
            sourceState.automation.pointCount,
            sourceState.automation.enabled
        );
        if (!set.changed()) return false;
    }
    if (sourceState.recordedShape.stored()) {
        const auto* modulationPoints =
            sourcePoints + sourceState.automation.pointCount;
        if (!appendRecordedShape(
                domain,
                address,
                sourceState.recordedShape,
                sourceState.modulationAmount,
                modulationPoints
            )) {
            return false;
        }
    }
    return true;
}

}  // namespace core::state::modulation::project_control_macro_detail

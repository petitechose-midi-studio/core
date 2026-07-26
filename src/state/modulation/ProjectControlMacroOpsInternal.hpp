#pragma once

#include "state/modulation/ProjectControlMacroOps.hpp"

namespace core::state::modulation::project_control_macro_detail {

[[nodiscard]] bool validAddress(
    const macro::MacroAutomationSlotAddress& address
);

[[nodiscard]] const ModulationBindingState* firstBindingForDestination(
    const ProjectModulationState& state,
    const ModulationDestination& destination,
    uint16_t& count
);

[[nodiscard]] ModulationBindingState* bindingById(
    ProjectModulationState& state,
    ModulationBindingId id
);

[[nodiscard]] ProjectModulationFocusEntry* focusEntryFor(
    ProjectModulationFocusState& focus,
    const ModulationDestination& destination
);

[[nodiscard]] uint16_t nextFocusStamp(ProjectModulationFocusState& focus);

[[nodiscard]] ProjectModulationFocusEntry& allocateFocusEntry(
    ProjectModulationFocusState& focus,
    const ModulationDestination& destination
);

[[nodiscard]] bool removePrimaryModulation(
    ProjectControlDomainState& domain,
    const ProjectControlMacroDestinationView& view
);

[[nodiscard]] bool appendRecordedShape(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlCurvePayload& curve,
    float amount,
    const ProjectPackedCurvePoint* points
);

[[nodiscard]] bool readDomainMacroSlot(
    const ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    ProjectControlMacroDestinationView& out
);

[[nodiscard]] bool replaceSlotInDomain(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlMacroDestinationPayload& sourceState,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
);

}  // namespace core::state::modulation::project_control_macro_detail

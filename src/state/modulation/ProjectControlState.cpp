#include "state/modulation/ProjectControlState.hpp"

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::modulation {

FLASHMEM ProjectControlState::ProjectControlState() = default;

FLASHMEM void ProjectControlState::clear() {
    authored.clear();
    plan = {};
    runtime = {};
    timeTelemetry = {};
    sourceScratch.fill(0.0f);
    triggerScratch = {};
    audition = {};
    focus = {};
    authoredRevision = 1;
    compiledRevision = 0;
    runtimeContextHash = 0;
    reserved = 0;
}

FLASHMEM void ProjectControlState::markAuthoredBindingAmountMutation(
    ModulationBindingId bindingId
) {
    const bool planWasCurrent = compiledRevision == authoredRevision;
    const auto* authoredBinding = findProjectModulationBinding(
        authored.modulation,
        bindingId
    );
    markAuthoredMutation();
    if (!planWasCurrent || authoredBinding == nullptr) return;
    for (uint16_t index = 0U; index < plan.bindingCount; ++index) {
        auto& binding = plan.bindings[index];
        if (binding.id != bindingId) continue;
        binding.amountQ15 = authoredBinding->amountQ15;
        break;
    }
    compiledRevision = authoredRevision;
}

FLASHMEM void ProjectControlState::markAuthoredDestinationScaleMutation(
    const ModulationDestination& destination
) {
    const bool planWasCurrent = compiledRevision == authoredRevision;
    const bool destinationValid = modulationDestinationValid(destination);
    const uint16_t authoredScale = destinationValid
        ? projectModulationDestinationScaleQ15(
            authored.modulation,
            destination
        )
        : PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    markAuthoredMutation();
    if (!planWasCurrent || !destinationValid) return;
    for (uint16_t index = 0U; index < plan.destinationCount; ++index) {
        auto& runtimeDestination = plan.destinations[index];
        if (runtimeDestination.destination != destination) continue;
        runtimeDestination.destinationScaleQ15 = authoredScale;
        break;
    }
    compiledRevision = authoredRevision;
}

}  // namespace core::state::modulation

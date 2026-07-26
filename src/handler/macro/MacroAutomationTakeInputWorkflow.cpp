#include "handler/macro/MacroAutomationTakeInputWorkflow.hpp"

#include <config/PlatformCompat.hpp>

#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "midi/MidiUtils.hpp"
#include "state/macro/MacroAutomationDomain.hpp"

namespace core::handler {

FLASHMEM bool MacroAutomationTakeInputWorkflow::recordAndPublish(
    const MacroPerformanceDomainServices& services,
    MacroMidiCcRuntimeAdapter& midiRuntime,
    uint8_t macro,
    uint32_t nowMs,
    float absoluteBase
) {
    const float base = core::state::macro::macroAutomationClamp01(absoluteBase);
    if (!services.recordAutomationTakeValue(macro, nowMs, base)) return false;

    const auto resolved = services.resolveManualValue(macro, base);
    services.setResolvedValue(macro, resolved);
    if (services.isActivePageEnabled()) {
        (void)midiRuntime.publishLiveManual(
            macro,
            core::midi::toCC(resolved.resolved)
        );
    }
    return true;
}

}  // namespace core::handler

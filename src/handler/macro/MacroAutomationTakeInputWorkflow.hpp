#pragma once

#include <cstdint>

namespace core::handler {

class MacroPerformanceDomainServices;
class MacroMidiCcRuntimeAdapter;

/** Canonical live-input projection for the single Automation take recorder. */
class MacroAutomationTakeInputWorkflow {
public:
    [[nodiscard]] static bool recordAndPublish(
        const MacroPerformanceDomainServices& services,
        MacroMidiCcRuntimeAdapter& midiRuntime,
        uint8_t macro,
        uint32_t nowMs,
        float absoluteBase
    );
};

}  // namespace core::handler

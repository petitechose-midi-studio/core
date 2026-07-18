#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/macro/MacroAutomationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/shared/MidiCcDestinationResolver.hpp"

namespace core::handler {

class MacroMidiCcRuntimeAdapter;

class MacroAutomationPlaybackService {
public:
    struct StateRefs {
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
        const oc::state::Signal<uint32_t>* runtimeOwnerRevision = nullptr;
    };

    MacroAutomationPlaybackService(StateRefs state,
                                   MacroPerformanceDomainServices services,
                                   MacroMidiCcRuntimeAdapter& midiRuntime);

    void update(uint32_t nowMs);
    void reset();

private:
    struct FramePublicationContext;

    void consumeRuntimeOwnerActivation_();
    core::state::modulation::ProjectModulationCompileContext compileContext_() const;
    bool ensureProjectRuntime_(
        const core::state::modulation::ProjectControlTimeSnapshot& time
    );
    static bool provideBase_(
        void* context,
        uint16_t destinationIndex,
        const core::state::modulation::ModulationDestination& destination,
        core::state::modulation::ProjectLogicalMacroBaseInput& out
    );
    static void captureRuntimeDestination_(
        void* context,
        uint16_t destinationIndex,
        const core::state::modulation::ProjectLogicalMacroRuntimeValue& value
    );
    static bool produceProjectFrame_(
        void* context,
        core::state::shared::MidiCcCandidate* destination,
        uint16_t capacity,
        uint16_t& written
    );
    bool appendStaticAuthors_(FramePublicationContext& context) const;
    [[nodiscard]] uint16_t activeAuthorCount_() const;
    void stageVisibleProjection_(
        FramePublicationContext& context,
        const core::state::modulation::ProjectLogicalMacroRuntimeValue& value
    );
    void syncActivePageRuntimeUi_(uint8_t track, uint8_t page);
    void invalidateComputedRuntime_();

    core::state::macro::MacroPagesState& pages_;
    core::state::macro::MacroUiState& macro_ui_;
    const oc::state::Signal<uint32_t>* runtime_owner_revision_ = nullptr;
    MacroPerformanceDomainServices services_;
    MacroMidiCcRuntimeAdapter& midi_runtime_;

    bool update_scheduled_ = false;
    uint32_t next_due_ms_ = 0;
    uint32_t consumed_runtime_owner_revision_ = 0;
    uint8_t cached_track_ = 0xFF;
    uint8_t cached_page_ = 0xFF;
};

// Hot service: keep the target-side default-heap allocation intentionally small.
static_assert(
    sizeof(void*) != 4U || sizeof(MacroAutomationPlaybackService) == 80U
);

}  // namespace core::handler

#pragma once

#include <array>
#include <cstdint>

#include <oc/api/MidiAPI.hpp>
#include <oc/state/Signal.hpp>

#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/StatusBarState.hpp"
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
        core::state::StatusBarState& statusBar;
        const oc::state::Signal<uint32_t>* runtimeOwnerRevision = nullptr;
    };

    MacroAutomationPlaybackService(StateRefs state,
                                   MacroPerformanceDomainServices services,
                                   MacroMidiCcRuntimeAdapter& midiRuntime);

    /**
     * Compatibility-only direct-output path for isolated legacy tests.
     * Standalone production assembly must inject MacroMidiCcRuntimeAdapter.
     */
    [[deprecated("Inject MacroMidiCcRuntimeAdapter in production")]]
    MacroAutomationPlaybackService(StateRefs state,
                                   MacroPerformanceDomainServices services,
                                   oc::api::MidiAPI& midi);

    void update(uint32_t nowMs);
    void reset();

private:
    struct FramePublicationContext;

    void consumeRuntimeOwnerActivation_(uint32_t nowMs);
    void updatePlaybackBeat_(uint32_t nowMs);
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
    void updateVisibleProjection_(
        uint16_t destinationIndex,
        const core::state::modulation::ProjectLogicalMacroRuntimeValue& value
    );
    void syncActivePageRuntimeProjection_(uint8_t track, uint8_t page);
    void invalidateComputedRuntime_();
    void invalidateSentCache_();

    core::state::macro::MacroPagesState& pages_;
    core::state::macro::MacroUiState& macro_ui_;
    core::state::StatusBarState& status_bar_;
    const oc::state::Signal<uint32_t>* runtime_owner_revision_ = nullptr;
    MacroPerformanceDomainServices services_;
    MacroMidiCcRuntimeAdapter* midi_runtime_ = nullptr;
    oc::api::MidiAPI* direct_midi_fallback_ = nullptr;

    bool was_playing_ = false;
    bool update_scheduled_ = false;
    uint32_t last_update_ms_ = 0;
    uint32_t next_due_ms_ = 0;
    uint32_t consumed_runtime_owner_revision_ = 0;
    float playback_beat_ = 0.0f;
    uint8_t cached_track_ = 0xFF;
    uint8_t cached_page_ = 0xFF;
    std::array<uint8_t, core::state::macro::MACRO_COUNT> sent_cc_values_{};
};

}  // namespace core::handler

#pragma once

#include <cstdint>

#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/macro/MacroConstants.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/project/ProjectTrackState.hpp"

namespace core::handler {

/**
 * Macro-domain author adapter for the singular Gate 8 LIVE coordinator.
 *
 * MacroAutomationPlaybackService is the sole complete-frame producer. This
 * adapter only replaces an already-published stable author for immediate
 * manual response, so a local fallback can never discard other Tracks.
 */
class MacroMidiCcRuntimeAdapter final {
public:
    struct StateRefs {
        core::state::macro::MacroPagesState& pages;
        const core::state::project::ProjectTrackState& projectTracks;
    };

    MacroMidiCcRuntimeAdapter(
        StateRefs state,
        MacroPerformanceDomainServices services,
        MidiCcGlobalFrameCoordinator& coordinator
    );

    MacroMidiCcRuntimeAdapter(const MacroMidiCcRuntimeAdapter&) = delete;
    MacroMidiCcRuntimeAdapter& operator=(const MacroMidiCcRuntimeAdapter&) = delete;

    /** Publishes one immediate already-resolved encoder/manual movement. */
    MidiCcGlobalFrameResult publishLiveManual(uint8_t macroIndex, uint8_t value);

    bool publishProjectFrame(
        MidiCcGlobalFrameCoordinator::PersistentAuthorProducer producer,
        void* context
    );
    [[nodiscard]] core::state::modulation::ProjectControlTimeSnapshot
        projectControlTimeSnapshot() const;
    [[nodiscard]] bool projectModulationTriggersPending() const;
    uint16_t drainProjectModulationTriggers(
        core::state::modulation::ProjectModulationTriggerFrame& out
    );

    static constexpr uint16_t stableAddress(
        uint8_t track,
        uint8_t page,
        uint8_t macroIndex
    ) {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(track) * core::state::macro::PAGE_COUNT + page) *
                core::state::macro::MACRO_COUNT +
            macroIndex
        );
    }

private:
    core::state::macro::MacroPagesState& pages_;
    const core::state::project::ProjectTrackState& project_tracks_;
    MacroPerformanceDomainServices services_;
    MidiCcGlobalFrameCoordinator& coordinator_;
};

static_assert(
    MacroMidiCcRuntimeAdapter::stableAddress(
        core::state::macro::TRACK_COUNT - 1U,
        core::state::macro::PAGE_COUNT - 1U,
        core::state::macro::MACRO_COUNT - 1U
    ) < 0xFFFFU
);

}  // namespace core::handler

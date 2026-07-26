#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"

#include "state/project/ProjectTrackDomainOps.hpp"

namespace core::handler {

MacroMidiCcRuntimeAdapter::MacroMidiCcRuntimeAdapter(
    StateRefs state,
    MacroPerformanceDomainServices services,
    MidiCcGlobalFrameCoordinator& coordinator
)
    : pages_(state.pages)
    , project_tracks_(state.projectTracks)
    , services_(services)
    , coordinator_(coordinator) {}

MidiCcGlobalFrameResult MacroMidiCcRuntimeAdapter::publishLiveManual(
    uint8_t macroIndex,
    uint8_t value
) {
    if (macroIndex >= core::state::macro::MACRO_COUNT || value > 127U) {
        return MidiCcGlobalFrameResult{};
    }
    if (!services_.isActivePageEnabled() ||
        !services_.isMacroSlotActive(macroIndex)) {
        return MidiCcGlobalFrameResult{};
    }
    const uint8_t activeTrack = pages_.currentActiveTrack();
    const uint16_t audible = core::state::project::audibleMask(
        project_tracks_,
        pages_.currentTrackEnabledMask()
    );
    if ((audible & static_cast<uint16_t>(1U << activeTrack)) == 0U) {
        // Manual input remains authored by the Macro workflow, but an
        // inaudible Track must not replace or emit a physical MIDI author.
        return MidiCcGlobalFrameResult{};
    }
    const auto& config = services_.activeConfig(macroIndex);
    const uint8_t channel = core::state::project::projectTrackMidiChannel(
        project_tracks_, activeTrack
    );
    const auto candidate = core::state::shared::MidiCcCandidate{
        .destination = core::state::shared::MidiCcDestination{
            .identity = core::state::shared::MidiCcDestinationIdentity{
                .port = MidiCcGlobalFrameCoordinator::OUTPUT_PORT,
                .channel = channel,
                .controller = config.cc,
            },
            .routeValidity = core::state::shared::MidiCcRouteValidity::VALID,
        },
        .author = core::state::shared::MidiCcAuthor{
            .candidateClass =
                core::state::shared::MidiCcCandidateClass::LIVE_MANUAL,
            .stableAddress = stableAddress(
                activeTrack,
                pages_.currentActivePage(),
                macroIndex
            ),
        },
        .localValue = value,
    };
    uint16_t candidateCount = 0;
    if (!coordinator_.upsertPersistentAuthor(candidate, candidateCount)) {
        return MidiCcGlobalFrameResult{};
    }
    return {
        .status = MidiCcGlobalFrameStatus::OK,
        .resolveStatus = core::state::shared::MidiCcResolveStatus::OK,
        .candidateCount = candidateCount,
    };
}

bool MacroMidiCcRuntimeAdapter::publishProjectFrame(
    MidiCcGlobalFrameCoordinator::PersistentAuthorProducer producer,
    void* context
) {
    return coordinator_.publishPersistentAuthorsGenerated(producer, context);
}

core::state::modulation::ProjectControlTimeSnapshot
MacroMidiCcRuntimeAdapter::projectControlTimeSnapshot() const {
    return coordinator_.projectControlTimeSnapshot();
}

bool MacroMidiCcRuntimeAdapter::projectModulationTriggersPending() const {
    return coordinator_.hasPendingProjectModulationTriggers();
}

uint16_t MacroMidiCcRuntimeAdapter::drainProjectModulationTriggers(
    core::state::modulation::ProjectModulationTriggerFrame& out
) {
    return coordinator_.drainProjectModulationTriggers(out);
}

}  // namespace core::handler

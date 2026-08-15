#include "handler/sequencer/SequencerCcLaneDomainServices.hpp"

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::handler {

namespace seq = core::state::sequencer;
namespace shared = core::state::shared;

FLASHMEM SequencerCcLaneDomainServices::SequencerCcLaneDomainServices(StateRefs state)
    : editor_(state.editor)
    , tracks_(state.tracks)
    , project_tracks_(state.projectTracks)
    , macro_pages_(state.macroPages) {}

FLASHMEM seq::SequencerCcTrackRoute SequencerCcLaneDomainServices::trackRoute(
    uint8_t track
) const {
    return seq::makeSequencerCcTrackRoute(
        0,
        core::state::project::projectTrackMidiChannel(project_tracks_, track)
    );
}

FLASHMEM seq::SequencerCcProjectRoutingView
SequencerCcLaneDomainServices::routingView() const {
    seq::SequencerCcProjectRoutingView project{};
    for (uint8_t track = 0; track < project.size(); ++track) {
        const auto& pattern = seq::canonicalTrackPattern(tracks_, editor_, track);
        project[track] = {
            .lanes = seq::sequencerCcLaneView(pattern),
            .trackRoute = trackRoute(track),
        };
    }
    return project;
}

FLASHMEM bool SequencerCcLaneDomainServices::conflictsWithActiveMacro_(
    const shared::MidiCcDestination& destination
) const {
    if (macro_pages_ == nullptr ||
        destination.routeValidity != shared::MidiCcRouteValidity::VALID) {
        return false;
    }
    const auto& page = macro_pages_->activePageData();
    const uint8_t channel = core::state::project::projectTrackMidiChannel(
        project_tracks_,
        macro_pages_->currentActiveTrack()
    );
    for (uint8_t slot = 0; slot < core::state::macro::MACRO_COUNT; ++slot) {
        if (!page.isMacroActive(slot)) continue;
        const auto identity = shared::MidiCcDestinationIdentity{
            .port = 0,
            .channel = channel,
            .controller = page.cc[slot],
        };
        if (shared::sameMidiCcDestinationIdentity(
                destination.identity,
                identity
            )) {
            return true;
        }
    }
    return false;
}

FLASHMEM SequencerCcLanePreflight SequencerCcLaneDomainServices::preflight(
    uint8_t track,
    uint8_t lane,
    const seq::SequencerCcLaneDraft& draft
) const {
    SequencerCcLanePreflight result{};
    if (track >= seq::SequencerTrackBankState::TRACK_COUNT ||
        lane >= seq::SequencerCcLaneBank::MAX_LANES ||
        !seq::validSequencerCcLaneDraft(draft)) {
        return result;
    }

    const auto project = routingView();
    const auto duplicate = seq::preflightSequencerCcLaneDestination(
        project,
        {.track = track, .lane = lane},
        draft
    );
    result.laneConflict = duplicate.duplicate;
    result.conflictingLane = duplicate.existing;
    result.resolvedDestination = duplicate.resolvedCandidate;
    result.routeValid = duplicate.resolvedCandidate.routeValidity ==
        shared::MidiCcRouteValidity::VALID;
    result.macroConflict = !result.laneConflict &&
        conflictsWithActiveMacro_(result.resolvedDestination);
    return result;
}

}  // namespace core::handler

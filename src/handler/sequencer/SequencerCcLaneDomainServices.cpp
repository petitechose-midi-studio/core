#include "handler/sequencer/SequencerCcLaneDomainServices.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerCcLanePatternOps.hpp"

namespace core::handler {

namespace seq = core::state::sequencer;
namespace shared = core::state::shared;

FLASHMEM SequencerCcLaneDomainServices::SequencerCcLaneDomainServices(StateRefs state)
    : editor_(state.editor)
    , tracks_(state.tracks)
    , macro_pages_(state.macroPages) {}

FLASHMEM const seq::SequencerPatternState& SequencerCcLaneDomainServices::pattern_(
    uint8_t track
) const {
    const uint8_t clamped = seq::SequencerTrackBankState::clampTrackIndex(track);
    return clamped == tracks_.activeTrackIndex()
        ? editor_.pattern
        : tracks_.track(clamped);
}

FLASHMEM seq::SequencerCcTrackRoute SequencerCcLaneDomainServices::trackRoute(
    uint8_t track
) const {
    return seq::makeSequencerCcTrackRoute(
        0,
        pattern_(track).midiChannel.get()
    );
}

FLASHMEM seq::SequencerCcProjectRoutingView
SequencerCcLaneDomainServices::routingView() const {
    seq::SequencerCcProjectRoutingView project{};
    for (uint8_t track = 0; track < project.size(); ++track) {
        const auto& pattern = pattern_(track);
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
    const uint8_t channel = macro_pages_->activeTrackData().channel;
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

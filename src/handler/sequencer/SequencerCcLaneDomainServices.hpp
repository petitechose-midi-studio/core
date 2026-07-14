#pragma once

#include "state/macro/MacroPagesState.hpp"
#include "state/sequencer/SequencerCcLaneRouting.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

struct SequencerCcLanePreflight {
    bool routeValid = false;
    bool laneConflict = false;
    bool macroConflict = false;
    core::state::sequencer::SequencerCcLaneAddress conflictingLane{};
    core::state::shared::MidiCcDestination resolvedDestination{};

    [[nodiscard]] bool blocked() const { return laneConflict; }
    [[nodiscard]] bool warning() const { return !laneConflict && macroConflict; }
};

/** Read-only, allocation-free project preflight shared by Add and Settings. */
class SequencerCcLaneDomainServices {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& editor;
        core::state::sequencer::SequencerTrackBankState& tracks;
        const core::state::macro::MacroPagesState* macroPages = nullptr;
    };

    explicit SequencerCcLaneDomainServices(StateRefs state);

    [[nodiscard]] core::state::sequencer::SequencerCcProjectRoutingView
    routingView() const;

    [[nodiscard]] SequencerCcLanePreflight preflight(
        uint8_t track,
        uint8_t lane,
        const core::state::sequencer::SequencerCcLaneDraft& draft
    ) const;

    [[nodiscard]] core::state::sequencer::SequencerCcTrackRoute trackRoute(
        uint8_t track
    ) const;

private:
    const core::state::sequencer::SequencerPatternState& pattern_(uint8_t track) const;
    bool conflictsWithActiveMacro_(
        const core::state::shared::MidiCcDestination& destination
    ) const;

    core::state::sequencer::SequencerState& editor_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    const core::state::macro::MacroPagesState* macro_pages_ = nullptr;
};

}  // namespace core::handler

#pragma once

#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler::sequencer::step_content_draft_workflow {

enum class BackResult : uint8_t {
    NONE = 0,
    CONTINUE_EDITING,
    DISCARDED,
    SAVED,
    FAILED,
};

[[nodiscard]] bool apply(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::handler::SequencerHistoryDomainServices& history
);

[[nodiscard]] BackResult requestBack(
    core::state::sequencer::SequencerState& sequencer
);
[[nodiscard]] BackResult requestBack(
    core::state::sequencer::SequencerState& sequencer,
    const core::handler::SequencerHistoryDomainServices& history
);

void moveExitChoice(
    core::state::sequencer::SequencerState& sequencer,
    float delta
);

[[nodiscard]] BackResult applyExitChoice(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::handler::SequencerHistoryDomainServices& history
);

}  // namespace core::handler::sequencer::step_content_draft_workflow

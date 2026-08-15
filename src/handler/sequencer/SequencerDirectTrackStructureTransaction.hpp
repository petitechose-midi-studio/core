#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/sequencer/SequencerPreparedTrackStructureTransaction.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/StructureNavigationState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

/** Product wiring shared by direct Sequencer Track creation and removal. */
struct SequencerDirectTrackStructureStateRefs {
    core::state::sequencer::SequencerTrackBankState& tracks;
    core::state::sequencer::SequencerState& sequencer;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
    core::state::TrackNavigationState& trackNavigation;
    core::state::StructureClipboardState& clipboard;
    core::state::macro::MacroPagesState& macroPages;
    core::state::sequencer::SequencerTrackActivationQueue& activationQueue;
    SharedTrackDomainServices sharedTracks;
    SequencerHistoryDomainServices history;
};

static_assert(
    sizeof(void*) != 4U ||
        sizeof(SequencerDirectTrackStructureStateRefs) == 64U,
    "direct Track Structure state facade exceeds its ARM stack contract"
);

[[nodiscard]] SequencerPreparedTrackStructureResult
executeSequencerCreateTrackStructure(
    SequencerDirectTrackStructureStateRefs state,
    core::state::sequencer::SequencerTrackKind kind =
        core::state::sequencer::SequencerTrackKind::INSTRUMENT,
    core::state::sequencer::DrumKitPreset drumPreset =
        core::state::sequencer::DrumKitPreset::GENERAL_MIDI
);

[[nodiscard]] SequencerPreparedTrackStructureResult
executeSequencerRemoveCurrentTrackStructure(
    SequencerDirectTrackStructureStateRefs state,
    uint8_t latchedTargetTrack
);

[[nodiscard]] SequencerPreparedTrackStructureResult
executeSequencerRemoveSelectionTrackStructure(
    SequencerDirectTrackStructureStateRefs state,
    uint8_t latchedActiveTrack
);

}  // namespace core::handler

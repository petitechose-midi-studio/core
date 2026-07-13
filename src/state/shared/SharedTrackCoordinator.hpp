#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/macro/MacroPagesState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::shared {

/**
 * Coordinates the shared track invariant across macro, sequencer, and UI state.
 *
 * This boundary owns mask/active-track sanitization and the synchronized writes
 * to macro pages, sequencer track bank, active sequencer editor, and shared UI
 * signals. Persistence scheduling stays with the app-level owner that calls it.
 */
struct SharedTrackCoordinator {
    struct StateRefs {
        oc::state::Signal<uint8_t, 8>& activeTrack;
        oc::state::Signal<uint16_t, 16>& enabledMask;
        macro::MacroPagesState& macroPages;
        sequencer::SequencerTrackBankState& sequencerTracks;
        sequencer::SequencerState& sequencer;
    };

    struct Result {
        uint16_t enabledMask = 0x0001;
        uint8_t activeTrack = 0;
        bool changed = false;
        bool ok = true;
    };

    static uint16_t sanitizeEnabledMask(uint16_t enabledMask);
    static uint8_t sanitizeActiveTrack(uint16_t enabledMask, uint8_t activeTrack);
    static Result apply(StateRefs state, uint16_t enabledMask, uint8_t activeTrack);
    /**
     * Publishes a sequencer Track transaction whose target editor and bank
     * state have already been prepared. Unlike apply(), this never copies a
     * graph and therefore cannot fail after the transaction starts mutating.
     */
    static Result publishPreparedSequencerState(
        StateRefs state,
        uint16_t enabledMask,
        uint8_t activeTrack
    );
    static Result refreshFromMacroPages(StateRefs state);
    static Result refreshFromSequencer(StateRefs state);
};

}  // namespace core::state::shared

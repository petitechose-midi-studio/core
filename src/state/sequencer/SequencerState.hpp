#pragma once

/**
 * @file SequencerState.hpp
 * @brief Sequencer state for Core UI + v0 playback engine integration
 */

#include <cstdint>

#include <oc/note/sequencer/StepSequencerState.hpp>
#include <oc/state/Signal.hpp>

namespace core::state::sequencer {

using oc::state::Signal;

/**
 * @brief Core sequencer state
 *
 * This extends the reusable engine state (oc-note) with UI-only fields.
 */
struct SequencerState : public oc::note::sequencer::StepSequencerState {
    static constexpr uint8_t STEPS_PER_PAGE = 8;
    static constexpr uint8_t MAX_STEPS = oc::note::sequencer::StepSequencerState::MAX_STEPS;
    static constexpr uint8_t PAGE_COUNT = (MAX_STEPS + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE;

    /// Visible page index [0..PAGE_COUNT-1]
    Signal<uint8_t> page{0};

    /// Absolute focused step index [0..length-1]
    Signal<uint8_t> focusedStep{0};

    void reset() {
        oc::note::sequencer::StepSequencerState::reset();
        page.set(0);
        focusedStep.set(0);
    }

    uint8_t activePageCount() const {
        const uint8_t len = length.get();
        if (len == 0) return 0;
        const uint8_t pages = static_cast<uint8_t>((len + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE);
        return (pages > PAGE_COUNT) ? PAGE_COUNT : pages;
    }

    bool isInPattern(uint8_t step) const {
        return step < length.get();
    }
};

}  // namespace core::state::sequencer

#pragma once

/**
 * @file SequencerState.hpp
 * @brief Minimal sequencer UI state (standalone, UI-first)
 */

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state::sequencer {

using oc::state::Signal;

/**
 * @brief Minimal sequencer state used by SequencerView + handlers
 *
 * v0 focuses on UI interactions (step enable + paging). Engine and persistence
 * will be added later.
 */
struct SequencerState {
    static constexpr uint8_t STEPS_PER_PAGE = 8;
    static constexpr uint8_t MAX_STEPS = 16;
    static constexpr uint8_t PAGE_COUNT = (MAX_STEPS + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE;

    /// Visible page index [0..PAGE_COUNT-1]
    Signal<uint8_t> page{0};

    /// Absolute focused step index [0..MAX_STEPS-1]
    Signal<uint8_t> focusedStep{0};

    /// Enabled step bitmask (bit i => step i enabled)
    Signal<uint64_t> enabledMask{0};

    void reset() {
        page.set(0);
        focusedStep.set(0);
        enabledMask.set(0);
    }

    bool isEnabled(uint8_t step) const {
        if (step >= MAX_STEPS) return false;
        const uint64_t mask = enabledMask.get();
        return (mask & (1ULL << step)) != 0;
    }

    void setEnabled(uint8_t step, bool enabled) {
        if (step >= MAX_STEPS) return;
        uint64_t mask = enabledMask.get();
        if (enabled) {
            mask |= (1ULL << step);
        } else {
            mask &= ~(1ULL << step);
        }
        enabledMask.set(mask);
    }

    void toggle(uint8_t step) {
        if (step >= MAX_STEPS) return;
        const uint64_t next = enabledMask.get() ^ (1ULL << step);
        enabledMask.set(next);
    }
};

}  // namespace core::state::sequencer

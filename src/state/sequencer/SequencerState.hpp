#pragma once

/**
 * @file SequencerState.hpp
 * @brief Minimal sequencer UI state (standalone, UI-first)
 */

#include <array>
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
    static constexpr uint8_t MAX_STEPS = 64;
    static constexpr uint8_t PAGE_COUNT = (MAX_STEPS + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE;
    static constexpr uint8_t DEFAULT_LENGTH = 18;

    SequencerState() { reset(); }

    /// Pattern length in steps (v0: fixed at 18, menus later)
    Signal<uint8_t> length{DEFAULT_LENGTH};

    /// Visible page index [0..PAGE_COUNT-1]
    Signal<uint8_t> page{0};

    /// Absolute focused step index [0..MAX_STEPS-1]
    Signal<uint8_t> focusedStep{0};

    /// Absolute playhead step index [0..length-1], or -1 for "none"
    Signal<int16_t> playheadStep{-1};

    /// Enabled step bitmask (bit i => step i enabled)
    Signal<uint64_t> enabledMask{0};

    // Step data (UI-first; engine/persistence later)
    std::array<uint8_t, MAX_STEPS> note{};      // MIDI note number
    std::array<uint8_t, MAX_STEPS> gate{};      // 0..100
    std::array<uint8_t, MAX_STEPS> velocity{};  // 0..100
    std::array<int8_t, MAX_STEPS> nudge{};      // -50..50

    uint8_t activePageCount() const {
        const uint8_t len = length.get();
        if (len == 0) return 0;
        const uint8_t pages = static_cast<uint8_t>((len + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE);
        return (pages > PAGE_COUNT) ? PAGE_COUNT : pages;
    }

    bool isInPattern(uint8_t step) const {
        return step < length.get();
    }

    void reset() {
        length.set(DEFAULT_LENGTH);
        page.set(0);
        focusedStep.set(0);
        playheadStep.set(-1);
        enabledMask.set(0);

        for (uint8_t i = 0; i < MAX_STEPS; ++i) {
            note[i] = static_cast<uint8_t>(48 + (i % 12));  // C3..B3
            gate[i] = 75;
            velocity[i] = 80;
            nudge[i] = 0;
        }
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

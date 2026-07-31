#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "state/modulation/ProjectControlState.hpp"

namespace core::sequencer {

/**
 * Bounded SPSC bridge from physically dispatched Note edges to Project
 * modulation evaluation.
 *
 * The realtime MIDI consumer is the sole producer. The application-side
 * Project runtime is the sole consumer. Overflow is reported once through the
 * next drained frame; no allocation, lock or retry path is introduced.
 */
class ProjectModulationTriggerQueue final {
public:
    static constexpr uint16_t CAPACITY =
        core::state::modulation::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY;

    [[nodiscard]] bool enqueue(
        const core::state::modulation::ProjectModulationTriggerEvent& event
    );
    [[nodiscard]] bool hasPending() const;
    uint16_t drain(
        core::state::modulation::ProjectModulationTriggerFrame& out
    );
    void reset();

private:
    static constexpr uint16_t RING_MASK = CAPACITY - 1U;

    std::array<
        core::state::modulation::ProjectModulationTriggerEvent,
        CAPACITY
    > events_{};
    std::atomic<uint16_t> write_sequence_{0U};
    std::atomic<uint16_t> read_sequence_{0U};
    std::atomic<uint16_t> overflow_sequence_{0U};
    uint16_t last_drained_overflow_sequence_ = 0U;
};

static_assert((ProjectModulationTriggerQueue::CAPACITY &
               (ProjectModulationTriggerQueue::CAPACITY - 1U)) == 0U);
static_assert(std::atomic<uint16_t>::is_always_lock_free);

}  // namespace core::sequencer

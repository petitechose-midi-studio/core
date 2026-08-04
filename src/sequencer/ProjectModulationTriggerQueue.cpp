#include "sequencer/ProjectModulationTriggerQueue.hpp"

#include <algorithm>

namespace core::sequencer {

bool ProjectModulationTriggerQueue::enqueue(
    const core::state::modulation::ProjectModulationTriggerEvent& event
) {
    const uint16_t write = write_sequence_.load(std::memory_order_relaxed);
    const uint16_t read = read_sequence_.load(std::memory_order_acquire);
    if (static_cast<uint16_t>(write - read) >= CAPACITY) {
        (void)overflow_sequence_.fetch_add(
            1U,
            std::memory_order_release
        );
        return false;
    }
    events_[write & RING_MASK] = event;
    write_sequence_.store(
        static_cast<uint16_t>(write + 1U),
        std::memory_order_release
    );
    return true;
}

bool ProjectModulationTriggerQueue::hasPending() const {
    const uint16_t write = write_sequence_.load(std::memory_order_acquire);
    const uint16_t read = read_sequence_.load(std::memory_order_relaxed);
    const uint16_t overflow = overflow_sequence_.load(
        std::memory_order_acquire
    );
    return write != read ||
        overflow != last_drained_overflow_sequence_;
}

uint16_t ProjectModulationTriggerQueue::drain(
    core::state::modulation::ProjectModulationTriggerFrame& out
) {
    out.count = 0U;
    const uint16_t overflow = overflow_sequence_.load(
        std::memory_order_acquire
    );
    out.droppedEventCount = static_cast<uint16_t>(
        overflow - last_drained_overflow_sequence_
    );
    last_drained_overflow_sequence_ = overflow;

    const uint16_t read = read_sequence_.load(std::memory_order_relaxed);
    const uint16_t write = write_sequence_.load(std::memory_order_acquire);
    const uint16_t available = static_cast<uint16_t>(write - read);
    const uint16_t count = std::min<uint16_t>(
        available,
        static_cast<uint16_t>(out.events.size())
    );
    for (uint16_t index = 0U; index < count; ++index) {
        out.events[index] =
            events_[static_cast<uint16_t>(read + index) & RING_MASK];
    }
    out.count = count;
    read_sequence_.store(
        static_cast<uint16_t>(read + count),
        std::memory_order_release
    );
    return count;
}

void ProjectModulationTriggerQueue::reset() {
    events_ = {};
    write_sequence_.store(0U, std::memory_order_relaxed);
    read_sequence_.store(0U, std::memory_order_relaxed);
    overflow_sequence_.store(0U, std::memory_order_relaxed);
    last_drained_overflow_sequence_ = 0U;
}

}  // namespace core::sequencer

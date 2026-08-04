#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>

#include "sequencer/ProjectModulationTriggerQueue.hpp"

namespace {

namespace mod = core::state::modulation;
using core::sequencer::ProjectModulationTriggerQueue;

mod::ProjectModulationTriggerEvent event(uint8_t note) {
    return {
        .trigger = {
            mod::ModulationTriggerKind::TRACK_NOTE,
            2U,
            3U,
            note,
        },
        .edge = mod::ProjectModulationTriggerEdge::GATE_ON,
        .velocity = static_cast<uint8_t>(note + 1U),
    };
}

void test_order_is_preserved_and_events_are_consumed_once() {
    ProjectModulationTriggerQueue queue{};
    assert(queue.enqueue(event(60U)));
    assert(queue.enqueue(event(61U)));
    assert(queue.hasPending());

    mod::ProjectModulationTriggerFrame frame{};
    assert(queue.drain(frame) == 2U);
    assert(frame.count == 2U);
    assert(frame.droppedEventCount == 0U);
    assert(frame.events[0].trigger.data == 60U);
    assert(frame.events[1].trigger.data == 61U);
    assert(!queue.hasPending());

    assert(queue.drain(frame) == 0U);
    assert(frame.count == 0U);
    assert(frame.droppedEventCount == 0U);
}

void test_overflow_is_reported_once_without_reordering_retained_events() {
    ProjectModulationTriggerQueue queue{};
    for (uint16_t index = 0U;
         index < ProjectModulationTriggerQueue::CAPACITY;
         ++index) {
        assert(queue.enqueue(event(static_cast<uint8_t>(index & 0x7FU))));
    }
    assert(!queue.enqueue(event(127U)));

    mod::ProjectModulationTriggerFrame frame{};
    assert(queue.drain(frame) == ProjectModulationTriggerQueue::CAPACITY);
    assert(frame.droppedEventCount == 1U);
    assert(frame.events[0].trigger.data == 0U);
    assert(frame.events[ProjectModulationTriggerQueue::CAPACITY - 1U]
               .trigger.data ==
           static_cast<uint8_t>(
               (ProjectModulationTriggerQueue::CAPACITY - 1U) & 0x7FU
           ));
    assert(!queue.hasPending());

    assert(queue.drain(frame) == 0U);
    assert(frame.droppedEventCount == 0U);
}

void test_reset_discards_events_and_overflow_accounting() {
    ProjectModulationTriggerQueue queue{};
    assert(queue.enqueue(event(64U)));
    queue.reset();

    mod::ProjectModulationTriggerFrame frame{};
    assert(!queue.hasPending());
    assert(queue.drain(frame) == 0U);
    assert(frame.droppedEventCount == 0U);
}

}  // namespace

int main() {
    test_order_is_preserved_and_events_are_consumed_once();
    test_overflow_is_reported_once_without_reordering_retained_events();
    test_reset_discards_events_and_overflow_accounting();
    std::cout << "ProjectModulationTriggerQueue tests passed\n";
    return 0;
}

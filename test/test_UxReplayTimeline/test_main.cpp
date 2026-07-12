#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

#include "../../sdl/integration/UxReplayTimeline.hpp"

using sdl::integration::UxReplayTimeline;

namespace {

void test_first_action_uses_script_deadline() {
    UxReplayTimeline timeline;
    assert(timeline.schedule(500) == 500);
}

void test_on_time_actions_keep_script_deadlines() {
    UxReplayTimeline timeline;
    timeline.record(100, 100);
    assert(timeline.schedule(250) == 250);
}

void test_late_action_preserves_next_interval() {
    UxReplayTimeline timeline;
    timeline.record(8550, 9356);

    assert(timeline.schedule(9600) == 10406);
    timeline.record(9600, 10406);
    assert(timeline.schedule(9650) == 10456);
}

void test_equal_timestamps_remain_grouped() {
    UxReplayTimeline timeline;
    timeline.record(100, 180);
    assert(timeline.schedule(100) == 180);
}

void test_deadline_saturates_on_overflow() {
    UxReplayTimeline timeline;
    constexpr uint32_t limit = std::numeric_limits<uint32_t>::max();
    timeline.record(10, limit - 5);
    assert(timeline.schedule(20) == limit);
}

}  // namespace

int main() {
    test_first_action_uses_script_deadline();
    test_on_time_actions_keep_script_deadlines();
    test_late_action_preserves_next_interval();
    test_equal_timestamps_remain_grouped();
    test_deadline_saturates_on_overflow();

    std::cout << "UxReplayTimeline tests passed\n";
    return 0;
}

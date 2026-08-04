#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>

#include <oc/note/clock/ClockConstants.hpp>

#include "sequencer/ProjectControlClockPublisher.hpp"

namespace {

using core::sequencer::ProjectControlClockPublisher;

void test_clock_projects_sequencer_ticks_and_subtick_fraction() {
    ProjectControlClockPublisher clock{};
    constexpr uint32_t periodUs = 1000U;
    clock.publishLocked(10U, true, 10000U, periodUs);
    auto snapshot = clock.snapshot();
    const uint32_t projectTicksPerSequencerTick =
        core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT /
        oc::note::clock::PPQN;
    assert(snapshot.musicalTick == 10U * projectTicksPerSequencerTick);
    assert(snapshot.musicalTickFractionQ16 == 0U);
    assert(snapshot.playing);
    assert(snapshot.transportGeneration == 1U);

    clock.publishLocked(10U, true, 10500U, periodUs);
    snapshot = clock.snapshot();
    assert(snapshot.musicalTick ==
           10U * projectTicksPerSequencerTick +
               projectTicksPerSequencerTick / 2U);
    assert(snapshot.musicalTickFractionQ16 == 0U);
    assert(snapshot.transportGeneration == 1U);
}

void test_start_and_backward_resynchronization_advance_generation() {
    ProjectControlClockPublisher clock{};
    clock.publishLocked(20U, true, 1000U, 1000U);
    assert(clock.snapshot().transportGeneration == 1U);
    clock.publishLocked(21U, false, 2000U, 1000U);
    clock.publishLocked(21U, true, 2100U, 1000U);
    assert(clock.snapshot().transportGeneration == 2U);
    clock.publishLocked(2U, true, 3000U, 1000U);
    assert(clock.snapshot().transportGeneration == 3U);
}

void test_reset_clears_publication_state() {
    ProjectControlClockPublisher clock{};
    clock.publishLocked(4U, true, 1000U, 1000U);
    clock.reset();
    const auto snapshot = clock.snapshot();
    assert(snapshot.musicalTick == 0U);
    assert(snapshot.transportGeneration == 0U);
    assert(!snapshot.playing);
}

}  // namespace

int main() {
    test_clock_projects_sequencer_ticks_and_subtick_fraction();
    test_start_and_backward_resynchronization_advance_generation();
    test_reset_clears_publication_state();
    std::cout << "ProjectControlClockPublisher tests passed\n";
    return 0;
}

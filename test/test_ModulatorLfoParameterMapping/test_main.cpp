#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>

#include "state/modulation/ModulatorLfoParameterMapping.hpp"

namespace {

namespace lfo = core::state::modulation::lfo;

void test_rate_grid_is_monotone_and_round_trips() {
    uint32_t previous = 0U;
    for (uint8_t index = 0U; index < lfo::RATE_COUNT; ++index) {
        const uint32_t period = lfo::ratePeriodTicks(index);
        assert(period > previous);
        assert(lfo::rateIndex(period) == index);
        previous = period;
    }
}

void test_unknown_rate_falls_back_to_quarter_note() {
    assert(lfo::rateIndex(12345U) == 4U);
    assert(lfo::ratePeriodTicks(4U) ==
           core::state::modulation::PROJECT_CONTROL_TICKS_PER_BEAT);
}

void test_depth_mapping_clamps_and_round_trips_cardinal_values() {
    assert(lfo::depthPercentToQ15(-200) == -32767);
    assert(lfo::depthPercentToQ15(200) == 32767);
    assert(lfo::depthQ15ToPercent(-32767) == -100);
    assert(lfo::depthQ15ToPercent(32767) == 100);
    assert(lfo::depthQ15ToPercent(lfo::depthPercentToQ15(50)) == 50);
}

}  // namespace

int main() {
    test_rate_grid_is_monotone_and_round_trips();
    test_unknown_rate_falls_back_to_quarter_note();
    test_depth_mapping_clamps_and_round_trips_cardinal_values();
    std::cout << "ModulatorLfoParameterMapping tests passed.\n";
    return 0;
}

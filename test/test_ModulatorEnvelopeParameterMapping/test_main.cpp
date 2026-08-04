#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>

#include "state/modulation/ModulatorEnvelopeParameterMapping.hpp"

namespace {

namespace envelope = core::state::modulation::envelope;
namespace mod = core::state::modulation;

void test_free_duration_grid_is_monotone_and_reaches_authored_limits() {
    constexpr auto attack = mod::ModulatorEnvelopeTimeParameter::ATTACK;
    constexpr auto smooth = mod::ModulatorEnvelopeTimeParameter::SMOOTH;
    assert(envelope::durationCount(
        mod::ModulatorTimingMode::FREE,
        attack
    ) == 256U);
    assert(envelope::durationAt(
        0U,
        mod::ModulatorTimingMode::FREE,
        attack
    ) == 0U);
    assert(envelope::durationAt(
        255U,
        mod::ModulatorTimingMode::FREE,
        attack
    ) == 30000U);
    assert(envelope::durationAt(
        255U,
        mod::ModulatorTimingMode::FREE,
        smooth
    ) == 500U);

    uint16_t previousAttack = 0U;
    uint16_t previousSmooth = 0U;
    for (uint16_t index = 0U; index < 256U; ++index) {
        const uint16_t attackValue = envelope::durationAt(
            index,
            mod::ModulatorTimingMode::FREE,
            attack
        );
        const uint16_t smoothValue = envelope::durationAt(
            index,
            mod::ModulatorTimingMode::FREE,
            smooth
        );
        assert(attackValue >= previousAttack);
        assert(smoothValue >= previousSmooth);
        assert(envelope::durationIndex(
                   attackValue,
                   mod::ModulatorTimingMode::FREE,
                   attack
               ) <= 255U);
        previousAttack = attackValue;
        previousSmooth = smoothValue;
    }
}

void test_sync_grid_is_shared_and_smooth_stops_at_half_bar() {
    constexpr auto attack = mod::ModulatorEnvelopeTimeParameter::ATTACK;
    constexpr auto smooth = mod::ModulatorEnvelopeTimeParameter::SMOOTH;
    assert(envelope::durationCount(
        mod::ModulatorTimingMode::SYNC,
        attack
    ) == 12U);
    assert(envelope::durationCount(
        mod::ModulatorTimingMode::SYNC,
        smooth
    ) == 7U);
    assert(envelope::durationAt(
        0U,
        mod::ModulatorTimingMode::SYNC,
        attack
    ) == 0U);
    assert(envelope::durationAt(
        11U,
        mod::ModulatorTimingMode::SYNC,
        attack
    ) == 12288U);
    assert(envelope::durationAt(
        6U,
        mod::ModulatorTimingMode::SYNC,
        smooth
    ) == 384U);
    assert(envelope::durationIndex(
        12288U,
        mod::ModulatorTimingMode::SYNC,
        attack
    ) == 11U);
    assert(envelope::durationIndex(
        384U,
        mod::ModulatorTimingMode::SYNC,
        smooth
    ) == 6U);
}

void test_sustain_mapping_is_bounded_and_round_trips_cardinal_values() {
    assert(envelope::sustainPercentToQ15(0U) == 0U);
    assert(envelope::sustainPercentToQ15(100U) ==
           mod::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15);
    assert(envelope::sustainPercentToQ15(255U) ==
           mod::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15);
    assert(envelope::sustainQ15ToPercent(0U) == 0U);
    assert(envelope::sustainQ15ToPercent(
        mod::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15
    ) == 100U);
}

}  // namespace

int main() {
    test_free_duration_grid_is_monotone_and_reaches_authored_limits();
    test_sync_grid_is_shared_and_smooth_stops_at_half_bar();
    test_sustain_mapping_is_bounded_and_round_trips_cardinal_values();
    std::cout << "ModulatorEnvelopeParameterMapping tests passed.\n";
    return 0;
}

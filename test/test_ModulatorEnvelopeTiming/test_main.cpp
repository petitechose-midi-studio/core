#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "state/modulation/ModulatorEnvelopeTiming.hpp"

namespace {

namespace mod = core::state::modulation;

constexpr std::array<mod::ModulatorEnvelopeTimeParameter, 6U> PARAMETERS{
    mod::ModulatorEnvelopeTimeParameter::DELAY,
    mod::ModulatorEnvelopeTimeParameter::ATTACK,
    mod::ModulatorEnvelopeTimeParameter::HOLD,
    mod::ModulatorEnvelopeTimeParameter::DECAY,
    mod::ModulatorEnvelopeTimeParameter::RELEASE,
    mod::ModulatorEnvelopeTimeParameter::SMOOTH,
};

constexpr std::array<uint16_t, 12U> EXPECTED_SYNC_GRID{
    0U,
    12U,
    24U,
    48U,
    96U,
    192U,
    384U,
    768U,
    1536U,
    3072U,
    6144U,
    12288U,
};

static_assert(mod::MODULATOR_ENVELOPE_TICKS_PER_BEAT == 192U);
static_assert(mod::MODULATOR_ENVELOPE_SYNC_BASE_TICKS == EXPECTED_SYNC_GRID);
static_assert(mod::resolveModulatorEnvelopeSyncTicks(
                  1U,
                  mod::ModulatorEnvelopeFeel::TRIPLET
              ) == 1U);
static_assert(mod::resolveModulatorEnvelopeSyncTicks(
                  1U,
                  mod::ModulatorEnvelopeFeel::DOTTED
              ) == 2U);

bool expectedCanonicalSyncBase(uint16_t duration) {
    for (const uint16_t candidate : EXPECTED_SYNC_GRID) {
        if (candidate == duration) return true;
    }
    return false;
}

void test_parameter_getters_and_setters_cover_every_duration() {
    mod::ModulatorAdsrParameters parameters{};
    parameters.delay = 101U;
    parameters.attack = 102U;
    parameters.hold = 103U;
    parameters.decay = 104U;
    parameters.release = 105U;
    parameters.smooth = 106U;
    parameters.sustainQ15 = 23456U;
    parameters.traits = 0xA55AU;

    for (std::size_t index = 0U; index < PARAMETERS.size(); ++index) {
        assert(mod::isValidModulatorEnvelopeTimeParameter(PARAMETERS[index]));
        assert(mod::modulatorEnvelopeDuration(parameters, PARAMETERS[index]) ==
               static_cast<uint16_t>(101U + index));
        assert(mod::setModulatorEnvelopeDuration(
            parameters,
            PARAMETERS[index],
            static_cast<uint16_t>(201U + index)
        ));
    }

    assert(parameters.delay == 201U);
    assert(parameters.attack == 202U);
    assert(parameters.hold == 203U);
    assert(parameters.decay == 204U);
    assert(parameters.release == 205U);
    assert(parameters.smooth == 206U);
    assert(parameters.sustainQ15 == 23456U);
    assert(parameters.traits == 0xA55AU);

    const auto invalid = static_cast<mod::ModulatorEnvelopeTimeParameter>(255U);
    assert(!mod::isValidModulatorEnvelopeTimeParameter(invalid));
    assert(mod::modulatorEnvelopeDuration(parameters, invalid) == 0U);
    assert(!mod::setModulatorEnvelopeDuration(parameters, invalid, 999U));
    assert(parameters.delay == 201U);
    assert(parameters.smooth == 206U);
}

void test_free_authoring_bounds_are_parameter_specific() {
    for (const auto parameter : PARAMETERS) {
        const bool smooth =
            parameter == mod::ModulatorEnvelopeTimeParameter::SMOOTH;
        const uint16_t maximum = smooth ? 500U : 30000U;
        assert(mod::maximumModulatorEnvelopeFreeMilliseconds(parameter) ==
               maximum);
        assert(mod::isValidModulatorEnvelopeAuthoringDuration(
            parameter,
            mod::ModulatorTimingMode::FREE,
            0U
        ));
        assert(mod::isValidModulatorEnvelopeAuthoringDuration(
            parameter,
            mod::ModulatorTimingMode::FREE,
            maximum
        ));
        assert(!mod::isValidModulatorEnvelopeAuthoringDuration(
            parameter,
            mod::ModulatorTimingMode::FREE,
            static_cast<uint16_t>(maximum + 1U)
        ));
        assert(!mod::isValidModulatorEnvelopeAuthoringDuration(
            parameter,
            mod::ModulatorTimingMode::FREE,
            UINT16_MAX
        ));
    }
}

void test_fixed_point_free_grid_matches_the_logarithmic_contract() {
    constexpr auto attack = mod::ModulatorEnvelopeTimeParameter::ATTACK;
    constexpr auto smooth = mod::ModulatorEnvelopeTimeParameter::SMOOTH;
    static_assert(mod::modulatorEnvelopeFreeDurationAt(0U, attack) == 0U);
    static_assert(mod::modulatorEnvelopeFreeDurationAt(1U, attack) == 1U);
    static_assert(mod::modulatorEnvelopeFreeDurationAt(64U, attack) == 13U);
    static_assert(mod::modulatorEnvelopeFreeDurationAt(128U, attack) == 173U);
    static_assert(mod::modulatorEnvelopeFreeDurationAt(192U, attack) == 2326U);
    static_assert(mod::modulatorEnvelopeFreeDurationAt(255U, attack) == 30000U);
    static_assert(mod::modulatorEnvelopeFreeDurationAt(64U, smooth) == 5U);
    static_assert(mod::modulatorEnvelopeFreeDurationAt(128U, smooth) == 22U);
    static_assert(mod::modulatorEnvelopeFreeDurationAt(192U, smooth) == 107U);
    static_assert(mod::modulatorEnvelopeFreeDurationAt(255U, smooth) == 500U);

    for (const auto parameter : PARAMETERS) {
        uint16_t previous = 0U;
        for (uint16_t index = 0U;
             index < mod::MODULATOR_ENVELOPE_FREE_DURATION_STEP_COUNT;
             ++index) {
            const uint16_t value = mod::modulatorEnvelopeFreeDurationAt(
                index,
                parameter
            );
            assert(value >= previous);
            const uint16_t resolved = mod::modulatorEnvelopeFreeDurationIndex(
                value,
                parameter
            );
            assert(mod::modulatorEnvelopeFreeDurationAt(
                       resolved,
                       parameter
                   ) == value);
            previous = value;
        }
    }
}

void test_sync_authoring_grid_and_bounds_are_exhaustive() {
    for (const auto parameter : PARAMETERS) {
        const bool smooth =
            parameter == mod::ModulatorEnvelopeTimeParameter::SMOOTH;
        const uint16_t maximum = smooth ? 384U : 12288U;
        assert(mod::maximumModulatorEnvelopeSyncBaseTicks(parameter) == maximum);

        for (uint32_t candidate = 0U; candidate <= UINT16_MAX; ++candidate) {
            const auto duration = static_cast<uint16_t>(candidate);
            const bool expected = expectedCanonicalSyncBase(duration) &&
                                  duration <= maximum;
            assert(mod::isValidModulatorEnvelopeAuthoringDuration(
                       parameter,
                       mod::ModulatorTimingMode::SYNC,
                       duration
                   ) == expected);
        }
    }
}

void test_stored_validation_matches_current_authoring_contract() {
    constexpr std::array<mod::ModulatorTimingMode, 2U> MODES{
        mod::ModulatorTimingMode::FREE,
        mod::ModulatorTimingMode::SYNC,
    };
    for (const auto parameter : PARAMETERS) {
        for (const auto timing : MODES) {
            for (uint32_t candidate = 0U; candidate <= UINT16_MAX; ++candidate) {
                const auto duration = static_cast<uint16_t>(candidate);
                assert(mod::isValidModulatorEnvelopeStoredDuration(
                           parameter,
                           timing,
                           duration
                       ) == mod::isValidModulatorEnvelopeAuthoringDuration(
                           parameter,
                           timing,
                           duration
                       ));
            }
        }
    }

    mod::ModulatorAdsrParameters parameters{};
    assert(mod::setModulatorEnvelopeDuration(
        parameters,
        mod::ModulatorEnvelopeTimeParameter::ATTACK,
        UINT16_MAX
    ));
    assert(mod::modulatorEnvelopeDuration(
               parameters,
               mod::ModulatorEnvelopeTimeParameter::ATTACK
           ) == UINT16_MAX);
    assert(!mod::isValidModulatorEnvelopeAuthoringDuration(
        mod::ModulatorEnvelopeTimeParameter::ATTACK,
        mod::ModulatorTimingMode::FREE,
        UINT16_MAX
    ));
    assert(!mod::isValidModulatorEnvelopeStoredDuration(
        mod::ModulatorEnvelopeTimeParameter::ATTACK,
        mod::ModulatorTimingMode::FREE,
        UINT16_MAX
    ));
}

void test_all_feels_resolve_the_entire_grid() {
    assert(mod::isValidModulatorEnvelopeFeel(
        mod::ModulatorEnvelopeFeel::STRAIGHT
    ));
    assert(mod::isValidModulatorEnvelopeFeel(
        mod::ModulatorEnvelopeFeel::TRIPLET
    ));
    assert(mod::isValidModulatorEnvelopeFeel(
        mod::ModulatorEnvelopeFeel::DOTTED
    ));

    for (const uint16_t base : EXPECTED_SYNC_GRID) {
        assert(mod::resolveModulatorEnvelopeSyncTicks(
                   base,
                   mod::ModulatorEnvelopeFeel::STRAIGHT
               ) == static_cast<uint32_t>(base));
        assert(mod::resolveModulatorEnvelopeSyncTicks(
                   base,
                   mod::ModulatorEnvelopeFeel::TRIPLET
               ) == (2U * static_cast<uint32_t>(base)) / 3U);
        assert(mod::resolveModulatorEnvelopeSyncTicks(
                   base,
                   mod::ModulatorEnvelopeFeel::DOTTED
               ) == (3U * static_cast<uint32_t>(base)) / 2U);
    }

    for (uint32_t candidate = 0U; candidate <= UINT16_MAX; ++candidate) {
        const auto base = static_cast<uint16_t>(candidate);
        const uint32_t tripletNumerator = 2U * candidate;
        const uint32_t tripletExpected = tripletNumerator / 3U +
            (tripletNumerator % 3U >= 2U ? 1U : 0U);
        const uint32_t dottedNumerator = 3U * candidate;
        const uint32_t dottedExpected = dottedNumerator / 2U +
            (dottedNumerator % 2U);
        assert(mod::resolveModulatorEnvelopeSyncTicks(
                   base,
                   mod::ModulatorEnvelopeFeel::STRAIGHT
               ) == candidate);
        assert(mod::resolveModulatorEnvelopeSyncTicks(
                   base,
                   mod::ModulatorEnvelopeFeel::TRIPLET
               ) == tripletExpected);
        assert(mod::resolveModulatorEnvelopeSyncTicks(
                   base,
                   mod::ModulatorEnvelopeFeel::DOTTED
               ) == dottedExpected);
    }

    // Arbitrary inputs exercise the documented nearest/half-up math helper.
    assert(mod::resolveModulatorEnvelopeSyncTicks(
               2U,
               mod::ModulatorEnvelopeFeel::TRIPLET
           ) == 1U);
    assert(mod::resolveModulatorEnvelopeSyncTicks(
               4U,
               mod::ModulatorEnvelopeFeel::TRIPLET
           ) == 3U);
    assert(mod::resolveModulatorEnvelopeSyncTicks(
               3U,
               mod::ModulatorEnvelopeFeel::DOTTED
           ) == 5U);
    assert(mod::resolveModulatorEnvelopeSyncTicks(
               UINT16_MAX,
               mod::ModulatorEnvelopeFeel::TRIPLET
           ) == 43690U);
    assert(mod::resolveModulatorEnvelopeSyncTicks(
               UINT16_MAX,
               mod::ModulatorEnvelopeFeel::DOTTED
           ) == 98303U);

    const auto invalid = static_cast<mod::ModulatorEnvelopeFeel>(255U);
    assert(!mod::isValidModulatorEnvelopeFeel(invalid));
    assert(mod::resolveModulatorEnvelopeSyncTicks(123U, invalid) == 123U);
}

void test_invalid_tags_fail_both_validation_boundaries() {
    const auto invalidParameter =
        static_cast<mod::ModulatorEnvelopeTimeParameter>(255U);
    const auto invalidTiming = static_cast<mod::ModulatorTimingMode>(255U);
    assert(!mod::isValidModulatorEnvelopeTimingMode(invalidTiming));
    assert(!mod::isValidModulatorEnvelopeAuthoringDuration(
        invalidParameter,
        mod::ModulatorTimingMode::FREE,
        0U
    ));
    assert(!mod::isValidModulatorEnvelopeAuthoringDuration(
        mod::ModulatorEnvelopeTimeParameter::ATTACK,
        invalidTiming,
        0U
    ));
    assert(!mod::isValidModulatorEnvelopeStoredDuration(
        invalidParameter,
        mod::ModulatorTimingMode::SYNC,
        0U
    ));
    assert(!mod::isValidModulatorEnvelopeStoredDuration(
        mod::ModulatorEnvelopeTimeParameter::ATTACK,
        invalidTiming,
        0U
    ));
}

}  // namespace

int main() {
    test_parameter_getters_and_setters_cover_every_duration();
    test_free_authoring_bounds_are_parameter_specific();
    test_fixed_point_free_grid_matches_the_logarithmic_contract();
    test_sync_authoring_grid_and_bounds_are_exhaustive();
    test_stored_validation_matches_current_authoring_contract();
    test_all_feels_resolve_the_entire_grid();
    test_invalid_tags_fail_both_validation_boundaries();
    std::cout << "ModulatorEnvelopeTiming tests passed\n";
    return 0;
}

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "ui/modulation/ModulatorAdsrUiModel.hpp"

namespace {

namespace adsr = core::ui::modulation::adsr;
namespace mod = core::state::modulation;

void test_preview_uses_effective_feels_and_marks_note_off_without_fake_sustain() {
    mod::ModulatorAdsrParameters parameters{};
    parameters.delay = 12U;
    parameters.attack = 24U;
    parameters.hold = 48U;
    parameters.decay = 96U;
    parameters.release = 192U;
    parameters.sustainQ15 = 16384U;
    parameters.traits = mod::withModulatorAdsrTiming(
        parameters.traits,
        mod::ModulatorTimingMode::SYNC
    );
    parameters.traits = mod::withModulatorAdsrFeel(
        parameters.traits,
        mod::ModulatorEnvelopeTimeParameter::ATTACK,
        mod::ModulatorEnvelopeFeel::TRIPLET
    );
    parameters.traits = mod::withModulatorAdsrFeel(
        parameters.traits,
        mod::ModulatorEnvelopeTimeParameter::HOLD,
        mod::ModulatorEnvelopeFeel::DOTTED
    );

    // Effective D/A/H/D/R = 12/16/72/96/192 ticks.
    const auto boundaries = adsr::previewBoundaries(parameters);
    assert(boundaries.totalDuration == 388U);
    assert(boundaries.delayEndQ16 == (12U * 65535U) / 388U);
    assert(boundaries.attackEndQ16 == (28U * 65535U) / 388U);
    assert(boundaries.holdEndQ16 == (100U * 65535U) / 388U);
    assert(boundaries.decayEndQ16 == (196U * 65535U) / 388U);
    assert(boundaries.sustainEndQ16 == boundaries.decayEndQ16);

    assert(adsr::previewValue(parameters, boundaries, 0U) == 0.0f);
    assert(std::fabs(adsr::previewValue(
               parameters,
               boundaries,
               boundaries.attackEndQ16
           ) - 1.0f) < 0.0001f);
    assert(std::fabs(adsr::previewValue(
               parameters,
               boundaries,
               boundaries.sustainEndQ16
           ) - 0.5f) < 0.0001f);
    assert(adsr::previewValue(parameters, boundaries, 65535U) == 0.0f);

    uint16_t marker = 0U;
    assert(adsr::runtimeMarkerPosition(
        boundaries,
        mod::ProjectModulationAdsrStage::SUSTAIN,
        0U,
        marker
    ));
    assert(marker == boundaries.sustainEndQ16);
    assert(adsr::runtimeMarkerPosition(
        boundaries,
        mod::ProjectModulationAdsrStage::RELEASE,
        0U,
        marker
    ));
    assert(marker == boundaries.sustainEndQ16);
}

void test_zero_time_envelope_shows_held_sustain_without_synthetic_ramp() {
    mod::ModulatorAdsrParameters parameters{};
    parameters.delay = 0U;
    parameters.attack = 0U;
    parameters.hold = 0U;
    parameters.decay = 0U;
    parameters.release = 0U;
    parameters.sustainQ15 = 24576U;
    const auto boundaries = adsr::previewBoundaries(parameters);
    assert(boundaries.delayEndQ16 == 0U);
    assert(boundaries.attackEndQ16 == 0U);
    assert(boundaries.holdEndQ16 == 0U);
    assert(boundaries.decayEndQ16 == 0U);
    assert(boundaries.sustainEndQ16 == 65535U);
    assert(boundaries.totalDuration == 1U);
    assert(std::fabs(adsr::previewValue(parameters, boundaries, 32768U) -
                     0.75f) < 0.0001f);
}

}  // namespace

int main() {
    test_preview_uses_effective_feels_and_marks_note_off_without_fake_sustain();
    test_zero_time_envelope_shows_held_sustain_without_synthetic_ramp();
    std::cout << "ModulatorAdsrUiModel tests passed\n";
    return 0;
}

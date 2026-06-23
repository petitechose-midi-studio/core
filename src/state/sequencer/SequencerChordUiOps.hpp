#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerChord.hpp>

namespace core::state::sequencer {

struct SequencerContentStepProjection;
struct SequencerState;

struct SequencerChordVoicePreview {
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint16_t gate = 0;
    uint16_t delayTicks = 0;
    int16_t interval = 0;
    bool intervalUsesScaleDegrees = false;
};

struct SequencerChordPreview {
    static constexpr uint8_t MAX_VOICES =
        oc::note::sequencer::StepSequencerChordResolution::MAX_VOICES;

    bool valid = false;
    oc::note::sequencer::StepSequencerChordSource source =
        oc::note::sequencer::StepSequencerChordSource::Single;
    uint8_t voiceCount = 0;
    uint16_t spanTicks = 1;
    std::array<SequencerChordVoicePreview, MAX_VOICES> voices{};
    oc::note::sequencer::StepSequencerChordAnalysis analysis{};
};

struct SequencerStepChordUiState {
    bool valid = false;
    bool rootContext = true;
    oc::note::sequencer::StepSequencerChordMode mode =
        oc::note::sequencer::StepSequencerChordMode::Single;
    oc::note::sequencer::StepSequencerChordSpec spec{};
    uint8_t effectiveVoiceCount = 1;
    SequencerChordPreview preview{};
};

inline oc::note::sequencer::StepSequencerChordMode defaultChordModeForContentContext(
    bool rootContext
) {
    return rootContext
        ? oc::note::sequencer::StepSequencerChordMode::Single
        : oc::note::sequencer::StepSequencerChordMode::Inherit;
}

SequencerStepChordUiState resolveStepChordUiState(
    const SequencerState& sequencer,
    uint8_t step
);

void resolveStepChordPreview(
    SequencerStepChordUiState& chord,
    const SequencerContentStepProjection& projection,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);

}  // namespace core::state::sequencer

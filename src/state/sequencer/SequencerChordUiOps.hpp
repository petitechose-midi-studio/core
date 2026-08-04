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
    bool inSelectedScale = true;
};

struct SequencerChordPreview {
    static constexpr uint8_t MAX_VOICES =
        oc::note::sequencer::StepSequencerChordResolution::MAX_VOICES;

    bool valid = false;
    oc::note::sequencer::StepSequencerChordSource source =
        oc::note::sequencer::StepSequencerChordSource::Single;
    bool harmonyAdjustedForPitchMode = false;
    bool intervalBasisAdjusted = false;
    bool inversionClamped = false;
    bool rangeLimited = false;
    uint8_t rootNote = 0;
    uint8_t voiceCount = 0;
    uint8_t requestedVoiceCount = 1;
    uint8_t effectiveInversion = 0;
    uint8_t droppedVoiceCount = 0;
    oc::note::sequencer::StepSequencerChordIntervalBasis requestedIntervalBasis =
        oc::note::sequencer::StepSequencerChordIntervalBasis::FollowPitchContext;
    oc::note::sequencer::StepSequencerChordIntervalBasis intervalBasis =
        oc::note::sequencer::StepSequencerChordIntervalBasis::ChromaticSemitones;
    oc::note::sequencer::StepSequencerChordHarmony harmony =
        oc::note::sequencer::StepSequencerChordHarmony::DiatonicTriad;
    oc::note::sequencer::StepSequencerChordVoicing voicing =
        oc::note::sequencer::StepSequencerChordVoicing::Close;
    uint16_t spanTicks = 1;
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings{};
    std::array<SequencerChordVoicePreview, MAX_VOICES> voices{};
    oc::note::sequencer::StepSequencerChordAnalysis analysis{};
};

struct SequencerStepChordUiState {
    bool valid = false;
    bool rootContext = true;
    bool pitchFollowsScale = true;
    bool scaleAvailable = false;
    bool intervalsUseScaleDegrees = false;
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

#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

bool isRootContentView(const SequencerState& sequencer);
bool isMicroSequenceContentView(const SequencerState& sequencer);

bool enterMicroSequenceContentView(
    SequencerState& sequencer,
    uint8_t parentStep,
    SequencerGraphSequenceId sequenceId
);
bool leaveContentView(SequencerState& sequencer);
void refreshContentView(SequencerState& sequencer);

uint8_t activeContentLength(const SequencerState& sequencer);
uint8_t activeContentPageCount(const SequencerState& sequencer);
uint8_t normalizeActiveContentPage(const SequencerState& sequencer, uint8_t page);
uint8_t activeContentPageStartStep(const SequencerState& sequencer, uint8_t page);
uint8_t activeContentPageForStep(uint8_t step);
bool resolveActiveContentStepInPage(
    const SequencerState& sequencer,
    uint8_t page,
    uint8_t indexInPage,
    uint8_t& outStep
);
bool activeContentStepInPattern(const SequencerState& sequencer, uint8_t step);

bool toggleActiveContentStep(SequencerState& sequencer, uint8_t step);
bool setActiveContentStepFromNormalized(
    SequencerState& sequencer,
    uint8_t step,
    StepProperty property,
    float normalized,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
float activeContentStepPropertyToNormalized(
    const SequencerState& sequencer,
    uint8_t step,
    StepProperty property,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
bool resizeActiveMicroSequenceContent(SequencerState& sequencer, uint8_t length);

}  // namespace core::state::sequencer

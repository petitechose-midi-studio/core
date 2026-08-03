#pragma once

/**
 * @file SequencerState.hpp
 * @brief Sequencer state for Core UI and playback engine integration
 */

#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>

#include "SequencerPatternState.hpp"
#include "SequencerPatternEditorState.hpp"
#include "SequencerQuickControlsDraft.hpp"
#include "SequencerStepContentDraftSession.hpp"
#include "SequencerUiState.hpp"

namespace core::state::sequencer {

using oc::state::Signal;

struct SequencerState {
    static constexpr uint8_t STEPS_PER_PAGE = SequencerPatternState::STEPS_PER_PAGE;
    static constexpr uint8_t MAX_STEPS = SequencerPatternState::MAX_STEPS;
    static constexpr uint8_t PAGE_COUNT = SequencerPatternState::PAGE_COUNT;
    static constexpr uint16_t MAX_GATE_PERCENT = SequencerPatternState::MAX_GATE_PERCENT;
    static constexpr uint8_t DEFAULT_NOTE = SequencerPatternState::DEFAULT_NOTE;
    static constexpr uint8_t DEFAULT_VELOCITY = SequencerPatternState::DEFAULT_VELOCITY;
    static constexpr uint16_t DEFAULT_GATE_PERCENT = SequencerPatternState::DEFAULT_GATE_PERCENT;
    static constexpr uint8_t DEFAULT_PROBABILITY = SequencerPatternState::DEFAULT_PROBABILITY;

    SequencerPatternState pattern;

    /// Visible page index [0..PAGE_COUNT-1].
    /// May temporarily point beyond the current pattern length during paste target selection.
    Signal<uint8_t, 8> page{0};

    /// Absolute focused step index.
    /// May temporarily point beyond the current pattern length during paste target selection.
    Signal<uint8_t, 6> focusedStep{0};

    /// Bumps when runtime publishes or editor mutations invalidate resolved variation observations.
    Signal<uint32_t> variationTelemetryRevision{0};
    Signal<int16_t> playheadStep{-1};
    Signal<uint16_t> playheadStepTickOffset{0};
    uint16_t playheadStepTicks = 1;
    Signal<uint32_t> probabilityCycleRevision{0};
    oc::note::sequencer::StepBitMask128 probabilityCycleMask{};
    uint32_t probabilityCycleIndex = 0;
    oc::note::sequencer::StepSequencerResolvedVariation lastResolvedVariation{};
    oc::note::sequencer::StepSequencerCycleVariationTelemetry cycleVariationTelemetry{};
    oc::note::sequencer::StepSequencerExpandedVariationTelemetry expandedVariationTelemetry{};
    oc::note::sequencer::StepSequencerRuntimeDiagnostics runtimeDiagnostics{};

    /// Active property edited by the 8 macro encoders in Sequencer view
    Signal<StepProperty, 6> activeStepProperty{StepProperty::NOTE};
    /// State is a direct Step property without polluting the musical-value enum.
    Signal<bool, 6> stepStatePropertyActive{false};

    // UI state
    SequencerStepEditOverlayState stepEdit;
    SequencerContextSelectorState contextSelector;
    SequencerPresetLibrarySessionState presetLibrary;
    SequencerCcLaneUiState ccLaneUi;
    SequencerStepPropertyInlineSelectorState stepPropertyInlineSelector;
    SequencerStepContentSelectorState stepContentSelector;
    SequencerStepInlineFeedbackState stepInlineFeedback;
    SequencerPatternVariationFeedbackState patternVariationFeedback;
    SequencerHistoryFeedbackState historyFeedback;
    SequencerPatternQuickControlsState patternQuickControls;
    SequencerPatternEditorState patternEditor;
    SequencerContentViewState contentView;
    // One lazy detached Pattern used only while Quick Controls is held.
    SequencerQuickControlsDraftSession quickControlsDraft;
    // One cold PSRAM scratch shared by Chord/Micro/Cycle creation sessions.
    // Published Pattern data remains untouched until explicit Apply/Save.
    SequencerStepContentDraftSession stepContentDraft;
    SequencerStructureUiState structureUi;

    SequencerState();
    ~SequencerState();

    static uint8_t clampMidi7(uint8_t value) {
        return SequencerPatternState::clampMidi7(value);
    }

    static uint16_t clampGatePercent(uint16_t value) {
        return SequencerPatternState::clampGatePercent(value);
    }

    static int8_t clampNudge(int value) {
        return SequencerPatternState::clampNudge(value);
    }

    static uint8_t clampProbability(uint8_t value) {
        return SequencerPatternState::clampProbability(value);
    }

    static int8_t clampPatternSwingOffsetPercent(int value) {
        return SequencerPatternState::clampPatternSwingOffsetPercent(value);
    }

    static int8_t clampPatternNudgePercent(int value) {
        return SequencerPatternState::clampPatternNudgePercent(value);
    }

    void invalidateVariationTelemetry();
    void invalidateStepVariationTelemetry(uint8_t step);

    uint8_t variationRangeForProperty(StepProperty property) const {
        return pattern.variationRangeForProperty(property);
    }

    bool setVariationRangeForProperty(StepProperty property, uint8_t range);
    bool setPatternVariationRanges(oc::note::sequencer::StepSequencerVariationRanges ranges);
    bool setPatternScalePolicy(SequencerPatternScalePolicy policy);
    bool setPatternScaleOverride(oc::note::sequencer::StepSequencerScaleSettings settings);
    bool setPitchEditMode(SequencerPitchEditMode mode);
    bool setPatternSwingOffsetPercent(int value);
    bool setPatternNudgePercent(int value);

    bool setStepNoteAt(uint8_t step, uint8_t noteValue);

    bool setStepVelocityAt(uint8_t step, uint8_t velocityValue);

    bool setStepGateAt(uint8_t step, uint16_t gatePercent);

    bool setStepNudgeAt(uint8_t step, int8_t nudgeValue);

    bool setStepProbabilityAt(uint8_t step, uint8_t probabilityValue);

    bool setStepDataAt(
        uint8_t step,
        uint8_t noteValue,
        uint8_t velocityValue,
        uint16_t gatePercent
    );

    bool setStepDataAt(
        uint8_t step,
        uint8_t noteValue,
        uint8_t velocityValue,
        uint16_t gatePercent,
        int8_t nudgeValue
    );

    bool setStepDataAt(
        uint8_t step,
        uint8_t noteValue,
        uint8_t velocityValue,
        uint16_t gatePercent,
        int8_t nudgeValue,
        uint8_t probabilityValue
    );

    void reset();

    void updateUi(uint32_t nowMs) {
        stepInlineFeedback.update(nowMs);
        patternVariationFeedback.update(nowMs);
        patternQuickControls.update(nowMs);
        historyFeedback.update(nowMs);
    }

    uint8_t activePageCount() const {
        return pattern.activePageCount();
    }

    uint8_t normalizePage(uint8_t page) const {
        return pattern.normalizePage(page);
    }

    uint8_t clampPage(uint8_t page) const {
        return pattern.clampPage(page);
    }

    uint8_t visiblePage() const {
        return normalizePage(page.get());
    }

    uint8_t pageStartStep(uint8_t page) const {
        return pattern.pageStartStep(page);
    }

    uint8_t pageStartStepClamped(uint8_t page) const {
        return pattern.pageStartStepClamped(page);
    }

    uint8_t pageForStep(uint8_t step) const {
        return pattern.pageForStep(step);
    }

    bool resolveStepInPage(uint8_t page, uint8_t indexInPage, uint8_t& outStep) const {
        return pattern.resolveStepInPage(page, indexInPage, outStep);
    }

    bool isInPattern(uint8_t step) const {
        return pattern.isInPattern(step);
    }
};

}  // namespace core::state::sequencer

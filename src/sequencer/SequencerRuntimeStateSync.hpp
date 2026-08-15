#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerPlaybackRegion.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::sequencer {

struct ProjectTimingContext {
    uint8_t swingPercent = 0;
};

/**
 * Lightweight signature for deciding whether runtime state must be resynced.
 *
 * Runtime playback should copy editor/snapshot data only when this signature
 * changes, so the hot path can avoid rebuilding track engines on every tick.
 */
struct SequencerRuntimeStateSignature {
    uint8_t length = 0;
    uint8_t playStart = 0;
    uint8_t loopStart = 0;
    uint8_t loopEnd = 0;
    uint8_t stepsPerBeat = 0;
    oc::note::sequencer::StepBitMask128 enabledMask{};
    uint32_t stepDataRevision = 0;
    uint32_t patternVariationRevision = 0;
    uint32_t patternScaleRevision = 0;
    uint32_t patternTimingRevision = 0;
    uint32_t graphRevision = 0;
    uint8_t effectiveSwingPercent = 0;
    int8_t patternNudgePercent = 0;
    bool pitchFollowsScale = true;
    oc::note::sequencer::StepSequencerScaleSettings effectiveScaleSettings{};

    bool matches(const SequencerRuntimeStateSignature& other) const {
        return length == other.length &&
               playStart == other.playStart &&
               loopStart == other.loopStart &&
               loopEnd == other.loopEnd &&
               stepsPerBeat == other.stepsPerBeat &&
               enabledMask == other.enabledMask &&
               stepDataRevision == other.stepDataRevision &&
               patternVariationRevision == other.patternVariationRevision &&
               patternScaleRevision == other.patternScaleRevision &&
               patternTimingRevision == other.patternTimingRevision &&
               graphRevision == other.graphRevision &&
               effectiveSwingPercent == other.effectiveSwingPercent &&
               patternNudgePercent == other.patternNudgePercent &&
               pitchFollowsScale == other.pitchFollowsScale &&
               effectiveScaleSettings.root == other.effectiveScaleSettings.root &&
               effectiveScaleSettings.type == other.effectiveScaleSettings.type &&
               effectiveScaleSettings.mode == other.effectiveScaleSettings.mode;
    }
};

/**
 * Runtime-only telemetry projected back into sequencer state for the UI.
 *
 * These values are observations of playback, not editable pattern data. Keep
 * them separate from snapshot mutation helpers to avoid making the UI state a
 * second source of musical truth.
 */
struct SequencerRuntimeTelemetrySnapshot {
    int16_t playheadStep = -1;
    uint16_t playheadStepTickOffset = 0;
    uint16_t playheadStepTicks = 1;
    // UI-only normalized phase inside the current root step. The musical
    // scheduler remains authoritative through the integer tick fields above.
    uint8_t playheadStepPhaseQ8 = 0;
    uint32_t probabilityCycleIndex = 0;
    oc::note::sequencer::StepBitMask128 probabilityCycleMask{};
    uint32_t variationTelemetryRevision = 0;
    oc::note::sequencer::StepSequencerResolvedVariation lastResolvedVariation{};
    oc::note::sequencer::StepSequencerCycleVariationTelemetry cycleVariationTelemetry{};
    oc::note::sequencer::StepSequencerExpandedVariationTelemetry expandedVariationTelemetry{};
    oc::note::sequencer::StepSequencerRuntimeDiagnostics runtimeDiagnostics{};
};

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerState& source,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings,
    ProjectTimingContext projectTiming
);

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerPatternState& source,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings,
    ProjectTimingContext projectTiming
);

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerPatternSnapshot& source
);

oc::note::sequencer::StepSequencerPlaybackRegion runtimePlaybackRegion(
    const core::state::sequencer::SequencerPatternSnapshot& source
);

void syncRuntimeState(oc::note::sequencer::StepSequencerRuntimeState& target,
                      const core::state::sequencer::SequencerPatternSnapshot& source);

SequencerRuntimeTelemetrySnapshot captureRuntimeTelemetry(
    const oc::note::sequencer::StepSequencerRuntimeState& runtimeState
);

/**
 * Projects a stable 0..255 visual phase from integer transport telemetry.
 *
 * Passing a zero tick period disables sub-tick extrapolation, which keeps the
 * helper deterministic for tests and non-realtime callers.
 */
uint8_t projectPlaybackPhaseQ8(uint16_t tickOffset,
                               uint16_t ticksPerStep,
                               uint32_t tickAnchorUs,
                               uint32_t tickPeriodUs,
                               uint32_t nowUs);

void publishRuntimeTelemetry(core::state::sequencer::SequencerState& target,
                             const SequencerRuntimeTelemetrySnapshot& telemetry);

void publishRuntimeTelemetry(core::state::sequencer::SequencerState& target,
                             const oc::note::sequencer::StepSequencerRuntimeState& runtimeState);

}  // namespace core::sequencer

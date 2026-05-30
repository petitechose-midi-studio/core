#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::sequencer {

/**
 * Lightweight signature for deciding whether runtime state must be resynced.
 *
 * Runtime playback should copy editor/snapshot data only when this signature
 * changes, so the hot path can avoid rebuilding track engines on every tick.
 */
struct SequencerRuntimeStateSignature {
    uint8_t length = 0;
    uint8_t stepsPerBeat = 0;
    uint8_t midiChannel = 0;
    oc::note::sequencer::StepBitMask128 enabledMask{};
    uint32_t stepDataRevision = 0;
    uint32_t patternVariationRevision = 0;
    uint32_t patternScaleRevision = 0;
    uint32_t graphRevision = 0;
    oc::note::sequencer::StepSequencerScaleSettings effectiveScaleSettings{};

    bool matches(const SequencerRuntimeStateSignature& other) const {
        return length == other.length &&
               stepsPerBeat == other.stepsPerBeat &&
               midiChannel == other.midiChannel &&
               enabledMask == other.enabledMask &&
               stepDataRevision == other.stepDataRevision &&
               patternVariationRevision == other.patternVariationRevision &&
               patternScaleRevision == other.patternScaleRevision &&
               graphRevision == other.graphRevision &&
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
    uint32_t probabilityCycleIndex = 0;
    oc::note::sequencer::StepBitMask128 probabilityCycleMask{};
    uint32_t variationTelemetryRevision = 0;
    oc::note::sequencer::StepSequencerResolvedVariation lastResolvedVariation{};
    oc::note::sequencer::StepSequencerCycleVariationTelemetry cycleVariationTelemetry{};
};

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerState& source,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings
);

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerPatternState& source,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings
);

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerPatternSnapshot& source
);

void syncRuntimeState(oc::note::sequencer::StepSequencerRuntimeState& target,
                      const core::state::sequencer::SequencerPatternSnapshot& source);

SequencerRuntimeTelemetrySnapshot captureRuntimeTelemetry(
    const oc::note::sequencer::StepSequencerRuntimeState& runtimeState
);

void publishRuntimeTelemetry(core::state::sequencer::SequencerState& target,
                             const SequencerRuntimeTelemetrySnapshot& telemetry);

void publishRuntimeTelemetry(core::state::sequencer::SequencerState& target,
                             const oc::note::sequencer::StepSequencerRuntimeState& runtimeState);

}  // namespace core::sequencer

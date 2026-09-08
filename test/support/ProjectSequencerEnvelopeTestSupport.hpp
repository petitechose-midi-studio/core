#pragma once

#include "../../src/persistence/SequencerPersistenceEnvelope.hpp"
#include "../../src/state/sequencer/SequencerHistory.hpp"

namespace test_support {

inline core::persistence::sequencer_codec::EnvelopeEncodeResult
encodeProjectSequencerSnapshot(
    const core::state::sequencer::SequencerHistoryTrackBankSnapshot& snapshot,
    uint8_t* out,
    uint32_t capacity
) {
    namespace codec = core::persistence::sequencer_codec;
    using TrackBank = core::state::sequencer::SequencerTrackBankState;

    codec::ProjectSequencerSnapshotEncodeSource source{};
    core::state::sequencer::SequencerClipGridSnapshot clips{};
    for (uint8_t track = 0U; track < TrackBank::TRACK_COUNT; ++track) {
        if ((snapshot.flat.enabledMask & static_cast<uint16_t>(1U << track)) != 0U) {
            clips.residentSlots[track] = 0U;
        }
    }
    source.flat = &snapshot.flat;
    source.clips = &clips;
    source.focusedStep = snapshot.focusedStep;
    source.activeStepProperty = snapshot.activeStepProperty;
    const uint8_t activeTrack = TrackBank::sanitizeActiveTrack(
        snapshot.flat.enabledMask,
        snapshot.flat.activeTrack
    );
    for (uint8_t i = 0; i < codec::PERSISTED_TRACK_COUNT; ++i) {
        source.graphs[i] = snapshot.bankGraphs[i].get();
        source.ccLanes[i] = snapshot.bankCcLanes[i].get();
    }
    return codec::fillProjectSequencerEnvelope(source, out, capacity);
}

inline core::persistence::sequencer_codec::EnvelopeEncodeResult
encodeProjectSequencerSnapshot(
    const core::state::sequencer::SequencerHistoryTrackBankSnapshot& snapshot,
    core::persistence::sequencer_codec::EnvelopeBuffer& out
) {
    return encodeProjectSequencerSnapshot(
        snapshot,
        out.bytes.data(),
        static_cast<uint32_t>(out.bytes.size())
    );
}

}  // namespace test_support

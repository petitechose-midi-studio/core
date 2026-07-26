#pragma once

#include <cstdint>

#include "sequencer/ProjectTrackRuntimeSnapshotBank.hpp"

namespace test_support {

/**
 * Explicit test-only Project Track authority used by isolated MIDI-CC tests.
 *
 * Product code has no implicit routing/mix fallback: tests which do not model
 * Track policy opt into this deterministic all-audible, zero-delay fixture.
 */
inline core::sequencer::ProjectTrackRuntimeSnapshot
makeAllAudibleProjectTrackRuntimeSnapshot() {
    core::sequencer::ProjectTrackRuntimeSnapshot result{};
    result.enabledMask = UINT16_MAX;
    result.audibleMask = UINT16_MAX;
    for (uint8_t track = 0U; track < result.midiChannels.size(); ++track) {
        result.midiChannels[track] = track;
    }
    return result;
}

}  // namespace test_support

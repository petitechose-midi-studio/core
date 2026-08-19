#pragma once

#include <cstdint>

#include "persistence/SequencerPatternPresetCodec.hpp"

namespace core::persistence {

struct PatternPresetFactoryDescriptor {
    const char* id = nullptr;
    const char* semanticName = nullptr;
    core::state::sequencer::SequencerTrackKind trackKind =
        core::state::sequencer::SequencerTrackKind::INSTRUMENT;
};

/** Small immutable V1 factory pack authored in Flash. */
class PatternPresetFactoryLibrary {
public:
    static uint8_t count(
        core::state::sequencer::SequencerTrackKind trackKind
    );
    static bool descriptorAt(
        core::state::sequencer::SequencerTrackKind trackKind,
        uint8_t index,
        PatternPresetFactoryDescriptor& out
    );
    static bool describe(
        const char* presetId,
        PatternPresetFactoryDescriptor& out
    );
    static bool contains(const char* presetId);

    static sequencer_pattern_preset_codec::EncodeResult encode(
        const char* presetId,
        core::state::sequencer::SequencerPatternState& patternScratch,
        core::state::sequencer::DrumTrackState* drumScratch,
        core::state::sequencer::SequencerPatternPresetMetadata& metadataOut,
        uint8_t* out,
        uint16_t capacity
    );
};

}  // namespace core::persistence

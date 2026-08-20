#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

#include "persistence/SequencerPatternPresetCodec.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerPatternRegionOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace {

namespace codec = core::persistence::sequencer_pattern_preset_codec;
namespace seq = core::state::sequencer;

void authorCcLane(seq::SequencerPatternState& pattern) {
    auto* bank = seq::ensureSequencerCcLaneBank(pattern);
    assert(bank != nullptr);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = 74U;
    draft.initialValue = 48U;
    assert(seq::createSequencerCcLane(*bank, 0U, draft).changed());
    assert(seq::setSequencerCcLaneEvent(*bank, 0U, 7U, 96U).changed());
    pattern.bumpCcLaneRevision();
}

void assertSameDrumRhythm(
    const seq::DrumTrackState& lhs,
    const seq::DrumTrackState& rhs
) {
    assert(lhs.pattern.defaultLength == rhs.pattern.defaultLength);
    assert(lhs.pattern.defaultStepsPerBeat == rhs.pattern.defaultStepsPerBeat);
    for (uint8_t lane = 0U; lane < seq::DRUM_MAX_LANES; ++lane) {
        assert(seq::sameDrumLanePattern(
            lhs.pattern.lanes[lane],
            rhs.pattern.lanes[lane]
        ));
    }
    assert(lhs.advancedStepKeys == rhs.advancedStepKeys);
}

void testInstrumentRoundTrip() {
    seq::SequencerState source{};
    source.reset();
    seq::SequencerPatternPresetMetadata sourceMetadata{};
    assert(seq::setSequencerPatternPresetMetadata(
        sourceMetadata,
        seq::SequencerTrackKind::INSTRUMENT,
        "instrument-pattern-001",
        "Chromatic pulse"
    ));
    assert(source.pattern.setContentLength(32U));
    assert(seq::setPatternPlaybackRegion(source.pattern, {32U, 2U, 5U, 27U}));
    auto enabled = source.pattern.enabledMask.get();
    enabled.setBit(3U, true);
    source.pattern.enabledMask.set(enabled);
    assert(source.pattern.setStepDataAt(3U, 72U, 111U, 240U, -12, 67U));
    assert(seq::setNodeNoteOffset(
        source.pattern,
        seq::rootStepNodeId(3U),
        7
    ));
    authorCcLane(source.pattern);

    std::array<uint8_t, codec::MAX_ENCODED_SIZE> bytes{};
    const auto encoded = codec::encode(
        sourceMetadata,
        source.pattern,
        nullptr,
        bytes.data(),
        static_cast<uint16_t>(bytes.size())
    );
    assert(encoded.ok());
    assert(encoded.bytesWritten > codec::HEADER_SIZE);

    codec::MetadataView metadata{};
    assert(codec::decodeMetadata(
        bytes.data(),
        codec::HEADER_SIZE,
        metadata
    ));
    assert(metadata.metadata.trackKind == seq::SequencerTrackKind::INSTRUMENT);
    assert(std::strcmp(metadata.metadata.semanticName, "Chromatic pulse") == 0);
    assert(metadata.drumRecordSize == 0U);

    seq::SequencerState decoded{};
    decoded.reset();
    seq::SequencerPatternPresetMetadata decodedMetadata{};
    assert(codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decodedMetadata,
        decoded.pattern,
        nullptr
    ));
    assert(std::strcmp(decodedMetadata.technicalId, "instrument-pattern-001") == 0);
    assert(seq::sameMusicalPatternState(source.pattern, decoded.pattern));

    std::cout << "[PASS] instrument Pattern Preset round-trip: "
              << encoded.bytesWritten << " bytes\n";
}

void testDrumRoundTripAndKitCompatibility() {
    seq::SequencerState source{};
    source.reset();
    seq::SequencerPatternPresetMetadata sourceMetadata{};
    assert(seq::setSequencerPatternPresetMetadata(
        sourceMetadata,
        seq::SequencerTrackKind::DRUM,
        "drum-pattern-001",
        "Broken beat"
    ));
    seq::DrumTrackState sourceDrum{};
    sourceDrum.reset(seq::DrumKitPreset::GENERAL_MIDI);
    assert(sourceDrum.pattern.setDefaults(16U, 4U));
    assert(sourceDrum.pattern.setStepEnabled(0U, 0U, true));
    assert(sourceDrum.pattern.setStepVelocity(0U, 0U, 118U));
    assert(sourceDrum.pattern.setLaneTimingCustom(1U, 7U, 4U));
    assert(sourceDrum.pattern.setStepEnabled(1U, 3U, true));
    assert(sourceDrum.bindAdvancedRootSlot(0U, 0U, 0U));
    assert(seq::setNodeVelocityOffset(
        source.pattern,
        seq::rootStepNodeId(0U),
        9
    ));

    std::array<uint8_t, codec::MAX_ENCODED_SIZE> bytes{};
    const auto encoded = codec::encode(
        sourceMetadata,
        source.pattern,
        &sourceDrum,
        bytes.data(),
        static_cast<uint16_t>(bytes.size())
    );
    assert(encoded.ok());

    seq::SequencerState decoded{};
    decoded.reset();
    seq::DrumTrackState decodedDrum{};
    seq::SequencerPatternPresetMetadata decodedMetadata{};
    assert(codec::decode(
        bytes.data(),
        encoded.bytesWritten,
        decodedMetadata,
        decoded.pattern,
        &decodedDrum
    ));
    assert(decodedMetadata.trackKind == seq::SequencerTrackKind::DRUM);
    assert(seq::sameMusicalPatternState(source.pattern, decoded.pattern));
    assertSameDrumRhythm(sourceDrum, decodedDrum);

    auto destination = sourceDrum;
    destination.kit.lanes[0].midiNote = 60U;
    destination.kit.lanes[0] =
        seq::canonicalDrumLaneDescriptor(destination.kit.lanes[0]);
    assert(seq::sequencerPatternPresetDrumKitCompatible(
        sourceDrum,
        destination
    ));
    destination.kit.laneCount--;
    assert(!seq::sequencerPatternPresetDrumKitCompatible(
        sourceDrum,
        destination
    ));

    std::cout << "[PASS] Drum Pattern Preset preserves rhythm, not target kit: "
              << encoded.bytesWritten << " bytes\n";
}

void testStrictEnvelopeAndIntegrity() {
    seq::SequencerState source{};
    source.reset();
    seq::SequencerPatternPresetMetadata sourceMetadata{};
    assert(seq::setSequencerPatternPresetMetadata(
        sourceMetadata,
        seq::SequencerTrackKind::INSTRUMENT,
        "strict-pattern",
        "Strict pattern"
    ));

    std::array<uint8_t, codec::MAX_ENCODED_SIZE> bytes{};
    const auto encoded = codec::encode(
        sourceMetadata,
        source.pattern,
        nullptr,
        bytes.data(),
        static_cast<uint16_t>(bytes.size())
    );
    assert(encoded.ok());

    seq::SequencerState decoded{};
    seq::SequencerPatternPresetMetadata decodedMetadata{};
    seq::SequencerPatternPresetStatus status{};
    auto corrupt = bytes;
    corrupt[encoded.bytesWritten - 1U] ^= 0x01U;
    assert(!codec::decode(
        corrupt.data(),
        encoded.bytesWritten,
        decodedMetadata,
        decoded.pattern,
        nullptr,
        &status
    ));
    assert(status == seq::SequencerPatternPresetStatus::INVALID_FORMAT);

    corrupt = bytes;
    corrupt[codec::SEMANTIC_NAME_OFFSET] ^= 0x01U;
    assert(!codec::decode(
        corrupt.data(),
        encoded.bytesWritten,
        decodedMetadata,
        decoded.pattern,
        nullptr,
        &status
    ));
    assert(status == seq::SequencerPatternPresetStatus::INVALID_FORMAT);

    corrupt = bytes;
    corrupt[4] = 0U;
    assert(!codec::decode(
        corrupt.data(),
        encoded.bytesWritten,
        decodedMetadata,
        decoded.pattern,
        nullptr,
        &status
    ));
    assert(status == seq::SequencerPatternPresetStatus::UNSUPPORTED_VERSION);

    assert(!codec::encode(
        sourceMetadata,
        source.pattern,
        nullptr,
        bytes.data(),
        static_cast<uint16_t>(codec::HEADER_SIZE - 1U)
    ).ok());

    sourceMetadata.trackKind = seq::SequencerTrackKind::DRUM;
    seq::DrumTrackState sourceDrum{};
    sourceDrum.reset(seq::DrumKitPreset::GENERAL_MIDI);
    authorCcLane(source.pattern);
    assert(!codec::encode(
        sourceMetadata,
        source.pattern,
        &sourceDrum,
        bytes.data(),
        static_cast<uint16_t>(bytes.size())
    ).ok());

    std::cout << "[PASS] Pattern Preset envelope is strict and checksummed\n";
}

}  // namespace

int main() {
    testInstrumentRoundTrip();
    testDrumRoundTripAndKitCompatibility();
    testStrictEnvelopeAndIntegrity();
    std::cout << "[PASS] SequencerPatternPresetCodec tests\n";
    return 0;
}

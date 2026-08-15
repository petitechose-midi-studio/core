#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>

#include "persistence/SequencerCcLanePersistenceCodec.hpp"
#include "persistence/SequencerPersistenceEnvelope.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerPatternRegionOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace {

namespace codec = core::persistence::sequencer_codec;
namespace seq = core::state::sequencer;

void authorTwoLanes(seq::SequencerPatternState& pattern) {
    auto* bank = seq::ensureSequencerCcLaneBank(pattern);
    assert(bank != nullptr);

    seq::SequencerCcLaneDraft inherited{};
    inherited.destination.controller = 74U;
    inherited.destination.minimum = 10U;
    inherited.destination.maximum = 110U;
    inherited.destination.routePolicy =
        seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    inherited.initialValue = 64U;
    assert(seq::createSequencerCcLane(*bank, 0U, inherited).changed());
    assert(seq::setSequencerCcLaneEvent(*bank, 0U, 0U, 64U).changed());
    assert(seq::setSequencerCcLaneEvent(*bank, 0U, 127U, 110U).changed());
    assert(seq::setSequencerCcLaneTransition(
        *bank,
        0U,
        0U,
        seq::SequencerCcLaneTransition::LINEAR
    ).changed());
    assert(seq::setSequencerCcLaneTransition(
        *bank,
        0U,
        127U,
        seq::SequencerCcLaneTransition::EASE_IN_OUT
    ).changed());

    seq::SequencerCcLaneDraft pinned{};
    pinned.destination.controller = 1U;
    pinned.destination.routePolicy = seq::SequencerCcLaneRoutePolicy::PINNED;
    pinned.destination.pinnedPort = 2U;
    pinned.destination.pinnedChannel = 7U;
    pinned.initialValue = 20U;
    pinned.acceptedMacroConflict = true;
    assert(seq::createSequencerCcLane(*bank, 1U, pinned).changed());
    assert(seq::setSequencerCcLaneEvent(*bank, 1U, 3U, 99U).changed());
    assert(seq::setSequencerCcLaneTransition(
        *bank,
        1U,
        3U,
        seq::SequencerCcLaneTransition::EASE_OUT
    ).changed());
    pattern.bumpCcLaneRevision();
}

void assertTwoLanes(const seq::SequencerPatternState& pattern) {
    const auto* bank = seq::sequencerCcLaneView(pattern);
    assert(bank != nullptr);
    assert(seq::sequencerCcLaneCount(*bank) == 2U);
    assert(bank->lanes[0].destination.controller == 74U);
    assert(bank->lanes[0].values[0] == 64U);
    assert(bank->lanes[0].values[127] == 110U);
    assert(seq::sequencerCcLaneTransition(bank->lanes[0], 0U) ==
           seq::SequencerCcLaneTransition::LINEAR);
    assert(seq::sequencerCcLaneTransition(bank->lanes[0], 127U) ==
           seq::SequencerCcLaneTransition::EASE_IN_OUT);
    assert(bank->lanes[1].destination.pinnedPort == 2U);
    assert(bank->lanes[1].destination.pinnedChannel == 7U);
    assert(bank->lanes[1].acceptedMacroConflict);
    assert(bank->lanes[1].values[3] == 99U);
    assert(seq::sequencerCcLaneTransition(bank->lanes[1], 3U) ==
           seq::SequencerCcLaneTransition::EASE_OUT);
}

void testCurrentRecordRoundTripAndStrictVersioning() {
    seq::SequencerPatternState pattern{};
    authorTwoLanes(pattern);
    const auto source = *seq::sequencerCcLaneView(pattern);

    std::array<uint8_t, codec::SEQUENCER_CC_LANE_BANK_RECORD_SIZE> bytes{};
    assert(codec::encodeSequencerCcLaneBankRecord(
        source,
        bytes.data(),
        static_cast<uint16_t>(bytes.size())
    ));

    seq::SequencerCcLaneBank decoded{};
    assert(codec::decodeSequencerCcLaneBankRecord(
        bytes.data(),
        static_cast<uint16_t>(bytes.size()),
        decoded
    ));
    assert(seq::sameSequencerCcLaneBankMusicalData(source, decoded));

    const auto sentinel = decoded;
    const uint8_t currentVersion = bytes[0];
    bytes[0] = static_cast<uint8_t>(currentVersion - 1U);
    assert(!codec::decodeSequencerCcLaneBankRecord(
        bytes.data(),
        static_cast<uint16_t>(bytes.size()),
        decoded
    ));
    assert(seq::sameSequencerCcLaneBankMusicalData(sentinel, decoded));
    bytes[0] = currentVersion;
    assert(!codec::decodeSequencerCcLaneBankRecord(
        bytes.data(),
        static_cast<uint16_t>(bytes.size() - 1U),
        decoded
    ));
    assert(seq::sameSequencerCcLaneBankMusicalData(sentinel, decoded));

    std::cout << "[PASS] current CC lane record is strict and atomic\n";
}

void testPatternEnvelopeRoundTripAndStrictVersioning() {
    seq::SequencerState source{};
    source.reset();
    source.pattern.setContentLength(32U);
    assert(seq::setPatternPlaybackRegion(source.pattern, {32U, 2U, 5U, 27U}));
    authorTwoLanes(source.pattern);
    auto chord = oc::note::sequencer::StepSequencerChordSpec::semantic(
        oc::note::sequencer::StepSequencerChordHarmony::Custom,
        8U,
        oc::note::sequencer::StepSequencerChordVoicing::Open,
        1U,
        oc::note::sequencer::StepSequencerChordIntervalBasis::ChromaticSemitones
    );
    constexpr std::array<uint8_t, 8> intervals{
        0U, 3U, 5U, 8U, 12U, 17U, 24U, 31U,
    };
    for (uint8_t voice = 7U; voice > 0U; --voice) {
        chord.setCustomInterval(voice, intervals[voice]);
    }
    assert(seq::setNodeChordSpec(
        source.pattern,
        seq::rootStepNodeId(0U),
        chord
    ));

    codec::PatternEnvelopeBuffer bytes{};
    const auto encoded = codec::fillPatternEnvelope(
        source.pattern,
        bytes.bytes.data(),
        static_cast<uint32_t>(bytes.bytes.size())
    );
    assert(encoded.ok);
    assert(bytes.bytes[4] == codec::ENVELOPE_VERSION);

    seq::SequencerState loaded{};
    loaded.reset();
    assert(codec::applyPatternEnvelope(
        bytes.bytes.data(),
        encoded.size,
        loaded.pattern
    ));
    assertTwoLanes(loaded.pattern);
    const auto region = seq::patternPlaybackRegion(loaded.pattern);
    assert(region.contentLength == 32U);
    assert(region.playStart == 2U);
    assert(region.loopStart == 5U);
    assert(region.loopEnd == 27U);
    const auto* graph = seq::graphView(loaded.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(seq::rootStepNodeId(0U));
    assert(node != nullptr);
    assert(oc::note::sequencer::chordSpecsEqual(node->chordSpec, chord));
    assert(node->chordSpec.voices() == intervals.size());
    for (uint8_t voice = 0U; voice < intervals.size(); ++voice) {
        assert(node->chordSpec.customInterval(voice) == intervals[voice]);
    }

    auto invalidPitchContext = bytes;
    constexpr uint32_t PITCH_CONTEXT_OFFSET =
        codec::ENVELOPE_HEADER_SIZE +
        codec::ENVELOPE_SECTION_HEADER_SIZE +
        2U;
    invalidPitchContext.bytes[PITCH_CONTEXT_OFFSET] = 0xFFU;
    assert(!codec::applyPatternEnvelope(
        invalidPitchContext.bytes.data(),
        encoded.size,
        loaded.pattern
    ));

    bytes.bytes[4] = codec::LEGACY_ENVELOPE_VERSION;
    assert(codec::applyPatternEnvelope(
        bytes.bytes.data(),
        encoded.size,
        loaded.pattern
    ));
    bytes.bytes[4] = static_cast<uint8_t>(codec::ENVELOPE_VERSION + 1U);
    assert(!codec::applyPatternEnvelope(
        bytes.bytes.data(),
        encoded.size,
        loaded.pattern
    ));

    std::cout << "[PASS] Pattern envelope accepts v11 migration and rejects future versions\n";
}

void authorTrackRegions(
    seq::SequencerTrackBankState& bank,
    seq::SequencerState& active
) {
    const uint8_t activeTrack = bank.activeTrackIndex();
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        auto& pattern = track == activeTrack ? active.pattern : bank.track(track);
        pattern.setContentLength(128U);
        assert(seq::setPatternPlaybackRegion(
            pattern,
            {
                128U,
                track,
                static_cast<uint8_t>(track + 1U),
                static_cast<uint8_t>(track + 100U),
            }
        ));
    }
}

void assertTrackRegions(
    const seq::SequencerTrackBankState& bank,
    const seq::SequencerState& active
) {
    const uint8_t activeTrack = bank.activeTrackIndex();
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const auto& pattern = track == activeTrack ? active.pattern : bank.track(track);
        const auto region = seq::patternPlaybackRegion(pattern);
        assert(region.contentLength == 128U);
        assert(region.playStart == track);
        assert(region.loopStart == static_cast<uint8_t>(track + 1U));
        assert(region.loopEnd == static_cast<uint8_t>(track + 100U));
    }
}

void testProjectAndSetRoundTripEveryTrackOwner() {
    seq::SequencerState source{};
    seq::SequencerTrackBankState bank{};
    source.reset();
    bank.reset();
    assert(seq::initializeTrackBankFromActive(bank, source));
    bank.syncSharedTrackState(0x0007U, 0U);
    authorTwoLanes(source.pattern);
    authorTrackRegions(bank, source);

    const uint8_t activeTrack = bank.activeTrackIndex();
    auto* track1 = seq::ensureSequencerCcLaneBank(bank.track(1U));
    assert(track1 != nullptr);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = 11U;
    draft.destination.routePolicy = seq::SequencerCcLaneRoutePolicy::PINNED;
    draft.destination.pinnedPort = 3U;
    draft.destination.pinnedChannel = 12U;
    assert(seq::createSequencerCcLane(*track1, 3U, draft).changed());
    assert(seq::setSequencerCcLaneEvent(*track1, 3U, 64U, 42U).changed());
    assert(bank.setTrackKind(2U, seq::SequencerTrackKind::DRUM, true));
    assert(bank.drumTrack(2U).pattern.setStepEnabled(1U, 3U, true));
    assert(bank.drumTrack(2U).pattern.setStepVelocity(1U, 3U, 109U));

    seq::SequencerTrackBankSnapshot flat{};
    seq::captureTrackBankSnapshot(bank, source, flat);
    auto drums = std::make_unique<seq::DrumTrackBankSnapshot>();
    assert(drums);
    bank.captureDrumTrackBank(*drums);
    codec::ProjectSequencerSnapshotEncodeSource projectSource{};
    projectSource.flat = &flat;
    projectSource.drums = drums.get();
    for (uint8_t track = 0U;
         track < seq::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const auto& pattern = track == activeTrack
            ? source.pattern
            : bank.track(track);
        projectSource.ccLanes[track] = seq::sequencerCcLaneView(pattern);
    }

    codec::EnvelopeBuffer projectBytes{};
    const auto projectEncoded = codec::fillProjectSequencerEnvelope(
        projectSource,
        projectBytes.bytes.data(),
        static_cast<uint32_t>(projectBytes.bytes.size())
    );
    assert(projectEncoded.ok);
    seq::SequencerState projectLoaded{};
    seq::SequencerTrackBankState projectBank{};
    projectLoaded.reset();
    projectBank.reset();
    assert(codec::applyProjectSequencerEnvelope(
        projectBytes.bytes.data(),
        projectEncoded.size,
        projectBank,
        projectLoaded
    ));
    assertTwoLanes(projectLoaded.pattern);
    assert(seq::sequencerCcLaneView(projectBank.track(1U))->lanes[3].values[64] == 42U);
    assert(projectBank.isDrumTrack(2U));
    assert(projectBank.drumTrack(2U).pattern.stepEnabled(1U, 3U));
    assert(projectBank.drumTrack(2U).pattern.lanes[1U].velocity[3U] == 109U);
    assertTrackRegions(projectBank, projectLoaded);

    codec::EnvelopeBuffer setBytes{};
    const auto setEncoded = codec::fillSetEnvelope(
        bank,
        source,
        setBytes.bytes.data(),
        static_cast<uint32_t>(setBytes.bytes.size())
    );
    assert(setEncoded.ok);
    seq::SequencerState setLoaded{};
    seq::SequencerTrackBankState setBank{};
    setLoaded.reset();
    setBank.reset();
    assert(codec::applySetEnvelope(
        setBytes.bytes.data(),
        setEncoded.size,
        setBank,
        setLoaded
    ));
    assertTwoLanes(setLoaded.pattern);
    assert(seq::sequencerCcLaneView(setBank.track(1U))->lanes[3].values[64] == 42U);
    assert(setBank.isDrumTrack(2U));
    assert(setBank.drumTrack(2U).pattern.stepEnabled(1U, 3U));
    assert(setBank.drumTrack(2U).pattern.lanes[1U].velocity[3U] == 109U);
    assertTrackRegions(setBank, setLoaded);

    std::cout << "[PASS] Project and Set retain every Track-local lane owner\n";
}

void testEnvelopeWithoutDrumsClearsExistingDrumBank() {
    seq::SequencerState source{};
    seq::SequencerTrackBankState bank{};
    source.reset();
    bank.reset();
    assert(seq::initializeTrackBankFromActive(bank, source));

    seq::SequencerTrackBankSnapshot flat{};
    seq::captureTrackBankSnapshot(bank, source, flat);
    codec::ProjectSequencerSnapshotEncodeSource projectSource{};
    projectSource.flat = &flat;

    codec::EnvelopeBuffer projectBytes{};
    const auto projectEncoded = codec::fillProjectSequencerEnvelope(
        projectSource,
        projectBytes.bytes.data(),
        static_cast<uint32_t>(projectBytes.bytes.size())
    );
    assert(projectEncoded.ok);

    seq::SequencerState loaded{};
    seq::SequencerTrackBankState loadedBank{};
    loaded.reset();
    loadedBank.reset();
    assert(loadedBank.setTrackKind(0U, seq::SequencerTrackKind::DRUM, true));
    assert(loadedBank.drumTrackMask() != 0U);
    assert(codec::applyProjectSequencerEnvelope(
        projectBytes.bytes.data(),
        projectEncoded.size,
        loadedBank,
        loaded
    ));
    assert(loadedBank.drumTrackMask() == 0U);

    codec::EnvelopeBuffer setBytes{};
    const auto setEncoded = codec::fillSetEnvelope(
        bank,
        source,
        setBytes.bytes.data(),
        static_cast<uint32_t>(setBytes.bytes.size())
    );
    assert(setEncoded.ok);
    assert(loadedBank.setTrackKind(0U, seq::SequencerTrackKind::DRUM, true));
    assert(codec::applySetEnvelope(
        setBytes.bytes.data(),
        setEncoded.size,
        loadedBank,
        loaded
    ));
    assert(loadedBank.drumTrackMask() == 0U);

    setBytes.bytes[4] = codec::LEGACY_ENVELOPE_VERSION;
    assert(loadedBank.setTrackKind(0U, seq::SequencerTrackKind::DRUM, true));
    assert(codec::applySetEnvelope(
        setBytes.bytes.data(),
        setEncoded.size,
        loadedBank,
        loaded
    ));
    assert(loadedBank.drumTrackMask() == 0U);

    std::cout << "[PASS] Drum state is replaced by Drum-free current and v11 envelopes\n";
}

}  // namespace

int main() {
    testCurrentRecordRoundTripAndStrictVersioning();
    testPatternEnvelopeRoundTripAndStrictVersioning();
    testProjectAndSetRoundTripEveryTrackOwner();
    testEnvelopeWithoutDrumsClearsExistingDrumBank();
    std::cout << "All SequencerCcLanePersistence tests passed\n";
    return 0;
}

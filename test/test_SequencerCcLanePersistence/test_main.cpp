#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "persistence/SequencerCcLanePersistenceCodec.hpp"
#include "persistence/SequencerPersistenceEnvelope.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace {

namespace codec = core::persistence::sequencer_codec;
namespace seq = core::state::sequencer;

constexpr uint16_t kEnvelopeHeaderSize = 12;
constexpr uint16_t kSectionHeaderSize = 10;
constexpr uint16_t kCcLaneSectionId = 19;

uint16_t readU16(const uint8_t* data) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(data[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8U)
    );
}

void writeU16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value & 0xFFU);
    data[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

uint32_t findSectionHeader(
    const uint8_t* data,
    uint32_t size,
    uint16_t requestedId,
    uint8_t requestedTrack
) {
    assert(data != nullptr);
    assert(size >= kEnvelopeHeaderSize);
    const uint16_t sectionCount = readU16(data + 8U);
    uint32_t offset = kEnvelopeHeaderSize;
    for (uint16_t i = 0; i < sectionCount; ++i) {
        assert(offset + kSectionHeaderSize <= size);
        const uint16_t id = readU16(data + offset);
        const uint8_t track = data[offset + 2U];
        const uint16_t byteSize = readU16(data + offset + 8U);
        if (id == requestedId && track == requestedTrack) return offset;
        offset += kSectionHeaderSize + byteSize;
    }
    return size;
}

void authorTwoLanes(seq::SequencerPatternState& pattern) {
    auto* bank = seq::ensureSequencerCcLaneBank(pattern);
    assert(bank != nullptr);

    seq::SequencerCcLaneDraft inherited{};
    inherited.destination.controller = 74;
    inherited.destination.minimum = 10;
    inherited.destination.maximum = 110;
    inherited.destination.routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    inherited.initialValue = 64;
    assert(seq::createSequencerCcLane(*bank, 0, inherited).changed());
    assert(seq::setSequencerCcLaneEvent(*bank, 0, 0, 64).changed());
    assert(seq::setSequencerCcLaneEvent(*bank, 0, 127, 110).changed());
    assert(seq::setSequencerCcLaneTransition(
        *bank, 0, 0, seq::SequencerCcLaneTransition::LINEAR
    ).changed());
    assert(seq::setSequencerCcLaneTransition(
        *bank, 0, 127, seq::SequencerCcLaneTransition::EASE_IN_OUT
    ).changed());

    seq::SequencerCcLaneDraft pinned{};
    pinned.destination.controller = 1;
    pinned.destination.minimum = 0;
    pinned.destination.maximum = 127;
    pinned.destination.routePolicy = seq::SequencerCcLaneRoutePolicy::PINNED;
    pinned.destination.pinnedPort = 2;
    pinned.destination.pinnedChannel = 7;
    pinned.initialValue = 20;
    pinned.acceptedMacroConflict = true;
    assert(seq::createSequencerCcLane(*bank, 1, pinned).changed());
    assert(seq::setSequencerCcLaneEvent(*bank, 1, 3, 99).changed());
    assert(seq::setSequencerCcLaneTransition(
        *bank, 1, 3, seq::SequencerCcLaneTransition::EASE_OUT
    ).changed());
    pattern.bumpCcLaneRevision();
}

void assertTwoLanes(const seq::SequencerPatternState& pattern) {
    const auto* bank = seq::sequencerCcLaneView(pattern);
    assert(bank != nullptr);
    assert(seq::sequencerCcLaneCount(*bank) == 2);
    assert(bank->lanes[0].destination.routePolicy ==
           seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK);
    assert(bank->lanes[0].activeMask.test(0));
    assert(bank->lanes[0].values[0] == 64);
    assert(seq::sequencerCcLaneTransition(bank->lanes[0], 0) ==
           seq::SequencerCcLaneTransition::LINEAR);
    assert(bank->lanes[0].activeMask.test(127));
    assert(bank->lanes[0].values[127] == 110);
    assert(seq::sequencerCcLaneTransition(bank->lanes[0], 127) ==
           seq::SequencerCcLaneTransition::EASE_IN_OUT);
    assert(bank->lanes[1].destination.routePolicy ==
           seq::SequencerCcLaneRoutePolicy::PINNED);
    assert(bank->lanes[1].destination.pinnedPort == 2);
    assert(bank->lanes[1].destination.pinnedChannel == 7);
    assert(bank->lanes[1].acceptedMacroConflict);
    assert(bank->lanes[1].activeMask.test(3));
    assert(bank->lanes[1].values[3] == 99);
    assert(seq::sequencerCcLaneTransition(bank->lanes[1], 3) ==
           seq::SequencerCcLaneTransition::EASE_OUT);
}

void test_record_roundtrip_and_atomic_rejection() {
    seq::SequencerCcLaneBank source{};
    seq::SequencerPatternState pattern{};
    authorTwoLanes(pattern);
    source = *seq::sequencerCcLaneView(pattern);

    std::array<uint8_t, codec::SEQUENCER_CC_LANE_BANK_RECORD_SIZE> bytes{};
    assert(codec::encodeSequencerCcLaneBankRecord(source, bytes.data(), bytes.size()));

    seq::SequencerCcLaneBank decoded{};
    assert(codec::decodeSequencerCcLaneBankRecord(bytes.data(), bytes.size(), decoded));
    assert(seq::sameSequencerCcLaneBankMusicalData(source, decoded));

    // V1 records have the same 156-byte lane prefix and no transition bytes.
    // Decoding them must preserve every event and migrate all shapes to Hold.
    std::array<uint8_t, codec::LEGACY_SEQUENCER_CC_LANE_BANK_RECORD_SIZE>
        legacyBytes{};
    std::copy_n(bytes.data(), 5U, legacyBytes.data());
    legacyBytes[0] = 1U;
    constexpr size_t V1_LANE_SIZE = 156U;
    constexpr size_t V3_LANE_SIZE = 204U;
    for (size_t lane = 0; lane < seq::SequencerCcLaneBank::MAX_LANES; ++lane) {
        std::copy_n(
            bytes.data() + 5U + lane * V3_LANE_SIZE,
            V1_LANE_SIZE,
            legacyBytes.data() + 5U + lane * V1_LANE_SIZE
        );
    }
    seq::SequencerCcLaneBank migrated{};
    assert(codec::decodeSequencerCcLaneBankRecord(
        legacyBytes.data(), legacyBytes.size(), migrated
    ));
    assert(migrated.lanes[0].values[0] == 64);
    assert(seq::sequencerCcLaneTransition(migrated.lanes[0], 0) ==
           seq::SequencerCcLaneTransition::HOLD);
    assert(seq::sequencerCcLaneTransition(migrated.lanes[1], 3) ==
           seq::SequencerCcLaneTransition::HOLD);

    // V2 packed four shapes on two bits. Repack a true V2 record and verify
    // that all representable shapes migrate into the V3 three-bit layout.
    std::array<uint8_t, codec::LEGACY_V2_SEQUENCER_CC_LANE_BANK_RECORD_SIZE>
        legacyV2Bytes{};
    std::copy_n(bytes.data(), 5U, legacyV2Bytes.data());
    legacyV2Bytes[0] = 2U;
    constexpr size_t V2_LANE_SIZE = 188U;
    for (size_t laneIndex = 0;
         laneIndex < seq::SequencerCcLaneBank::MAX_LANES;
         ++laneIndex) {
        std::copy_n(
            bytes.data() + 5U + laneIndex * V3_LANE_SIZE,
            V1_LANE_SIZE,
            legacyV2Bytes.data() + 5U + laneIndex * V2_LANE_SIZE
        );
        const auto& lane = source.lanes[laneIndex];
        auto* transitions = legacyV2Bytes.data() + 5U +
            laneIndex * V2_LANE_SIZE + V1_LANE_SIZE;
        for (uint8_t step = 0; step < seq::SequencerCcLaneBank::MAX_STEPS; ++step) {
            const auto shape = seq::sequencerCcLaneTransition(lane, step);
            const uint8_t value = shape == seq::SequencerCcLaneTransition::EASE_IN_OUT
                ? 0U
                : static_cast<uint8_t>(shape);
            transitions[step / 4U] = static_cast<uint8_t>(
                transitions[step / 4U] |
                static_cast<uint8_t>(value << ((step % 4U) * 2U))
            );
        }
    }
    seq::SequencerCcLaneBank migratedV2{};
    assert(codec::decodeSequencerCcLaneBankRecord(
        legacyV2Bytes.data(),
        legacyV2Bytes.size(),
        migratedV2
    ));
    assert(seq::sequencerCcLaneTransition(migratedV2.lanes[0], 0) ==
           seq::SequencerCcLaneTransition::LINEAR);
    assert(seq::sequencerCcLaneTransition(migratedV2.lanes[1], 3) ==
           seq::SequencerCcLaneTransition::EASE_OUT);
    assert(seq::sequencerCcLaneTransition(migratedV2.lanes[0], 127) ==
           seq::SequencerCcLaneTransition::HOLD);

    seq::SequencerCcLaneBank sentinel = decoded;
    const auto canonicalBytes = bytes;
    bytes[5] = 2;  // occupied must be canonical bool 0/1
    assert(!codec::decodeSequencerCcLaneBankRecord(bytes.data(), bytes.size(), decoded));
    assert(seq::sameSequencerCcLaneBankMusicalData(sentinel, decoded));
    bytes = canonicalBytes;
    bytes[6] = 0xFF;  // acceptedMacroConflict must also be canonical
    assert(!codec::decodeSequencerCcLaneBankRecord(bytes.data(), bytes.size(), decoded));
    assert(seq::sameSequencerCcLaneBankMusicalData(sentinel, decoded));
    bytes = canonicalBytes;
    bytes[0] = static_cast<uint8_t>(seq::SequencerCcLaneBank::FORMAT_VERSION + 1U);
    assert(!codec::decodeSequencerCcLaneBankRecord(bytes.data(), bytes.size(), decoded));
    assert(seq::sameSequencerCcLaneBankMusicalData(sentinel, decoded));
    std::cout << "[PASS] fixed CC lane record roundtrip and atomic rejection\n";
}

void test_pattern_v6_roundtrip_v5_migration_and_malformed_atomicity() {
    seq::SequencerState source{};
    source.reset();
    source.pattern.midiChannel.set(2);
    authorTwoLanes(source.pattern);

    codec::PatternEnvelopeBuffer buffer{};
    const auto encoded = codec::fillPatternEnvelope(
        source.pattern,
        buffer.bytes.data(),
        buffer.bytes.size()
    );
    assert(encoded.ok);
    assert(buffer.bytes[4] == codec::CC_LANE_ENVELOPE_VERSION);

    seq::SequencerState loaded{};
    loaded.reset();
    assert(codec::applyPatternEnvelope(buffer.bytes.data(), encoded.size, loaded.pattern));
    assert(loaded.pattern.midiChannel.get() == 2);
    assertTwoLanes(loaded.pattern);

    const auto ccHeader = findSectionHeader(
        buffer.bytes.data(),
        encoded.size,
        kCcLaneSectionId,
        0
    );
    assert(ccHeader < encoded.size);

    // The CC section is last for this graph-free Pattern. Replace its V3
    // payload with a real V2 payload under the legacy v5 envelope header.
    std::array<uint8_t, codec::LEGACY_V2_SEQUENCER_CC_LANE_BANK_RECORD_SIZE>
        legacyV2Record{};
    const auto* sourceBank = seq::sequencerCcLaneView(source.pattern);
    assert(sourceBank != nullptr);
    std::array<uint8_t, codec::SEQUENCER_CC_LANE_BANK_RECORD_SIZE> currentRecord{};
    assert(codec::encodeSequencerCcLaneBankRecord(
        *sourceBank,
        currentRecord.data(),
        currentRecord.size()
    ));
    std::copy_n(currentRecord.data(), 5U, legacyV2Record.data());
    legacyV2Record[0] = 2U;
    constexpr size_t PREFIX_SIZE = 156U;
    constexpr size_t CURRENT_LANE_SIZE = 204U;
    constexpr size_t LEGACY_V2_LANE_SIZE = 188U;
    for (size_t laneIndex = 0;
         laneIndex < seq::SequencerCcLaneBank::MAX_LANES;
         ++laneIndex) {
        std::copy_n(
            currentRecord.data() + 5U + laneIndex * CURRENT_LANE_SIZE,
            PREFIX_SIZE,
            legacyV2Record.data() + 5U + laneIndex * LEGACY_V2_LANE_SIZE
        );
        auto* packed = legacyV2Record.data() + 5U +
            laneIndex * LEGACY_V2_LANE_SIZE + PREFIX_SIZE;
        for (uint8_t step = 0; step < seq::SequencerCcLaneBank::MAX_STEPS; ++step) {
            const auto shape = seq::sequencerCcLaneTransition(
                sourceBank->lanes[laneIndex],
                step
            );
            const uint8_t value = shape == seq::SequencerCcLaneTransition::EASE_IN_OUT
                ? 0U
                : static_cast<uint8_t>(shape);
            packed[step / 4U] = static_cast<uint8_t>(
                packed[step / 4U] |
                static_cast<uint8_t>(value << ((step % 4U) * 2U))
            );
        }
    }
    auto legacyEnvelope = buffer;
    legacyEnvelope.bytes[4] = codec::LEGACY_CC_LANE_ENVELOPE_VERSION;
    writeU16(
        legacyEnvelope.bytes.data() + ccHeader + 4U,
        codec::LEGACY_V2_SEQUENCER_CC_LANE_BANK_RECORD_SIZE
    );
    writeU16(
        legacyEnvelope.bytes.data() + ccHeader + 8U,
        codec::LEGACY_V2_SEQUENCER_CC_LANE_BANK_RECORD_SIZE
    );
    std::copy(
        legacyV2Record.begin(),
        legacyV2Record.end(),
        legacyEnvelope.bytes.begin() + ccHeader + kSectionHeaderSize
    );
    const uint32_t legacyEnvelopeSize = ccHeader + kSectionHeaderSize +
        codec::LEGACY_V2_SEQUENCER_CC_LANE_BANK_RECORD_SIZE;
    seq::SequencerState migratedEnvelope{};
    migratedEnvelope.reset();
    assert(codec::applyPatternEnvelope(
        legacyEnvelope.bytes.data(),
        legacyEnvelopeSize,
        migratedEnvelope.pattern
    ));
    assert(seq::sequencerCcLaneTransition(
        seq::sequencerCcLaneView(migratedEnvelope.pattern)->lanes[1],
        3
    ) == seq::SequencerCcLaneTransition::EASE_OUT);
    // Bank header (5) + lane route-policy field (9).
    buffer.bytes[ccHeader + kSectionHeaderSize + 14U] = 0xFF;

    seq::SequencerState unchanged{};
    unchanged.reset();
    unchanged.pattern.midiChannel.set(11);
    authorTwoLanes(unchanged.pattern);
    seq::SequencerCcLaneBank before = *seq::sequencerCcLaneView(unchanged.pattern);
    assert(!codec::applyPatternEnvelope(
        buffer.bytes.data(),
        encoded.size,
        unchanged.pattern
    ));
    assert(unchanged.pattern.midiChannel.get() == 11);
    assert(seq::sameSequencerCcLaneBankMusicalData(
        before,
        *seq::sequencerCcLaneView(unchanged.pattern)
    ));

    seq::SequencerState legacySource{};
    legacySource.reset();
    codec::PatternEnvelopeBuffer legacy{};
    const auto legacyEncoded = codec::fillPatternEnvelope(
        legacySource.pattern,
        legacy.bytes.data(),
        legacy.bytes.size()
    );
    assert(legacyEncoded.ok);
    assert(legacy.bytes[4] == codec::LEGACY_ENVELOPE_VERSION);
    assert(codec::applyPatternEnvelope(
        legacy.bytes.data(),
        legacyEncoded.size,
        unchanged.pattern
    ));
    assert(seq::sequencerCcLaneView(unchanged.pattern) == nullptr);

    legacy.bytes[4] = static_cast<uint8_t>(
        codec::SEMANTIC_CHORD_ENVELOPE_VERSION + 1U
    );
    unchanged.pattern.midiChannel.set(9);
    assert(!codec::applyPatternEnvelope(
        legacy.bytes.data(),
        legacyEncoded.size,
        unchanged.pattern
    ));
    assert(unchanged.pattern.midiChannel.get() == 9);
    std::cout << "[PASS] Pattern v6, v5 migration, future/malformed rejection\n";
}

void test_project_and_set_roundtrip_every_track_owner() {
    seq::SequencerState source{};
    seq::SequencerTrackBankState bank{};
    source.reset();
    bank.reset();
    assert(seq::initializeTrackBankFromActive(bank, source));
    authorTwoLanes(source.pattern);

    auto* track1 = seq::ensureSequencerCcLaneBank(bank.track(1));
    assert(track1 != nullptr);
    seq::SequencerCcLaneDraft draft{};
    draft.destination.controller = 11;
    draft.destination.routePolicy = seq::SequencerCcLaneRoutePolicy::PINNED;
    draft.destination.pinnedPort = 3;
    draft.destination.pinnedChannel = 12;
    assert(seq::createSequencerCcLane(*track1, 3, draft).changed());
    assert(seq::setSequencerCcLaneEvent(*track1, 3, 64, 42).changed());

    seq::SequencerTrackBankSnapshot flat{};
    seq::captureTrackBankSnapshot(bank, source, flat);
    codec::ProjectSequencerSnapshotEncodeSource projectSource{};
    projectSource.flat = &flat;
    const uint8_t active = bank.activeTrackIndex();
    for (uint8_t i = 0; i < seq::SequencerTrackBankState::TRACK_COUNT; ++i) {
        const auto& pattern = (i == active) ? source.pattern : bank.track(i);
        projectSource.ccLanes[i] = seq::sequencerCcLaneView(pattern);
    }

    codec::EnvelopeBuffer projectBytes{};
    const auto projectEncoded = codec::fillProjectSequencerEnvelope(
        projectSource,
        projectBytes.bytes.data(),
        projectBytes.bytes.size()
    );
    assert(projectEncoded.ok);
    assert(projectBytes.bytes[4] == codec::CC_LANE_ENVELOPE_VERSION);

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
    const auto* restoredTrack1 = seq::sequencerCcLaneView(projectBank.track(1));
    assert(restoredTrack1 != nullptr);
    assert(restoredTrack1->lanes[3].destination.pinnedChannel == 12);
    assert(restoredTrack1->lanes[3].activeMask.test(64));
    assert(restoredTrack1->lanes[3].values[64] == 42);

    codec::EnvelopeBuffer setBytes{};
    const auto setEncoded = codec::fillSetEnvelope(
        bank,
        source,
        setBytes.bytes.data(),
        setBytes.bytes.size()
    );
    assert(setEncoded.ok);
    assert(setBytes.bytes[4] == codec::CC_LANE_ENVELOPE_VERSION);

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
    assert(seq::sequencerCcLaneView(setBank.track(1))->lanes[3].values[64] == 42);
    std::cout << "[PASS] Project and Set retain all Pattern-local CC lane owners\n";
}

}  // namespace

int main() {
    test_record_roundtrip_and_atomic_rejection();
    test_pattern_v6_roundtrip_v5_migration_and_malformed_atomicity();
    test_project_and_set_roundtrip_every_track_owner();
    std::cout << "All Sequencer CC lane persistence tests passed.\n";
    return 0;
}

#include "persistence/SequencerCcLanePersistenceCodec.hpp"

#include <array>
#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::persistence::sequencer_codec {

namespace {

using core::state::sequencer::SequencerCcLane;
using core::state::sequencer::SequencerCcLaneBank;

FLASHMEM void writeU16(uint8_t* data, uint16_t& offset, uint16_t value) {
    data[offset++] = static_cast<uint8_t>(value & 0xFFU);
    data[offset++] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

FLASHMEM void writeU32(uint8_t* data, uint16_t& offset, uint32_t value) {
    data[offset++] = static_cast<uint8_t>(value & 0xFFU);
    data[offset++] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    data[offset++] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    data[offset++] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

FLASHMEM uint16_t readU16(const uint8_t* data, uint16_t& offset) {
    const uint16_t result = static_cast<uint16_t>(
        static_cast<uint16_t>(data[offset]) |
        static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1U]) << 8U)
    );
    offset = static_cast<uint16_t>(offset + 2U);
    return result;
}

FLASHMEM uint32_t readU32(const uint8_t* data, uint16_t& offset) {
    const uint32_t result = static_cast<uint32_t>(data[offset]) |
        (static_cast<uint32_t>(data[offset + 1U]) << 8U) |
        (static_cast<uint32_t>(data[offset + 2U]) << 16U) |
        (static_cast<uint32_t>(data[offset + 3U]) << 24U);
    offset = static_cast<uint16_t>(offset + 4U);
    return result;
}

FLASHMEM void encodeLane(
    const SequencerCcLane& lane,
    uint8_t* data,
    uint16_t& offset
) {
    data[offset++] = lane.occupied ? 1U : 0U;
    data[offset++] = lane.acceptedMacroConflict ? 1U : 0U;
    data[offset++] = static_cast<uint8_t>(lane.conflictPolicy);
    data[offset++] = lane.initialValue;
    writeU16(data, offset, lane.lifecycleGeneration);
    data[offset++] = lane.destination.controller;
    data[offset++] = lane.destination.minimum;
    data[offset++] = lane.destination.maximum;
    data[offset++] = static_cast<uint8_t>(lane.destination.routePolicy);
    data[offset++] = lane.destination.pinnedPort;
    data[offset++] = lane.destination.pinnedChannel;
    for (uint8_t byte = 0; byte < 16U; ++byte) {
        uint8_t packed = 0;
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            const uint8_t step = static_cast<uint8_t>(byte * 8U + bit);
            if (lane.activeMask.test(step)) {
                packed = static_cast<uint8_t>(packed | (1U << bit));
            }
        }
        data[offset++] = packed;
    }
    for (const uint8_t value : lane.values) data[offset++] = value;
    for (const uint8_t packedTransitions : lane.transitions) {
        data[offset++] = packedTransitions;
    }
}

FLASHMEM bool decodeLane(
    const uint8_t* data,
    uint16_t& offset,
    SequencerCcLane& lane,
    uint8_t formatVersion
) {
    const uint8_t occupied = data[offset++];
    const uint8_t acceptedMacroConflict = data[offset++];
    if (occupied > 1U || acceptedMacroConflict > 1U) return false;
    lane.occupied = occupied == 1U;
    lane.acceptedMacroConflict = acceptedMacroConflict == 1U;
    lane.conflictPolicy = static_cast<
        core::state::sequencer::SequencerCcLaneConflictPolicy
    >(data[offset++]);
    lane.initialValue = data[offset++];
    lane.lifecycleGeneration = readU16(data, offset);
    lane.destination.controller = data[offset++];
    lane.destination.minimum = data[offset++];
    lane.destination.maximum = data[offset++];
    lane.destination.routePolicy = static_cast<
        core::state::sequencer::SequencerCcLaneRoutePolicy
    >(data[offset++]);
    lane.destination.pinnedPort = data[offset++];
    lane.destination.pinnedChannel = data[offset++];
    for (uint8_t byte = 0; byte < 16U; ++byte) {
        const uint8_t packed = data[offset++];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            lane.activeMask.setBit(
                static_cast<uint8_t>(byte * 8U + bit),
                (packed & static_cast<uint8_t>(1U << bit)) != 0
            );
        }
    }
    for (uint8_t& value : lane.values) value = data[offset++];
    if (formatVersion == SequencerCcLaneBank::FORMAT_VERSION) {
        for (uint8_t& packedTransitions : lane.transitions) {
            packedTransitions = data[offset++];
        }
    } else if (formatVersion == 2U) {
        std::array<uint8_t, 32> legacyTransitions{};
        for (uint8_t& packedTransitions : legacyTransitions) {
            packedTransitions = data[offset++];
        }
        for (uint8_t step = 0; step < SequencerCcLaneBank::MAX_STEPS; ++step) {
            const uint8_t legacyByte = static_cast<uint8_t>(step / 4U);
            const uint8_t legacyShift = static_cast<uint8_t>((step % 4U) * 2U);
            const uint8_t transition = static_cast<uint8_t>(
                (legacyTransitions[legacyByte] >> legacyShift) & 0x03U
            );
            const uint16_t bit = static_cast<uint16_t>(step) * 3U;
            const uint8_t byte = static_cast<uint8_t>(bit / 8U);
            const uint8_t shift = static_cast<uint8_t>(bit % 8U);
            uint16_t packed = lane.transitions[byte];
            if (shift > 5U && byte + 1U < lane.transitions.size()) {
                packed = static_cast<uint16_t>(
                    packed |
                    static_cast<uint16_t>(lane.transitions[byte + 1U] << 8U)
                );
            }
            const uint16_t mask = static_cast<uint16_t>(0x07U << shift);
            packed = static_cast<uint16_t>(
                (packed & static_cast<uint16_t>(~mask)) |
                (static_cast<uint16_t>(transition) << shift)
            );
            lane.transitions[byte] = static_cast<uint8_t>(packed & 0xFFU);
            if (shift > 5U && byte + 1U < lane.transitions.size()) {
                lane.transitions[byte + 1U] = static_cast<uint8_t>(packed >> 8U);
            }
        }
    }
    return true;
}

}  // namespace

FLASHMEM bool encodeSequencerCcLaneBankRecord(
    const SequencerCcLaneBank& source,
    uint8_t* out,
    uint16_t size
) {
    if (out == nullptr || size != SEQUENCER_CC_LANE_BANK_RECORD_SIZE) {
        return false;
    }
    SequencerCcLaneBank canonical{};
    if (!core::state::sequencer::decodeCanonicalSequencerCcLaneBank(
            source,
            canonical
        )) {
        return false;
    }

    std::array<uint8_t, SEQUENCER_CC_LANE_BANK_RECORD_SIZE> pending{};
    uint16_t offset = 0;
    pending[offset++] = canonical.formatVersion;
    writeU32(pending.data(), offset, canonical.revision);
    for (const auto& lane : canonical.lanes) {
        encodeLane(lane, pending.data(), offset);
    }
    if (offset != pending.size()) return false;
    std::memcpy(out, pending.data(), pending.size());
    return true;
}

FLASHMEM bool decodeSequencerCcLaneBankRecord(
    const uint8_t* data,
    uint16_t size,
    SequencerCcLaneBank& out
) {
    if (data == nullptr ||
        (size != SEQUENCER_CC_LANE_BANK_RECORD_SIZE &&
         size != LEGACY_V2_SEQUENCER_CC_LANE_BANK_RECORD_SIZE &&
         size != LEGACY_V1_SEQUENCER_CC_LANE_BANK_RECORD_SIZE)) {
        return false;
    }
    uint16_t offset = 0;
    SequencerCcLaneBank persisted{};
    persisted.formatVersion = data[offset++];
    const bool legacyV1 = persisted.formatVersion == 1U &&
        size == LEGACY_V1_SEQUENCER_CC_LANE_BANK_RECORD_SIZE;
    const bool legacyV2 = persisted.formatVersion == 2U &&
        size == LEGACY_V2_SEQUENCER_CC_LANE_BANK_RECORD_SIZE;
    const bool currentV3 =
        persisted.formatVersion == SequencerCcLaneBank::FORMAT_VERSION &&
        size == SEQUENCER_CC_LANE_BANK_RECORD_SIZE;
    if (!legacyV1 && !legacyV2 && !currentV3) return false;
    persisted.revision = readU32(data, offset);
    for (auto& lane : persisted.lanes) {
        if (!decodeLane(data, offset, lane, persisted.formatVersion)) return false;
    }
    if (offset != size) return false;

    // V1 lanes had stepped Hold semantics only; V2 shapes were repacked above.
    persisted.formatVersion = SequencerCcLaneBank::FORMAT_VERSION;

    SequencerCcLaneBank canonical{};
    if (!core::state::sequencer::decodeCanonicalSequencerCcLaneBank(
            persisted,
            canonical
        )) {
        return false;
    }
    out = canonical;
    return true;
}

}  // namespace core::persistence::sequencer_codec

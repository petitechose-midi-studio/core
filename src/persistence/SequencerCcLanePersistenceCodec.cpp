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
    SequencerCcLane& lane
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
    for (uint8_t& packedTransitions : lane.transitions) {
        packedTransitions = data[offset++];
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
    if (data == nullptr || size != SEQUENCER_CC_LANE_BANK_RECORD_SIZE) {
        return false;
    }
    uint16_t offset = 0;
    SequencerCcLaneBank persisted{};
    persisted.formatVersion = data[offset++];
    if (persisted.formatVersion != SequencerCcLaneBank::FORMAT_VERSION) {
        return false;
    }
    persisted.revision = readU32(data, offset);
    for (auto& lane : persisted.lanes) {
        if (!decodeLane(data, offset, lane)) return false;
    }
    if (offset != size) return false;

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

#include "persistence/SequencerPatternPresetCodec.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceBinaryCodec.hpp"
#include "persistence/PersistenceChecksum.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"

namespace core::persistence::sequencer_pattern_preset_codec {

namespace {

namespace binary = core::persistence::binary_codec;
namespace checksum = core::persistence::checksum;
namespace sequencer = core::state::sequencer;

constexpr uint32_t kMagic = 0x3150504DU;  // "MPP1"
constexpr uint8_t kVersion =
    sequencer::SequencerPatternPresetMetadata::CURRENT_FORMAT_VERSION;

struct Header {
    uint32_t magic = kMagic;
    uint8_t version = kVersion;
    uint8_t kind = static_cast<uint8_t>(
        sequencer::SequencerTrackKind::INSTRUMENT
    );
    uint16_t headerSize = HEADER_SIZE;
    uint16_t patternEnvelopeSize = 0U;
    uint16_t drumRecordSize = 0U;
    uint32_t payloadCrc32 = 0U;
};

FLASHMEM void setStatus(
    sequencer::SequencerPatternPresetStatus* out,
    sequencer::SequencerPatternPresetStatus status
) {
    if (out != nullptr) *out = status;
}

FLASHMEM bool writeHeader(binary::Writer& writer, const Header& header) {
    return writer.writeU32(header.magic) &&
        writer.writeU8(header.version) &&
        writer.writeU8(header.kind) &&
        writer.writeU16(header.headerSize) &&
        writer.writeU16(header.patternEnvelopeSize) &&
        writer.writeU16(header.drumRecordSize) &&
        writer.writeU32(header.payloadCrc32);
}

FLASHMEM bool readHeader(binary::Reader& reader, Header& header) {
    return reader.readU32(header.magic) &&
        reader.readU8(header.version) &&
        reader.readU8(header.kind) &&
        reader.readU16(header.headerSize) &&
        reader.readU16(header.patternEnvelopeSize) &&
        reader.readU16(header.drumRecordSize) &&
        reader.readU32(header.payloadCrc32);
}

FLASHMEM bool headerShapeCanonical(const Header& header) {
    if (header.magic != kMagic || header.headerSize != HEADER_SIZE ||
        header.patternEnvelopeSize == 0U ||
        header.patternEnvelopeSize >
            sequencer_codec::MAX_PATTERN_ENVELOPE_PAYLOAD_SIZE ||
        header.kind > static_cast<uint8_t>(sequencer::SequencerTrackKind::DRUM)) {
        return false;
    }
    const auto kind = static_cast<sequencer::SequencerTrackKind>(header.kind);
    return kind == sequencer::SequencerTrackKind::DRUM
        ? header.drumRecordSize == sequencer_codec::DRUM_TRACK_RECORD_SIZE
        : header.drumRecordSize == 0U;
}

FLASHMEM bool readPrefix(
    const uint8_t* data,
    uint16_t size,
    Header& header,
    sequencer::SequencerPatternPresetMetadata& metadata,
    sequencer::SequencerPatternPresetStatus* status
) {
    if (data == nullptr || size < HEADER_SIZE) {
        setStatus(status, sequencer::SequencerPatternPresetStatus::INVALID_ARGUMENT);
        return false;
    }

    binary::Reader reader(data, size);
    if (!readHeader(reader, header) || header.magic != kMagic) {
        setStatus(status, sequencer::SequencerPatternPresetStatus::INVALID_FORMAT);
        return false;
    }
    if (header.version != kVersion) {
        setStatus(status, sequencer::SequencerPatternPresetStatus::UNSUPPORTED_VERSION);
        return false;
    }
    if (!headerShapeCanonical(header)) {
        setStatus(status, sequencer::SequencerPatternPresetStatus::INVALID_FORMAT);
        return false;
    }

    metadata = {};
    metadata.formatVersion = header.version;
    metadata.trackKind = static_cast<sequencer::SequencerTrackKind>(header.kind);
    if (!reader.readBytes(metadata.technicalId, sizeof(metadata.technicalId)) ||
        !reader.readBytes(metadata.semanticName, sizeof(metadata.semanticName)) ||
        reader.offset() != HEADER_SIZE ||
        !sequencer::sequencerPatternPresetMetadataIsCanonical(metadata)) {
        setStatus(status, sequencer::SequencerPatternPresetStatus::INVALID_FORMAT);
        return false;
    }
    return true;
}

FLASHMEM bool drumPayloadCanonical(
    const sequencer::SequencerPatternPresetMetadata& metadata,
    const sequencer::SequencerPatternState& pattern,
    const sequencer::DrumTrackState* sourceDrum
) {
    const bool drum = metadata.trackKind == sequencer::SequencerTrackKind::DRUM;
    return drum
        ? sourceDrum != nullptr && sequencer::sequencerCcLaneView(pattern) == nullptr
        : sourceDrum == nullptr;
}

}  // namespace

FLASHMEM EncodeResult encode(
    const sequencer::SequencerPatternPresetMetadata& metadata,
    const sequencer::SequencerPatternState& pattern,
    const sequencer::DrumTrackState* sourceDrum,
    uint8_t* out,
    uint16_t capacity
) {
    if (out == nullptr ||
        !sequencer::sequencerPatternPresetMetadataIsCanonical(metadata) ||
        !drumPayloadCanonical(metadata, pattern, sourceDrum)) {
        return {
            .status = sequencer::SequencerPatternPresetStatus::INVALID_ARGUMENT,
        };
    }

    const bool drum =
        metadata.trackKind == sequencer::SequencerTrackKind::DRUM;
    const uint16_t drumSize =
        drum ? sequencer_codec::DRUM_TRACK_RECORD_SIZE : 0U;
    if (capacity < static_cast<uint32_t>(HEADER_SIZE) + drumSize) {
        return {
            .status = sequencer::SequencerPatternPresetStatus::BUFFER_TOO_SMALL,
        };
    }

    const uint32_t patternCapacity =
        static_cast<uint32_t>(capacity) - HEADER_SIZE - drumSize;
    const auto patternEnvelope = sequencer_codec::fillPatternEnvelope(
        pattern,
        out + HEADER_SIZE,
        patternCapacity
    );
    if (!patternEnvelope.ok || patternEnvelope.size == 0U ||
        patternEnvelope.size > UINT16_MAX) {
        return {
            .status = patternCapacity <
                    sequencer_codec::MAX_PATTERN_ENVELOPE_PAYLOAD_SIZE
                ? sequencer::SequencerPatternPresetStatus::BUFFER_TOO_SMALL
                : sequencer::SequencerPatternPresetStatus::INVALID_ARGUMENT,
        };
    }

    const uint32_t drumOffset = HEADER_SIZE + patternEnvelope.size;
    if (drum && !sequencer_codec::encodeDrumTrackRecord(
                    *sourceDrum,
                    out + drumOffset,
                    drumSize
                )) {
        return {
            .status = sequencer::SequencerPatternPresetStatus::INVALID_ARGUMENT,
        };
    }

    Header header{};
    header.kind = static_cast<uint8_t>(metadata.trackKind);
    header.patternEnvelopeSize = static_cast<uint16_t>(patternEnvelope.size);
    header.drumRecordSize = drumSize;
    const uint16_t payloadSize = static_cast<uint16_t>(
        patternEnvelope.size + drumSize
    );
    binary::Writer writer(out, HEADER_SIZE);
    if (!writeHeader(writer, header) ||
        !writer.writeBytes(
            metadata.technicalId,
            sizeof(metadata.technicalId)
        ) ||
        !writer.writeBytes(
            metadata.semanticName,
            sizeof(metadata.semanticName)
        ) ||
        writer.offset() != HEADER_SIZE) {
        return {
            .status = sequencer::SequencerPatternPresetStatus::BUFFER_TOO_SMALL,
        };
    }
    header.payloadCrc32 = checksum::crc32(
        out + TECHNICAL_ID_OFFSET,
        static_cast<uint32_t>(METADATA_SIZE) + payloadSize
    );
    binary::Writer headerWriter(out, BASE_HEADER_SIZE);
    if (!writeHeader(headerWriter, header) ||
        headerWriter.offset() != BASE_HEADER_SIZE) {
        return {
            .status = sequencer::SequencerPatternPresetStatus::BUFFER_TOO_SMALL,
        };
    }

    return {
        .status = sequencer::SequencerPatternPresetStatus::OK,
        .bytesWritten = static_cast<uint16_t>(HEADER_SIZE + payloadSize),
    };
}

FLASHMEM bool decodeMetadata(
    const uint8_t* data,
    uint16_t size,
    MetadataView& out,
    sequencer::SequencerPatternPresetStatus* status
) {
    out = {};
    setStatus(status, sequencer::SequencerPatternPresetStatus::OK);
    Header header{};
    if (!readPrefix(data, size, header, out.metadata, status)) return false;
    out.patternEnvelopeSize = header.patternEnvelopeSize;
    out.drumRecordSize = header.drumRecordSize;
    out.payloadCrc32 = header.payloadCrc32;
    return true;
}

FLASHMEM bool decode(
    const uint8_t* data,
    uint16_t size,
    sequencer::SequencerPatternPresetMetadata& metadataOut,
    sequencer::SequencerPatternState& patternScratchOut,
    sequencer::DrumTrackState* drumScratchOut,
    sequencer::SequencerPatternPresetStatus* status
) {
    metadataOut = {};
    patternScratchOut.reset();
    if (drumScratchOut != nullptr) {
        drumScratchOut->reset(sequencer::DrumKitPreset::EMPTY);
    }
    setStatus(status, sequencer::SequencerPatternPresetStatus::OK);

    Header header{};
    sequencer::SequencerPatternPresetMetadata metadata{};
    if (!readPrefix(data, size, header, metadata, status)) return false;
    const uint32_t expectedSize = static_cast<uint32_t>(HEADER_SIZE) +
        header.patternEnvelopeSize + header.drumRecordSize;
    if (expectedSize != size ||
        checksum::crc32(
            data + TECHNICAL_ID_OFFSET,
            size - TECHNICAL_ID_OFFSET
        ) !=
            header.payloadCrc32) {
        setStatus(status, sequencer::SequencerPatternPresetStatus::INVALID_FORMAT);
        return false;
    }

    const bool drum = metadata.trackKind == sequencer::SequencerTrackKind::DRUM;
    if (drum && drumScratchOut == nullptr) {
        setStatus(status, sequencer::SequencerPatternPresetStatus::INVALID_ARGUMENT);
        return false;
    }
    const uint8_t* patternBytes = data + HEADER_SIZE;
    const uint8_t* drumBytes = patternBytes + header.patternEnvelopeSize;
    if ((drum && !sequencer_codec::decodeDrumTrackRecord(
                     drumBytes,
                     header.drumRecordSize,
                     *drumScratchOut
                 )) ||
        !sequencer_codec::applyPatternEnvelope(
            patternBytes,
            header.patternEnvelopeSize,
            patternScratchOut
        ) ||
        (drum && sequencer::sequencerCcLaneView(patternScratchOut) != nullptr)) {
        patternScratchOut.reset();
        if (drumScratchOut != nullptr) {
            drumScratchOut->reset(sequencer::DrumKitPreset::EMPTY);
        }
        setStatus(status, sequencer::SequencerPatternPresetStatus::INVALID_FORMAT);
        return false;
    }

    metadataOut = metadata;
    return true;
}

}  // namespace core::persistence::sequencer_pattern_preset_codec

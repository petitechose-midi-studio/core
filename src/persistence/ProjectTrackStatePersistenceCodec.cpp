#include "persistence/ProjectTrackStatePersistenceCodec.hpp"

#include <array>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceBinaryCodec.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"

namespace core::persistence::project_track_codec {

namespace {

namespace binary = core::persistence::binary_codec;
namespace project = core::state::project;

FLASHMEM bool validSnapshot(const project::ProjectTrackSnapshot& snapshot) {
    for (uint8_t track = 0U; track < project::PROJECT_TRACK_COUNT; ++track) {
        if (!project::validProjectTrackMidiChannel(
                snapshot.midiChannels[track]
            ) ||
            !project::validProjectTrackDelayMs(snapshot.delayMs[track])) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool writePayload(
    const project::ProjectTrackSnapshot& source,
    uint8_t* out
) {
    binary::Writer writer(out, PROJECT_TRACK_STATE_PAYLOAD_SIZE);
    for (const uint8_t channel : source.midiChannels) {
        if (!writer.writeU8(channel)) return false;
    }
    for (const int16_t delayMs : source.delayMs) {
        if (!writer.writeI16(delayMs)) return false;
    }
    return writer.writeU16(source.mutedMask) &&
           writer.writeU16(source.soloMask) &&
           writer.ok() &&
           writer.offset() == PROJECT_TRACK_STATE_PAYLOAD_SIZE;
}

FLASHMEM bool readPayload(
    const uint8_t* data,
    project::ProjectTrackSnapshot& pending
) {
    binary::Reader reader(data, PROJECT_TRACK_STATE_PAYLOAD_SIZE);
    for (uint8_t& channel : pending.midiChannels) {
        if (!reader.readU8(channel)) return false;
    }
    for (int16_t& delayMs : pending.delayMs) {
        if (!reader.readI16(delayMs)) return false;
    }
    return reader.readU16(pending.mutedMask) &&
           reader.readU16(pending.soloMask) &&
           reader.ok() &&
           reader.offset() == PROJECT_TRACK_STATE_PAYLOAD_SIZE;
}

}  // namespace

FLASHMEM EncodeResult encodeProjectTrackStatePayload(
    const project::ProjectTrackSnapshot& source,
    uint8_t* out,
    uint32_t outCapacity
) {
    if (out == nullptr) {
        return {.status = Status::INVALID_ARGUMENT};
    }
    if (!validSnapshot(source)) {
        return {.status = Status::INVALID_DOMAIN};
    }
    if (outCapacity < PROJECT_TRACK_STATE_PAYLOAD_SIZE) {
        return {.status = Status::BUFFER_TOO_SMALL};
    }

    std::array<uint8_t, PROJECT_TRACK_STATE_PAYLOAD_SIZE> pending{};
    if (!writePayload(source, pending.data())) {
        return {.status = Status::INVALID_DOMAIN};
    }
    for (uint32_t index = 0U; index < pending.size(); ++index) {
        out[index] = pending[index];
    }
    return {
        .status = Status::OK,
        .bytesRequired = PROJECT_TRACK_STATE_PAYLOAD_SIZE,
        .bytesWritten = PROJECT_TRACK_STATE_PAYLOAD_SIZE,
    };
}

FLASHMEM DecodeResult decodeProjectTrackStatePayload(
    const uint8_t* data,
    uint32_t size,
    uint8_t versionMajor,
    uint8_t versionMinor,
    project::ProjectTrackSnapshot& out
) {
    if (data == nullptr) {
        return {.status = Status::INVALID_ARGUMENT};
    }
    if (versionMajor != PROJECT_TRACK_CHUNK_VERSION_MAJOR ||
        versionMinor != PROJECT_TRACK_CHUNK_VERSION_MINOR) {
        return {.status = Status::UNSUPPORTED_VERSION};
    }
    if (size != PROJECT_TRACK_STATE_PAYLOAD_SIZE) {
        return {.status = Status::INVALID_PAYLOAD_SIZE};
    }

    project::ProjectTrackSnapshot pending{};
    if (!readPayload(data, pending) || !validSnapshot(pending)) {
        return {.status = Status::INVALID_DOMAIN};
    }

    out = pending;
    return {.status = Status::OK};
}

}  // namespace core::persistence::project_track_codec

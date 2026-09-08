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
    return project::validProjectTrackSnapshot(snapshot);
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
    if (!writer.writeU16(source.mutedMask) ||
        !writer.writeU16(source.soloMask)) {
        return false;
    }
    for (const auto& name : source.names) {
        for (const char character : name) {
            if (!writer.writeU8(static_cast<uint8_t>(character))) return false;
        }
    }
    return writer.ok() && writer.offset() == PROJECT_TRACK_STATE_PAYLOAD_SIZE;
}

FLASHMEM bool readPayload(
    const uint8_t* data,
    uint32_t size,
    project::ProjectTrackSnapshot& pending
) {
    binary::Reader reader(data, size);
    for (uint8_t& channel : pending.midiChannels) {
        if (!reader.readU8(channel)) return false;
    }
    for (int16_t& delayMs : pending.delayMs) {
        if (!reader.readI16(delayMs)) return false;
    }
    if (!reader.readU16(pending.mutedMask) ||
        !reader.readU16(pending.soloMask)) {
        return false;
    }
    if (size == PROJECT_TRACK_STATE_PAYLOAD_SIZE) {
        for (auto& name : pending.names) {
            for (char& character : name) {
                uint8_t value = 0U;
                if (!reader.readU8(value)) return false;
                character = static_cast<char>(value);
            }
        }
    }
    return reader.ok() && reader.offset() == size;
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
        (versionMinor != 0U &&
         versionMinor != PROJECT_TRACK_CHUNK_VERSION_MINOR)) {
        return {.status = Status::UNSUPPORTED_VERSION};
    }
    const uint32_t expectedSize = versionMinor == 0U
        ? PROJECT_TRACK_LEGACY_PAYLOAD_SIZE
        : PROJECT_TRACK_STATE_PAYLOAD_SIZE;
    if (size != expectedSize) {
        return {.status = Status::INVALID_PAYLOAD_SIZE};
    }

    project::ProjectTrackSnapshot pending =
        project::defaultProjectTrackSnapshot();
    if (!readPayload(data, size, pending) || !validSnapshot(pending)) {
        return {.status = Status::INVALID_DOMAIN};
    }

    out = pending;
    return {.status = Status::OK};
}

}  // namespace core::persistence::project_track_codec

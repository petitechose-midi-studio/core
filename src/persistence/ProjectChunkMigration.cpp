#include "persistence/ProjectChunkMigration.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceBinaryCodec.hpp"
#include "persistence/ProjectStatePersistenceCodec.hpp"
#include "state/project/ProjectDomainRules.hpp"
#include "state/project/ProjectSlug.hpp"

namespace core::persistence::project_migration {

namespace {

namespace project_file = core::persistence::project_file;
namespace state_codec = core::persistence::project_state_codec;
namespace binary = core::persistence::binary_codec;

constexpr uint32_t TRANSPORT_V0_PAYLOAD_SIZE = 4;
constexpr uint32_t PROJECT_META_V1_0_PAYLOAD_SIZE = 48;

struct TransportV0Payload {
    uint16_t tempoBpm = 120;
    uint8_t swingPercent = 0;
};

struct ProjectMetaV1_0Payload {
    char id[16] = {};
    char name[24] = {};
    uint32_t modifiedCounter = 0;
    uint8_t flags = 0;
};

static_assert(TRANSPORT_V0_PAYLOAD_SIZE == 4, "Unexpected transport v0 payload size");
static_assert(PROJECT_META_V1_0_PAYLOAD_SIZE == 48,
              "Unexpected project meta v1.0 payload size");

constexpr uint8_t kMetaFlagHasSavedIdentity = 1U << 1U;

FLASHMEM size_t boundedLength(const char* text, size_t capacity) {
    if (text == nullptr || capacity == 0) return 0;
    size_t length = 0;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

FLASHMEM char toLowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

FLASHMEM bool v1GeneratedIdToSlug(const char* id, size_t length, char* out, size_t outSize) {
    if (id == nullptr || out == nullptr || length != 4) return false;
    if (id[0] != 'P') return false;
    if (id[1] < '0' || id[1] > '9' ||
        id[2] < '0' || id[2] > '9' ||
        id[3] < '0' || id[3] > '9') {
        return false;
    }
    const uint16_t index = static_cast<uint16_t>(
        (id[1] - '0') * 100 +
        (id[2] - '0') * 10 +
        (id[3] - '0')
    );
    return core::state::project::formatGeneratedProjectSlug(index, out, outSize);
}

FLASHMEM bool normalizeStoredSlug(const char* text,
                                   size_t length,
                                   char* out,
                                   size_t outSize) {
    if (text == nullptr || out == nullptr || outSize == 0) return false;
    out[0] = '\0';

    size_t write = 0;
    char previous = '\0';
    for (size_t i = 0; i < length && write + 1U < outSize; ++i) {
        char c = toLowerAscii(text[i]);
        if (c == ' ' || c == '_') c = '-';
        if (!core::state::project::isProjectSlugChar(c)) continue;
        if (write == 0 && c == '.') continue;
        if (c == '.' && previous == '.') continue;
        out[write++] = c;
        previous = c;
    }

    while (write > 0 && out[write - 1U] == '.') {
        --write;
    }
    out[write] = '\0';
    return core::state::project::validProjectSlug(out);
}

FLASHMEM void copyPayloadText(const char* source, char* target, size_t targetSize) {
    if (target == nullptr || targetSize == 0) return;
    std::memset(target, 0, targetSize);
    if (source == nullptr) return;
    std::strncpy(target, source, targetSize - 1U);
}

FLASHMEM bool decodeTransportV0Payload(const uint8_t* data,
                                       uint32_t size,
                                       TransportV0Payload& out) {
    if (size != TRANSPORT_V0_PAYLOAD_SIZE) return false;
    binary::Reader reader(data, size);
    return reader.readU16(out.tempoBpm) &&
           reader.readU8(out.swingPercent) &&
           reader.skip(1) &&
           reader.ok() &&
           reader.offset() == TRANSPORT_V0_PAYLOAD_SIZE;
}

FLASHMEM bool decodeProjectMetaV1_0Payload(const uint8_t* data,
                                           uint32_t size,
                                           ProjectMetaV1_0Payload& out) {
    if (size != PROJECT_META_V1_0_PAYLOAD_SIZE) return false;
    binary::Reader reader(data, size);
    return reader.readBytes(out.id, sizeof(out.id)) &&
           reader.readBytes(out.name, sizeof(out.name)) &&
           reader.readU32(out.modifiedCounter) &&
           reader.readU8(out.flags) &&
           reader.skip(3) &&
           reader.ok() &&
           reader.offset() == PROJECT_META_V1_0_PAYLOAD_SIZE;
}

FLASHMEM Result migrateTransportV0(const project_file::DecodedChunkView& chunk,
                                   uint8_t* out,
                                   uint32_t outCapacity) {
    if (chunk.size != TRANSPORT_V0_PAYLOAD_SIZE || chunk.data == nullptr) {
        return {.status = Status::INVALID_PAYLOAD, .bytesWritten = 0};
    }
    if (out == nullptr || outCapacity < state_codec::PROJECT_TRANSPORT_PAYLOAD_SIZE) {
        return {
            .status = Status::OUTPUT_TOO_SMALL,
            .bytesWritten = state_codec::PROJECT_TRANSPORT_PAYLOAD_SIZE,
        };
    }

    TransportV0Payload source{};
    if (!decodeTransportV0Payload(chunk.data, chunk.size, source)) {
        return {.status = Status::INVALID_PAYLOAD, .bytesWritten = 0};
    }

    state_codec::ProjectTransportPayload target{};
    target.tempoCentiBpm =
        core::state::project::projectTempoToCentiBpm(static_cast<float>(source.tempoBpm));
    target.swingPercent = core::state::project::sanitizeProjectSwingPercent(source.swingPercent);
    target.runMode = core::state::project::PROJECT_RUN_MODE_DEFAULT;

    if (!state_codec::encodeTransportPayload(
            target,
            out,
            state_codec::PROJECT_TRANSPORT_PAYLOAD_SIZE
        )) {
        return {.status = Status::INVALID_PAYLOAD, .bytesWritten = 0};
    }
    return {
        .status = Status::MIGRATED,
        .bytesWritten = state_codec::PROJECT_TRANSPORT_PAYLOAD_SIZE,
    };
}

FLASHMEM Result migrateProjectMetaV1_0(const project_file::DecodedChunkView& chunk,
                                       uint8_t* out,
                                       uint32_t outCapacity) {
    if (chunk.size != PROJECT_META_V1_0_PAYLOAD_SIZE || chunk.data == nullptr) {
        return {.status = Status::INVALID_PAYLOAD, .bytesWritten = 0};
    }
    if (out == nullptr || outCapacity < state_codec::PROJECT_META_PAYLOAD_SIZE) {
        return {
            .status = Status::OUTPUT_TOO_SMALL,
            .bytesWritten = state_codec::PROJECT_META_PAYLOAD_SIZE,
        };
    }

    ProjectMetaV1_0Payload source{};
    if (!decodeProjectMetaV1_0Payload(chunk.data, chunk.size, source)) {
        return {.status = Status::INVALID_PAYLOAD, .bytesWritten = 0};
    }

    char slug[core::state::project::PROJECT_SLUG_SIZE] = {};
    const size_t idLength = boundedLength(source.id, sizeof(source.id));
    const size_t nameLength = boundedLength(source.name, sizeof(source.name));
    if (!v1GeneratedIdToSlug(source.id, idLength, slug, sizeof(slug)) &&
        !normalizeStoredSlug(source.id, idLength, slug, sizeof(slug))) {
        normalizeStoredSlug(source.name, nameLength, slug, sizeof(slug));
    }

    state_codec::ProjectMetaPayload target{};
    target.modifiedCounter = source.modifiedCounter;
    target.flags = source.flags;

    if (core::state::project::validProjectSlug(slug)) {
        copyPayloadText(slug, target.id, sizeof(target.id));
        copyPayloadText(slug, target.name, sizeof(target.name));
    } else {
        copyPayloadText(
            core::state::project::DEFAULT_UNSAVED_PROJECT_SLUG,
            target.name,
            sizeof(target.name)
        );
        target.flags = static_cast<uint8_t>(target.flags & ~kMetaFlagHasSavedIdentity);
    }

    if (!state_codec::encodeMetaPayload(target, out, state_codec::PROJECT_META_PAYLOAD_SIZE)) {
        return {.status = Status::INVALID_PAYLOAD, .bytesWritten = 0};
    }
    return {
        .status = Status::MIGRATED,
        .bytesWritten = state_codec::PROJECT_META_PAYLOAD_SIZE,
    };
}

}  // namespace

FLASHMEM Result migrateToCurrent(const project_file::DecodedChunkView& chunk,
                                 uint8_t* out,
                                 uint32_t outCapacity) {
    if (chunk.id == project_file::chunkIdValue(project_file::ChunkId::PROJECT_META) &&
        chunk.versionMajor == 1 &&
        chunk.versionMinor == 0) {
        return migrateProjectMetaV1_0(chunk, out, outCapacity);
    }

    if (chunk.versionMajor != 0) {
        return {.status = Status::UNSUPPORTED, .bytesWritten = 0};
    }

    switch (static_cast<project_file::ChunkId>(chunk.id)) {
        case project_file::ChunkId::TRANSPORT:
            return migrateTransportV0(chunk, out, outCapacity);
        default:
            return {.status = Status::UNSUPPORTED, .bytesWritten = 0};
    }
}

}  // namespace core::persistence::project_migration

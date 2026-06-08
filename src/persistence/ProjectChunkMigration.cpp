#include "persistence/ProjectChunkMigration.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/ProjectStatePersistencePayloads.hpp"

namespace core::persistence::project_migration {

namespace {

namespace project_file = core::persistence::project_file;
namespace state_codec = core::persistence::project_state_codec;

#pragma pack(push, 1)
struct TransportV0Payload {
    uint16_t tempoBpm = 120;
    uint8_t swingPercent = 0;
    uint8_t reserved0 = 0;
};
#pragma pack(pop)

static_assert(sizeof(TransportV0Payload) == 4, "Unexpected TransportV0Payload size");

FLASHMEM uint16_t clampTempoBpm(uint16_t tempoBpm) {
    if (tempoBpm < 20U) return 20U;
    if (tempoBpm > 300U) return 300U;
    return tempoBpm;
}

FLASHMEM uint8_t clampSwing(uint8_t swingPercent) {
    return swingPercent > 75U ? 75U : swingPercent;
}

FLASHMEM Result migrateTransportV0(const project_file::DecodedChunkView& chunk,
                                   uint8_t* out,
                                   uint32_t outCapacity) {
    if (chunk.size != sizeof(TransportV0Payload) || chunk.data == nullptr) {
        return {.status = Status::INVALID_PAYLOAD, .bytesWritten = 0};
    }
    if (out == nullptr || outCapacity < sizeof(state_codec::ProjectTransportPayload)) {
        return {
            .status = Status::OUTPUT_TOO_SMALL,
            .bytesWritten = sizeof(state_codec::ProjectTransportPayload),
        };
    }

    TransportV0Payload source{};
    std::memcpy(&source, chunk.data, sizeof(source));

    state_codec::ProjectTransportPayload target{};
    target.tempoCentiBpm = static_cast<uint16_t>(clampTempoBpm(source.tempoBpm) * 100U);
    target.swingPercent = clampSwing(source.swingPercent);
    target.runMode = core::state::project::ProjectTransportState::DEFAULT_RUN_MODE;

    std::memcpy(out, &target, sizeof(target));
    return {.status = Status::MIGRATED, .bytesWritten = sizeof(target)};
}

}  // namespace

FLASHMEM Result migrateToCurrent(const project_file::DecodedChunkView& chunk,
                                 uint8_t* out,
                                 uint32_t outCapacity) {
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

#pragma once

#include <cstdint>

#include "persistence/ProjectControlPersistencePayloads.hpp"

namespace core::persistence::project_control_codec {

enum class Status : uint8_t {
    OK = 0,
    INVALID_ARGUMENT,
    INVALID_DOMAIN,
    BUFFER_TOO_SMALL,
    SCRATCH_ALLOCATION_FAILED,
};

struct EncodeResult {
    Status status = Status::INVALID_ARGUMENT;
    uint32_t bytesRequired = 0;
    uint32_t bytesWritten = 0;
    uint32_t automationOffset = 0;
    uint32_t automationSize = 0;
    uint32_t modulationOffset = 0;
    uint32_t modulationSize = 0;

    [[nodiscard]] bool encoded() const { return status == Status::OK; }
};

struct ChunkPayloadView {
    bool present = false;
    uint8_t versionMajor = 0;
    uint8_t versionMinor = 0;
    uint16_t flags = 0;
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

enum class ChunkStatus : uint8_t {
    MISSING = 0,
    CURRENT,
    UNSUPPORTED_VERSION,
    INVALID_PAYLOAD,
    CAPACITY_EXCEEDED,
};

struct DecodeResult {
    Status status = Status::INVALID_ARGUMENT;
    ChunkStatus automationStatus = ChunkStatus::MISSING;
    ChunkStatus modulationStatus = ChunkStatus::MISSING;
    bool partial = false;
    bool overwriteSafe = true;

    [[nodiscard]] bool decoded() const { return status == Status::OK; }
};

/**
 * Writes canonical MAUT 1.6 then MODG 1.5 into one caller-owned buffer.
 * Capacity is preflighted before the first byte is changed.
 */
[[nodiscard]] EncodeResult encodeProjectControlPayloads(
    const core::state::modulation::ProjectControlDomainState& source,
    uint8_t* out,
    uint32_t outCapacity
);

/**
 * Decodes only the current control payloads into one temporary EXTMEM domain,
 * then publishes once. The two current chunks recover independently.
 */
[[nodiscard]] DecodeResult decodeProjectControlPayloads(
    const ChunkPayloadView& automation,
    const ChunkPayloadView& modulation,
    core::state::modulation::ProjectControlDomainState& out
);

}  // namespace core::persistence::project_control_codec

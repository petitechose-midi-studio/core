#pragma once

#include <cstdint>

#include "state/project/ProjectTrackState.hpp"

namespace core::persistence::project_track_codec {

inline constexpr uint8_t PROJECT_TRACK_CHUNK_VERSION_MAJOR = 1U;
inline constexpr uint8_t PROJECT_TRACK_CHUNK_VERSION_MINOR = 0U;

inline constexpr uint32_t PROJECT_TRACK_CHANNELS_PAYLOAD_SIZE =
    core::state::project::PROJECT_TRACK_COUNT;
inline constexpr uint32_t PROJECT_TRACK_DELAYS_PAYLOAD_SIZE =
    core::state::project::PROJECT_TRACK_COUNT * sizeof(int16_t);
inline constexpr uint32_t PROJECT_TRACK_MASKS_PAYLOAD_SIZE = 2U * sizeof(uint16_t);
inline constexpr uint32_t PROJECT_TRACK_STATE_PAYLOAD_SIZE =
    PROJECT_TRACK_CHANNELS_PAYLOAD_SIZE +
    PROJECT_TRACK_DELAYS_PAYLOAD_SIZE +
    PROJECT_TRACK_MASKS_PAYLOAD_SIZE;

static_assert(PROJECT_TRACK_STATE_PAYLOAD_SIZE == 52U);

enum class Status : uint8_t {
    OK = 0,
    INVALID_ARGUMENT,
    UNSUPPORTED_VERSION,
    BUFFER_TOO_SMALL,
    INVALID_PAYLOAD_SIZE,
    INVALID_DOMAIN,
};

struct EncodeResult {
    Status status = Status::INVALID_ARGUMENT;
    uint32_t bytesRequired = PROJECT_TRACK_STATE_PAYLOAD_SIZE;
    uint32_t bytesWritten = 0U;

    [[nodiscard]] bool encoded() const { return status == Status::OK; }
};

struct DecodeResult {
    Status status = Status::INVALID_ARGUMENT;

    [[nodiscard]] bool decoded() const { return status == Status::OK; }
};

/**
 * Encodes the canonical TRKS 1.0 payload.
 *
 * The output is not modified unless the snapshot is valid and the complete
 * 52-byte payload fits in the caller-provided buffer.
 */
[[nodiscard]] EncodeResult encodeProjectTrackStatePayload(
    const core::state::project::ProjectTrackSnapshot& source,
    uint8_t* out,
    uint32_t outCapacity
);

/**
 * Decodes an exact TRKS 1.0 payload transactionally.
 *
 * Version, size and every bounded value are validated before publishing the
 * result. `out` therefore remains byte-for-byte unchanged on every failure.
 */
[[nodiscard]] DecodeResult decodeProjectTrackStatePayload(
    const uint8_t* data,
    uint32_t size,
    uint8_t versionMajor,
    uint8_t versionMinor,
    core::state::project::ProjectTrackSnapshot& out
);

}  // namespace core::persistence::project_track_codec

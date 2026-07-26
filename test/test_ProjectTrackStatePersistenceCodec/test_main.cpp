#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "persistence/ProjectFileContainer.hpp"
#include "persistence/ProjectTrackStatePersistenceCodec.hpp"

namespace {

namespace codec = core::persistence::project_track_codec;
namespace project = core::state::project;
namespace project_file = core::persistence::project_file;

using Payload = std::array<uint8_t, codec::PROJECT_TRACK_STATE_PAYLOAD_SIZE>;

bool sameSnapshot(
    const project::ProjectTrackSnapshot& lhs,
    const project::ProjectTrackSnapshot& rhs
) {
    return lhs.midiChannels == rhs.midiChannels &&
           lhs.delayMs == rhs.delayMs &&
           lhs.mutedMask == rhs.mutedMask &&
           lhs.soloMask == rhs.soloMask;
}

project::ProjectTrackSnapshot makeSnapshot() {
    auto snapshot = project::defaultProjectTrackSnapshot();
    for (uint8_t track = 0U; track < project::PROJECT_TRACK_COUNT; ++track) {
        snapshot.midiChannels[track] =
            static_cast<uint8_t>(project::PROJECT_TRACK_COUNT - 1U - track);
        snapshot.delayMs[track] = static_cast<int16_t>(
            -45 + static_cast<int16_t>(track) * 6
        );
    }
    snapshot.delayMs.front() = project::PROJECT_TRACK_DELAY_MIN_MS;
    snapshot.delayMs.back() = project::PROJECT_TRACK_DELAY_MAX_MS;
    snapshot.mutedMask = 0xA55AU;
    snapshot.soloMask = 0x5AA5U;
    return snapshot;
}

project::ProjectTrackSnapshot makeSentinel() {
    project::ProjectTrackSnapshot snapshot{};
    snapshot.midiChannels.fill(7U);
    snapshot.delayMs.fill(37);
    snapshot.mutedMask = 0x1357U;
    snapshot.soloMask = 0x2468U;
    return snapshot;
}

Payload encode(const project::ProjectTrackSnapshot& snapshot) {
    Payload bytes{};
    const auto result = codec::encodeProjectTrackStatePayload(
        snapshot,
        bytes.data(),
        static_cast<uint32_t>(bytes.size())
    );
    assert(result.encoded());
    assert(result.bytesRequired == bytes.size());
    assert(result.bytesWritten == bytes.size());
    return bytes;
}

void assertDecodeFailureDoesNotMutate(
    const uint8_t* data,
    uint32_t size,
    uint8_t versionMajor,
    uint8_t versionMinor,
    codec::Status expectedStatus
) {
    auto out = makeSentinel();
    const auto before = out;
    const auto result = codec::decodeProjectTrackStatePayload(
        data,
        size,
        versionMajor,
        versionMinor,
        out
    );
    assert(result.status == expectedStatus);
    assert(!result.decoded());
    assert(sameSnapshot(out, before));
}

void testContractAndExplicitLittleEndianLayout() {
    static_assert(codec::PROJECT_TRACK_CHUNK_VERSION_MAJOR == 1U);
    static_assert(codec::PROJECT_TRACK_CHUNK_VERSION_MINOR == 0U);
    static_assert(codec::PROJECT_TRACK_STATE_PAYLOAD_SIZE == 52U);
    static_assert(
        project_file::chunkIdValue(project_file::ChunkId::TRACK_STATE) ==
        0x54524B53U
    );
    assert(project_file::isKnownChunkId(
        project_file::chunkIdValue(project_file::ChunkId::TRACK_STATE)
    ));

    const auto source = makeSnapshot();
    std::array<uint8_t, codec::PROJECT_TRACK_STATE_PAYLOAD_SIZE + 1U> bytes{};
    bytes.fill(0xEEU);
    const auto result = codec::encodeProjectTrackStatePayload(
        source,
        bytes.data(),
        static_cast<uint32_t>(bytes.size())
    );
    assert(result.encoded());
    assert(result.bytesRequired == codec::PROJECT_TRACK_STATE_PAYLOAD_SIZE);
    assert(result.bytesWritten == codec::PROJECT_TRACK_STATE_PAYLOAD_SIZE);
    assert(bytes.back() == 0xEEU);

    for (uint8_t track = 0U; track < project::PROJECT_TRACK_COUNT; ++track) {
        assert(bytes[track] == source.midiChannels[track]);
        const uint32_t offset = codec::PROJECT_TRACK_CHANNELS_PAYLOAD_SIZE +
            static_cast<uint32_t>(track) * 2U;
        const uint16_t raw = static_cast<uint16_t>(source.delayMs[track]);
        assert(bytes[offset] == static_cast<uint8_t>(raw & 0xFFU));
        assert(bytes[offset + 1U] == static_cast<uint8_t>(raw >> 8U));
    }

    constexpr uint32_t masksOffset =
        codec::PROJECT_TRACK_CHANNELS_PAYLOAD_SIZE +
        codec::PROJECT_TRACK_DELAYS_PAYLOAD_SIZE;
    assert(bytes[masksOffset] == 0x5AU);
    assert(bytes[masksOffset + 1U] == 0xA5U);
    assert(bytes[masksOffset + 2U] == 0xA5U);
    assert(bytes[masksOffset + 3U] == 0x5AU);
}

void testRoundTripPublishesCompleteSnapshot() {
    const auto source = makeSnapshot();
    const auto bytes = encode(source);
    auto out = makeSentinel();

    const auto result = codec::decodeProjectTrackStatePayload(
        bytes.data(),
        static_cast<uint32_t>(bytes.size()),
        codec::PROJECT_TRACK_CHUNK_VERSION_MAJOR,
        codec::PROJECT_TRACK_CHUNK_VERSION_MINOR,
        out
    );
    assert(result.decoded());
    assert(sameSnapshot(out, source));
}

void testDecodeRejectsVersionSizeAndNullTransactionally() {
    const auto bytes = encode(makeSnapshot());
    assertDecodeFailureDoesNotMutate(
        bytes.data(),
        static_cast<uint32_t>(bytes.size()),
        2U,
        0U,
        codec::Status::UNSUPPORTED_VERSION
    );
    assertDecodeFailureDoesNotMutate(
        bytes.data(),
        static_cast<uint32_t>(bytes.size()),
        1U,
        1U,
        codec::Status::UNSUPPORTED_VERSION
    );
    assertDecodeFailureDoesNotMutate(
        bytes.data(),
        static_cast<uint32_t>(bytes.size() - 1U),
        1U,
        0U,
        codec::Status::INVALID_PAYLOAD_SIZE
    );
    assertDecodeFailureDoesNotMutate(
        bytes.data(),
        static_cast<uint32_t>(bytes.size() + 1U),
        1U,
        0U,
        codec::Status::INVALID_PAYLOAD_SIZE
    );
    assertDecodeFailureDoesNotMutate(
        nullptr,
        static_cast<uint32_t>(bytes.size()),
        1U,
        0U,
        codec::Status::INVALID_ARGUMENT
    );
}

void testDecodeRejectsEveryBoundedDomainWithoutPartialPublish() {
    const auto valid = encode(makeSnapshot());

    auto invalidChannel = valid;
    invalidChannel[3U] = 16U;
    assertDecodeFailureDoesNotMutate(
        invalidChannel.data(),
        static_cast<uint32_t>(invalidChannel.size()),
        1U,
        0U,
        codec::Status::INVALID_DOMAIN
    );

    auto delayAboveMaximum = valid;
    constexpr uint32_t delayOffset = codec::PROJECT_TRACK_CHANNELS_PAYLOAD_SIZE;
    delayAboveMaximum[delayOffset] = 101U;
    delayAboveMaximum[delayOffset + 1U] = 0U;
    assertDecodeFailureDoesNotMutate(
        delayAboveMaximum.data(),
        static_cast<uint32_t>(delayAboveMaximum.size()),
        1U,
        0U,
        codec::Status::INVALID_DOMAIN
    );

    auto delayBelowMinimum = valid;
    delayBelowMinimum[delayOffset] = 0x9BU;      // -101, little-endian.
    delayBelowMinimum[delayOffset + 1U] = 0xFFU;
    assertDecodeFailureDoesNotMutate(
        delayBelowMinimum.data(),
        static_cast<uint32_t>(delayBelowMinimum.size()),
        1U,
        0U,
        codec::Status::INVALID_DOMAIN
    );
}

void testEncodePreflightsWithoutTouchingOutput() {
    const auto source = makeSnapshot();
    Payload output{};
    output.fill(0xA5U);
    const auto untouched = output;

    const auto tooSmall = codec::encodeProjectTrackStatePayload(
        source,
        output.data(),
        codec::PROJECT_TRACK_STATE_PAYLOAD_SIZE - 1U
    );
    assert(tooSmall.status == codec::Status::BUFFER_TOO_SMALL);
    assert(output == untouched);

    auto invalidChannel = source;
    invalidChannel.midiChannels[4U] = 16U;
    const auto badChannel = codec::encodeProjectTrackStatePayload(
        invalidChannel,
        output.data(),
        static_cast<uint32_t>(output.size())
    );
    assert(badChannel.status == codec::Status::INVALID_DOMAIN);
    assert(output == untouched);

    auto invalidDelay = source;
    invalidDelay.delayMs[4U] = 101;
    const auto badDelay = codec::encodeProjectTrackStatePayload(
        invalidDelay,
        output.data(),
        static_cast<uint32_t>(output.size())
    );
    assert(badDelay.status == codec::Status::INVALID_DOMAIN);
    assert(output == untouched);

    const auto nullOutput = codec::encodeProjectTrackStatePayload(
        source,
        nullptr,
        static_cast<uint32_t>(output.size())
    );
    assert(nullOutput.status == codec::Status::INVALID_ARGUMENT);
}

}  // namespace

int main() {
    testContractAndExplicitLittleEndianLayout();
    testRoundTripPublishesCompleteSnapshot();
    testDecodeRejectsVersionSizeAndNullTransactionally();
    testDecodeRejectsEveryBoundedDomainWithoutPartialPublish();
    testEncodePreflightsWithoutTouchingOutput();
    std::cout << "Project Track persistence codec tests passed\n";
    return 0;
}

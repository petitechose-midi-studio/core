#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "protocol/filesystem/FileSystemJobRpc.hpp"
#include "protocol/filesystem/FileSystemRpc.hpp"

namespace {

using namespace core::protocol::filesystem;

void appendU16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
    bytes.push_back(static_cast<uint8_t>(value >> 16U));
    bytes.push_back(static_cast<uint8_t>(value >> 24U));
}

std::vector<uint8_t> legacyFrame(
    FileSystemRpcMessageId messageId,
    std::vector<uint8_t> payload
) {
    const char* name = FileSystemRpcCodec::messageName(messageId);
    const size_t nameSize = std::strlen(name);
    std::vector<uint8_t> frame;
    frame.reserve(5U + nameSize + payload.size());
    frame.push_back(static_cast<uint8_t>(messageId));
    frame.push_back(static_cast<uint8_t>(nameSize));
    frame.insert(frame.end(), name, name + nameSize);
    frame.push_back(FILESYSTEM_RPC_SCHEMA);
    appendU16(frame, 1U);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

std::vector<uint8_t> jobRequest() {
    std::vector<uint8_t> frame;
    constexpr size_t nameSize = sizeof(FILESYSTEM_JOB_RPC_REQUEST_NAME) - 1U;
    frame.push_back(FILESYSTEM_JOB_RPC_REQUEST_ID);
    frame.push_back(static_cast<uint8_t>(nameSize));
    frame.insert(frame.end(), FILESYSTEM_JOB_RPC_REQUEST_NAME,
                 FILESYSTEM_JOB_RPC_REQUEST_NAME + nameSize);
    frame.push_back(FILESYSTEM_JOB_RPC_SCHEMA);
    appendU16(frame, 1U);
    frame.push_back(static_cast<uint8_t>(FileSystemJobCommand::CAPABILITIES));
    frame.push_back(0U);
    appendU16(frame, 0U);
    appendU32(frame, 0U);
    appendU32(frame, 0U);
    appendU32(frame, 0U);
    return frame;
}

std::vector<uint8_t> jobResponse() {
    std::array<uint8_t, 128U> bytes{};
    const auto encoded = FileSystemJobRpcCodec::encodeResponse(
        FileSystemJobResponse{}, bytes.data(), bytes.size());
    return encoded
        ? std::vector<uint8_t>(bytes.begin(), bytes.begin() + encoded.value())
        : std::vector<uint8_t>{};
}

const std::array<std::vector<uint8_t>, 10U>& canonicalSeeds() {
    static const auto seeds = [] {
        std::array<std::vector<uint8_t>, 10U> result{};

        std::vector<uint8_t> stat{
            static_cast<uint8_t>(FileSystemRpcStatus::OK),
            static_cast<uint8_t>(FileSystemRpcFileType::FILE),
        };
        appendU32(stat, 42U);
        result[0] = legacyFrame(FileSystemRpcMessageId::STAT_RESPONSE, std::move(stat));

        std::vector<uint8_t> list{
            static_cast<uint8_t>(FileSystemRpcStatus::OK), 0U, 0U, 1U, 0U,
            1U, 'x', static_cast<uint8_t>(FileSystemRpcFileType::FILE),
        };
        appendU32(list, 1U);
        list.push_back(0U);
        result[1] = legacyFrame(FileSystemRpcMessageId::LIST_RESPONSE, std::move(list));

        std::vector<uint8_t> read{
            static_cast<uint8_t>(FileSystemRpcStatus::OK),
        };
        appendU32(read, 7U);
        appendU16(read, 1U);
        read.push_back(0xABU);
        result[2] = legacyFrame(FileSystemRpcMessageId::READ_RESPONSE, std::move(read));

        std::vector<uint8_t> write{
            static_cast<uint8_t>(FileSystemRpcStatus::OK),
        };
        appendU16(write, 3U);
        appendU16(write, 4U);
        result[3] = legacyFrame(
            FileSystemRpcMessageId::WRITE_CHUNK_RESPONSE, std::move(write));

        result[4] = legacyFrame(
            FileSystemRpcMessageId::DELETE_RESPONSE,
            {static_cast<uint8_t>(FileSystemRpcStatus::OK)});

        std::vector<uint8_t> capabilities{
            static_cast<uint8_t>(FileSystemRpcStatus::OK),
            FILESYSTEM_RPC_SCHEMA,
        };
        appendU16(capabilities, FILESYSTEM_RPC_MAX_CHUNK_SIZE);
        appendU16(capabilities, FILESYSTEM_RPC_RESPONSE_BUFFER_SIZE);
        capabilities.push_back(FILESYSTEM_RPC_MAX_LIST_ENTRIES);
        appendU16(capabilities, oc::interface::FILESYSTEM_MAX_PATH_LENGTH);
        appendU32(capabilities, 0U);
        result[5] = legacyFrame(
            FileSystemRpcMessageId::CAPABILITIES_RESPONSE,
            std::move(capabilities));

        std::vector<uint8_t> mutation{
            static_cast<uint8_t>(FileSystemRpcStatus::OK),
            static_cast<uint8_t>(FileSystemRpcMutationOutcome::APPLIED),
            static_cast<uint8_t>(FileSystemRpcMutationSubject::NONE),
        };
        appendU32(mutation, 9U);
        mutation.resize(mutation.size() + FILESYSTEM_RPC_SHA256_SIZE, 0U);
        result[6] = legacyFrame(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            std::move(mutation));

        result[7] = legacyFrame(
            FileSystemRpcMessageId::DELETE_REQUEST,
            {1U, 'x', 0U});
        result[8] = jobRequest();
        result[9] = jobResponse();
        return result;
    }();
    return seeds;
}

void exercise(const uint8_t* data, size_t size) {

    (void)FileSystemJobRpcCodec::isSupportedStartRequest(data, size);
    (void)FileSystemJobRpcCodec::isCanonicalLegacyResponse(data, size);
    (void)FileSystemJobRpcCodec::decodeRequestHeader(data, size);
    (void)FileSystemJobRpcCodec::decodeRequest(data, size);
    (void)FileSystemJobRpcCodec::decodeResponse(data, size);
    (void)FileSystemRpcCodec::decodeFrame(data, size);
    (void)FileSystemRpcCodec::decodeStatResponse(data, size);
    (void)FileSystemRpcCodec::decodeListResponse(data, size);
    (void)FileSystemRpcCodec::decodeReadResponse(data, size);
    (void)FileSystemRpcCodec::decodeWriteResponse(data, size);
    (void)FileSystemRpcCodec::decodeStatusResponse(data, size);
    (void)FileSystemRpcCodec::decodeCapabilitiesResponse(data, size);
    (void)FileSystemRpcCodec::decodeConditionalMutationResponse(data, size);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    exercise(data, size);

    const auto& seeds = canonicalSeeds();
    std::vector<uint8_t> candidate = seeds[size == 0U ? 0U : data[0] % seeds.size()];
    if (!candidate.empty()) {
        for (size_t i = 1U; i < size; ++i) {
            candidate[(i - 1U) % candidate.size()] ^= data[i];
        }
        exercise(candidate.data(), candidate.size());
    }
    return 0;
}

#include "protocol/filesystem/FileSystemRpcDigest.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceChecksum.hpp"
#include "protocol/filesystem/FileSystemRpcInternal.hpp"

namespace core::protocol::filesystem::conditional_mutation {

namespace {

// Seven SHA-256 blocks keep streaming aligned while leaving enough DTCM stack
// headroom for the exact mutation lease and stat guards. Filesystem RPC is a
// cold control path; the modest extra read iteration protects the realtime
// stack without affecting musical work.
constexpr size_t HASH_READ_BUFFER_SIZE = 7U * 64U;
static_assert(HASH_READ_BUFFER_SIZE == 448U);

constexpr uint32_t rotateRight(uint32_t value, uint8_t count) {
    return (value >> count) | (value << (32U - count));
}

}  // namespace

FLASHMEM void DigestReadPlan::begin() {
    static constexpr uint32_t initialState[8] PROGMEM = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    std::memcpy(state_, initialState, sizeof(state_));
    std::memset(block_, 0, sizeof(block_));
    std::memset(digest_, 0, sizeof(digest_));
    block_size_ = 0U;
    total_bytes_ = 0U;
    file_size_ = 0U;
    offset_ = 0U;
    crc32_state_ = core::persistence::checksum::CRC32_INITIAL_STATE;
    status_ = FileSystemRpcStatus::STORAGE_ERROR;
    step_ = Step::STAT;
}

FLASHMEM bool DigestReadPlan::advance(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const char* path,
    uint8_t* scratch,
    size_t scratchSize
) {
    switch (step_) {
        case Step::STAT: {
            auto info = files.stat(lease, path);
            if (!info) return fail_(internal::mapError(info.error()));
            if (info.value().type != oc::interface::FileType::FILE) {
                return fail_(FileSystemRpcStatus::INVALID_ARGUMENT);
            }
            file_size_ = info.value().sizeBytes;
            step_ = file_size_ == 0U ? Step::VERIFY_STAT : Step::READ;
            return false;
        }
        case Step::READ: {
            if (!scratch || scratchSize == 0U) {
                return fail_(FileSystemRpcStatus::INVALID_ARGUMENT);
            }
            const size_t remaining = static_cast<size_t>(file_size_ - offset_);
            size_t requested = remaining < scratchSize ? remaining : scratchSize;
            if (requested > FILESYSTEM_RPC_MAX_CHUNK_SIZE) {
                requested = FILESYSTEM_RPC_MAX_CHUNK_SIZE;
            }
            auto read = files.read(lease, path, offset_, scratch, requested);
            if (!read) return fail_(internal::mapError(read.error()));
            if (read.value() == 0U || read.value() > requested) {
                return fail_(FileSystemRpcStatus::STORAGE_ERROR);
            }
            update_(scratch, read.value());
            offset_ += static_cast<uint32_t>(read.value());
            if (offset_ == file_size_) step_ = Step::VERIFY_STAT;
            return false;
        }
        case Step::VERIFY_STAT: {
            auto info = files.stat(lease, path);
            if (!info) return fail_(internal::mapError(info.error()));
            if (info.value().type != oc::interface::FileType::FILE ||
                info.value().sizeBytes != file_size_) {
                return fail_(FileSystemRpcStatus::PRECONDITION_FAILED);
            }
            finish_();
            status_ = FileSystemRpcStatus::OK;
            step_ = Step::COMPLETE;
            return true;
        }
        case Step::COMPLETE:
            return true;
        case Step::IDLE:
        default:
            return fail_(FileSystemRpcStatus::INVALID_STATE);
    }
}

bool DigestReadPlan::complete() const {
    return step_ == Step::COMPLETE;
}

bool DigestReadPlan::nextAdvanceReadsData() const {
    return step_ == Step::READ;
}

FLASHMEM void DigestReadPlan::update_(const uint8_t* data, size_t size) {
    if (!data || size == 0U) return;
    crc32_state_ = core::persistence::checksum::crc32Update(
        crc32_state_,
        data,
        size
    );
    total_bytes_ += size;
    while (size > 0U) {
        const size_t available = sizeof(block_) - block_size_;
        const size_t copied = size < available ? size : available;
        std::memcpy(block_ + block_size_, data, copied);
        block_size_ += copied;
        data += copied;
        size -= copied;
        if (block_size_ == sizeof(block_)) {
            transform_(block_);
            block_size_ = 0U;
        }
    }
}

FLASHMEM void DigestReadPlan::finish_() {
    crc32_state_ = core::persistence::checksum::crc32Finish(crc32_state_);
    const uint64_t bitLength = total_bytes_ * 8ULL;
    block_[block_size_++] = 0x80;
    if (block_size_ > 56U) {
        while (block_size_ < sizeof(block_)) block_[block_size_++] = 0U;
        transform_(block_);
        block_size_ = 0U;
    }
    while (block_size_ < 56U) block_[block_size_++] = 0U;
    for (uint8_t i = 0U; i < 8U; ++i) {
        block_[63U - i] = static_cast<uint8_t>(bitLength >> (i * 8U));
    }
    transform_(block_);
    for (uint8_t word = 0U; word < 8U; ++word) {
        digest_[word * 4U] = static_cast<uint8_t>(state_[word] >> 24U);
        digest_[word * 4U + 1U] = static_cast<uint8_t>(state_[word] >> 16U);
        digest_[word * 4U + 2U] = static_cast<uint8_t>(state_[word] >> 8U);
        digest_[word * 4U + 3U] = static_cast<uint8_t>(state_[word]);
    }
}

FLASHMEM void DigestReadPlan::transform_(const uint8_t block[64]) {
    static constexpr uint32_t roundConstants[64] PROGMEM = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };

    uint32_t words[64] = {};
    for (uint8_t i = 0U; i < 16U; ++i) {
        const size_t offset = i * 4U;
        words[i] = (static_cast<uint32_t>(block[offset]) << 24U) |
                   (static_cast<uint32_t>(block[offset + 1U]) << 16U) |
                   (static_cast<uint32_t>(block[offset + 2U]) << 8U) |
                   static_cast<uint32_t>(block[offset + 3U]);
    }
    for (uint8_t i = 16U; i < 64U; ++i) {
        const uint32_t s0 = rotateRight(words[i - 15U], 7U) ^
                            rotateRight(words[i - 15U], 18U) ^
                            (words[i - 15U] >> 3U);
        const uint32_t s1 = rotateRight(words[i - 2U], 17U) ^
                            rotateRight(words[i - 2U], 19U) ^
                            (words[i - 2U] >> 10U);
        words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];
    for (uint8_t i = 0U; i < 64U; ++i) {
        const uint32_t sum1 =
            rotateRight(e, 6U) ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + sum1 + choose + roundConstants[i] + words[i];
        const uint32_t sum0 =
            rotateRight(a, 2U) ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

FLASHMEM bool DigestReadPlan::fail_(FileSystemRpcStatus status) {
    status_ = status;
    step_ = Step::COMPLETE;
    return true;
}

FLASHMEM bool digestEquals(const uint8_t* lhs, const uint8_t* rhs) {
    uint8_t difference = 0;
    for (size_t i = 0; i < FILESYSTEM_RPC_SHA256_SIZE; ++i) {
        difference |= static_cast<uint8_t>(lhs[i] ^ rhs[i]);
    }
    return difference == 0;
}

FLASHMEM void copyDigest(uint8_t* destination, const uint8_t* source) {
    std::memcpy(destination, source, FILESYSTEM_RPC_SHA256_SIZE);
}

FLASHMEM bool hashBytes(const uint8_t* data, size_t size,
                        uint8_t output[FILESYSTEM_RPC_SHA256_SIZE]) {
    if ((!data && size != 0U) || !output) return false;
    DigestReadPlan plan;
    plan.begin();
    plan.update_(data, size);
    plan.finish_();
    copyDigest(output, plan.digest_);
    return true;
}

FLASHMEM DigestReadResult readDigest(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const char* path
) {
    DigestReadResult result{};
    DigestReadPlan plan;
    uint8_t buffer[HASH_READ_BUFFER_SIZE] = {};
    plan.begin();
    while (!plan.complete()) {
        (void)plan.advance(files, lease, path, buffer, sizeof(buffer));
    }
    result.status = plan.status();
    if (result.status == FileSystemRpcStatus::OK) {
        copyDigest(result.sha256, plan.digest());
        result.crc32 = plan.crc32();
    }
    return result;
}

}  // namespace core::protocol::filesystem::conditional_mutation

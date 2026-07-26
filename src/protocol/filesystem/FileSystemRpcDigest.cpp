#include "protocol/filesystem/FileSystemRpcDigest.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "protocol/filesystem/FileSystemRpcInternal.hpp"

namespace core::protocol::filesystem::conditional_mutation {

namespace {

constexpr size_t HASH_READ_BUFFER_SIZE = 512;

constexpr uint32_t rotateRight(uint32_t value, uint8_t count) {
    return (value >> count) | (value << (32U - count));
}

class Sha256 final {
public:
    void update(const uint8_t* data, size_t size) {
        if (!data || size == 0) return;
        totalBytes_ += size;
        while (size > 0) {
            const size_t available = sizeof(block_) - blockSize_;
            const size_t copied = size < available ? size : available;
            std::memcpy(block_ + blockSize_, data, copied);
            blockSize_ += copied;
            data += copied;
            size -= copied;
            if (blockSize_ == sizeof(block_)) {
                transform_(block_);
                blockSize_ = 0;
            }
        }
    }

    void finish(uint8_t out[FILESYSTEM_RPC_SHA256_SIZE]) {
        const uint64_t bitLength = totalBytes_ * 8ULL;
        block_[blockSize_++] = 0x80;
        if (blockSize_ > 56) {
            while (blockSize_ < sizeof(block_)) block_[blockSize_++] = 0;
            transform_(block_);
            blockSize_ = 0;
        }
        while (blockSize_ < 56) block_[blockSize_++] = 0;
        for (uint8_t i = 0; i < 8; ++i) {
            block_[63U - i] = static_cast<uint8_t>(bitLength >> (i * 8U));
        }
        transform_(block_);
        for (uint8_t word = 0; word < 8; ++word) {
            out[word * 4U] = static_cast<uint8_t>(state_[word] >> 24U);
            out[word * 4U + 1U] = static_cast<uint8_t>(state_[word] >> 16U);
            out[word * 4U + 2U] = static_cast<uint8_t>(state_[word] >> 8U);
            out[word * 4U + 3U] = static_cast<uint8_t>(state_[word]);
        }
    }

private:
    FLASHMEM void transform_(const uint8_t block[64]) {
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
        for (uint8_t i = 0; i < 16; ++i) {
            const size_t offset = i * 4U;
            words[i] = (static_cast<uint32_t>(block[offset]) << 24U) |
                       (static_cast<uint32_t>(block[offset + 1U]) << 16U) |
                       (static_cast<uint32_t>(block[offset + 2U]) << 8U) |
                       static_cast<uint32_t>(block[offset + 3U]);
        }
        for (uint8_t i = 16; i < 64; ++i) {
            const uint32_t s0 = rotateRight(words[i - 15U], 7) ^
                                rotateRight(words[i - 15U], 18) ^
                                (words[i - 15U] >> 3U);
            const uint32_t s1 = rotateRight(words[i - 2U], 17) ^
                                rotateRight(words[i - 2U], 19) ^
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
        for (uint8_t i = 0; i < 64; ++i) {
            const uint32_t sum1 =
                rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const uint32_t choose = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + sum1 + choose + roundConstants[i] + words[i];
            const uint32_t sum0 =
                rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
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

    uint32_t state_[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    uint8_t block_[64] = {};
    size_t blockSize_ = 0;
    uint64_t totalBytes_ = 0;
};

}  // namespace

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

FLASHMEM DigestReadResult readDigest(
    core::persistence::ProductFileService& files,
    const char* path
) {
    DigestReadResult result{};
    auto before = files.stat(path);
    if (!before) {
        result.status = internal::mapError(before.error());
        return result;
    }
    if (before.value().type != oc::interface::FileType::FILE) {
        result.status = FileSystemRpcStatus::INVALID_ARGUMENT;
        return result;
    }

    Sha256 sha256;
    uint8_t buffer[HASH_READ_BUFFER_SIZE] = {};
    uint32_t offset = 0;
    while (offset < before.value().sizeBytes) {
        const size_t remaining = before.value().sizeBytes - offset;
        const size_t requested = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        auto read = files.read(path, offset, buffer, requested);
        if (!read) {
            result.status = internal::mapError(read.error());
            return result;
        }
        if (read.value() == 0 || read.value() > requested) {
            result.status = FileSystemRpcStatus::STORAGE_ERROR;
            return result;
        }
        sha256.update(buffer, read.value());
        offset += static_cast<uint32_t>(read.value());
    }

    auto after = files.stat(path);
    if (!after) {
        result.status = internal::mapError(after.error());
        return result;
    }
    if (after.value().type != oc::interface::FileType::FILE ||
        after.value().sizeBytes != before.value().sizeBytes) {
        result.status = FileSystemRpcStatus::PRECONDITION_FAILED;
        return result;
    }
    sha256.finish(result.sha256);
    result.status = FileSystemRpcStatus::OK;
    return result;
}

}  // namespace core::protocol::filesystem::conditional_mutation

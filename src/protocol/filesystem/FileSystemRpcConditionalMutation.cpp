#include "protocol/filesystem/FileSystemRpcInternal.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::protocol::filesystem {

using oc::type::ErrorCode;
using oc::type::Result;
using internal::ByteReader;
using internal::ByteWriter;
using internal::bufferTooSmall;
using internal::mapError;
using internal::readPath;
using internal::writeFrameHeader;

namespace {

constexpr const char* JOURNAL_PATH = "tmp/rpc-conditional.journal";
constexpr const char* JOURNAL_STAGING_PATH = "tmp/rpc-conditional.journal.tmp";
constexpr const char* BACKUP_PATH = "tmp/rpc-conditional.backup";
constexpr const char* JOURNAL_QUARANTINE_PATH =
    FILESYSTEM_RPC_CONDITIONAL_JOURNAL_QUARANTINE_PATH;
constexpr const char* RESOLVED_TMP_PREFIX = "/midi-studio/tmp/";
constexpr const char* RESOLVED_PROTOCOL_TMP_PREFIX = "/midi-studio/tmp/rpc-";
constexpr uint8_t JOURNAL_VERSION = 1;
constexpr size_t JOURNAL_BUFFER_SIZE = 512;
constexpr size_t HASH_READ_BUFFER_SIZE = 512;
constexpr uint8_t JOURNAL_MAGIC[] = {'F', 'S', 'T', 'X'};

enum class ConditionalMutationKind : uint8_t {
    REPLACE = 1,
    DELETE = 2,
};

struct ConditionalMutationJournal {
    ConditionalMutationKind kind = ConditionalMutationKind::REPLACE;
    uint32_t operationId = 0;
    uint8_t expectedSourceSha256[FILESYSTEM_RPC_SHA256_SIZE] = {};
    uint8_t replacementSha256[FILESYSTEM_RPC_SHA256_SIZE] = {};
    char currentPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    char stagingPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
};

struct DigestReadResult {
    FileSystemRpcStatus status = FileSystemRpcStatus::STORAGE_ERROR;
    uint8_t sha256[FILESYSTEM_RPC_SHA256_SIZE] = {};
};

struct ExecutionResult {
    FileSystemRpcStatus status = FileSystemRpcStatus::STORAGE_ERROR;
    bool applied = false;
};

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
    void transform_(const uint8_t block[64]) {
        static constexpr uint32_t roundConstants[64] = {
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
            const uint32_t sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const uint32_t choose = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + sum1 + choose + roundConstants[i] + words[i];
            const uint32_t sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
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

FLASHMEM uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

bool digestEquals(const uint8_t* lhs, const uint8_t* rhs) {
    uint8_t difference = 0;
    for (size_t i = 0; i < FILESYSTEM_RPC_SHA256_SIZE; ++i) {
        difference |= static_cast<uint8_t>(lhs[i] ^ rhs[i]);
    }
    return difference == 0;
}

constexpr char asciiLower(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
}

bool pathEquals(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) return false;
    while (*lhs != '\0' && *rhs != '\0') {
        if (asciiLower(*lhs) != asciiLower(*rhs)) return false;
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

bool pathStartsWith(const char* path, const char* prefix) {
    if (!path || !prefix) return false;
    while (*prefix != '\0') {
        if (*path == '\0' || asciiLower(*path) != asciiLower(*prefix)) return false;
        ++path;
        ++prefix;
    }
    return true;
}

void copyDigest(uint8_t* destination, const uint8_t* source) {
    std::memcpy(destination, source, FILESYSTEM_RPC_SHA256_SIZE);
}

bool isNotFound(FileSystemRpcStatus status) {
    return status == FileSystemRpcStatus::NOT_FOUND;
}

FLASHMEM FileSystemRpcStatus removeIfExists(
    core::persistence::ProductFileService& files,
    const char* path
) {
    auto result = files.remove(path);
    if (result || result.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
        return FileSystemRpcStatus::OK;
    }
    return mapError(result.error());
}

FLASHMEM DigestReadResult readDigest(
    core::persistence::ProductFileService& files,
    const char* path
) {
    DigestReadResult result{};
    auto before = files.stat(path);
    if (!before) {
        result.status = mapError(before.error());
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
            result.status = mapError(read.error());
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
        result.status = mapError(after.error());
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

FLASHMEM FileSystemRpcStatus writeJournal(
    core::persistence::ProductFileService& files,
    const ConditionalMutationJournal& journal
) {
    uint8_t buffer[JOURNAL_BUFFER_SIZE] = {};
    ByteWriter writer(buffer, sizeof(buffer));
    const char* staging = journal.kind == ConditionalMutationKind::REPLACE
        ? journal.stagingPath
        : "";
    if (!writer.writeBytes(JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) ||
        !writer.writeU8(JOURNAL_VERSION) ||
        !writer.writeU8(static_cast<uint8_t>(journal.kind)) ||
        !writer.writeU32(journal.operationId) ||
        !writer.writeBytes(journal.expectedSourceSha256, FILESYSTEM_RPC_SHA256_SIZE) ||
        !writer.writeBytes(journal.replacementSha256, FILESYSTEM_RPC_SHA256_SIZE) ||
        !writer.writeString(journal.currentPath, oc::interface::FILESYSTEM_MAX_PATH_LENGTH) ||
        !writer.writeString(staging, oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) {
        return FileSystemRpcStatus::INVALID_ARGUMENT;
    }
    const uint32_t checksum = crc32(buffer, writer.position());
    if (!writer.writeU32(checksum)) return FileSystemRpcStatus::INVALID_ARGUMENT;

    auto existingJournal = files.stat(JOURNAL_PATH);
    if (existingJournal) return FileSystemRpcStatus::INVALID_STATE;
    if (existingJournal.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return mapError(existingJournal.error());
    }
    const auto orphanCleanup = removeIfExists(files, JOURNAL_STAGING_PATH);
    if (orphanCleanup != FileSystemRpcStatus::OK) return orphanCleanup;

    // The durable journal name is never opened or truncated directly. A power
    // loss during this stream leaves only an uncommitted staging file, which
    // recovery can safely discard because no data mutation begins before the
    // atomic rename below succeeds.
    auto begin = files.beginWrite(
        JOURNAL_STAGING_PATH,
        static_cast<uint32_t>(writer.position())
    );
    if (!begin) return mapError(begin.error());
    auto appended = files.appendWrite(buffer, writer.position());
    if (!appended || appended.value() != writer.position()) {
        files.abortWrite();
        (void)files.remove(JOURNAL_STAGING_PATH);
        return appended ? FileSystemRpcStatus::STORAGE_ERROR : mapError(appended.error());
    }
    auto finish = files.finishWrite();
    if (!finish) {
        files.abortWrite();
        (void)files.remove(JOURNAL_STAGING_PATH);
        return mapError(finish.error());
    }
    auto promoted = files.rename(JOURNAL_STAGING_PATH, JOURNAL_PATH);
    if (!promoted) {
        // If a backend reports an ambiguous rename failure, the caller leaves
        // recovery armed; removing the old name is harmless when promotion did
        // complete and otherwise clears the uncommitted stream.
        (void)files.remove(JOURNAL_STAGING_PATH);
        return mapError(promoted.error());
    }
    return FileSystemRpcStatus::OK;
}

FLASHMEM FileSystemRpcStatus readJournal(
    core::persistence::ProductFileService& files,
    ConditionalMutationJournal& journal,
    bool& present,
    bool& corrupt
) {
    present = false;
    corrupt = false;
    auto info = files.stat(JOURNAL_PATH);
    if (!info) {
        const auto status = mapError(info.error());
        return isNotFound(status) ? FileSystemRpcStatus::OK : status;
    }
    present = true;
    if (info.value().type != oc::interface::FileType::FILE ||
        info.value().sizeBytes < sizeof(JOURNAL_MAGIC) + 1U ||
        info.value().sizeBytes > JOURNAL_BUFFER_SIZE) {
        corrupt = true;
        return FileSystemRpcStatus::STORAGE_ERROR;
    }

    uint8_t buffer[JOURNAL_BUFFER_SIZE] = {};
    uint32_t offset = 0;
    while (offset < info.value().sizeBytes) {
        auto read = files.read(
            JOURNAL_PATH,
            offset,
            buffer + offset,
            info.value().sizeBytes - offset
        );
        if (!read) return mapError(read.error());
        if (read.value() == 0 ||
            read.value() > info.value().sizeBytes - offset) {
            corrupt = true;
            return FileSystemRpcStatus::STORAGE_ERROR;
        }
        offset += static_cast<uint32_t>(read.value());
    }

    ByteReader reader(buffer, info.value().sizeBytes);
    const uint8_t* magic = nullptr;
    uint8_t version = 0;
    uint8_t rawKind = 0;
    const uint8_t* expected = nullptr;
    const uint8_t* replacement = nullptr;
    if (!reader.readBytes(magic, sizeof(JOURNAL_MAGIC)) ||
        std::memcmp(magic, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) != 0 ||
        !reader.readU8(version)) {
        corrupt = true;
        return FileSystemRpcStatus::STORAGE_ERROR;
    }
    // A newer durable journal may describe a transaction this firmware cannot
    // interpret. Preserve it in place and expose read-only RPC operations only.
    if (version != JOURNAL_VERSION) return FileSystemRpcStatus::UNSUPPORTED;
    if (!reader.readU8(rawKind) ||
        (rawKind != static_cast<uint8_t>(ConditionalMutationKind::REPLACE) &&
         rawKind != static_cast<uint8_t>(ConditionalMutationKind::DELETE)) ||
        !reader.readU32(journal.operationId) ||
        !reader.readBytes(expected, FILESYSTEM_RPC_SHA256_SIZE) ||
        !reader.readBytes(replacement, FILESYSTEM_RPC_SHA256_SIZE) ||
        !reader.readString(
            journal.currentPath,
            sizeof(journal.currentPath),
            oc::interface::FILESYSTEM_MAX_PATH_LENGTH
        ) ||
        !reader.readString(
            journal.stagingPath,
            sizeof(journal.stagingPath),
            oc::interface::FILESYSTEM_MAX_PATH_LENGTH
        )) {
        corrupt = true;
        return FileSystemRpcStatus::STORAGE_ERROR;
    }
    uint32_t storedChecksum = 0;
    if (!reader.readU32(storedChecksum) || reader.remaining() != 0 ||
        storedChecksum != crc32(buffer, info.value().sizeBytes - sizeof(uint32_t))) {
        corrupt = true;
        return FileSystemRpcStatus::STORAGE_ERROR;
    }
    journal.kind = static_cast<ConditionalMutationKind>(rawKind);
    copyDigest(journal.expectedSourceSha256, expected);
    copyDigest(journal.replacementSha256, replacement);
    if (journal.currentPath[0] == '\0' ||
        (journal.kind == ConditionalMutationKind::REPLACE && journal.stagingPath[0] == '\0') ||
        (journal.kind == ConditionalMutationKind::DELETE && journal.stagingPath[0] != '\0')) {
        corrupt = true;
        return FileSystemRpcStatus::STORAGE_ERROR;
    }
    return FileSystemRpcStatus::OK;
}

FLASHMEM FileSystemRpcStatus quarantineCorruptJournal(
    core::persistence::ProductFileService& files
) {
    // Keep one deterministic evidence slot. Only protocol-owned journal
    // metadata is rotated; current, staging and backup data are never touched.
    const auto staleCleanup = removeIfExists(files, JOURNAL_QUARANTINE_PATH);
    if (staleCleanup != FileSystemRpcStatus::OK) return staleCleanup;
    auto quarantined = files.rename(JOURNAL_PATH, JOURNAL_QUARANTINE_PATH);
    return quarantined ? FileSystemRpcStatus::OK : mapError(quarantined.error());
}

FLASHMEM FileSystemRpcStatus removeJournalLast(
    core::persistence::ProductFileService& files
) {
    const auto stagingCleanup = removeIfExists(files, JOURNAL_STAGING_PATH);
    if (stagingCleanup != FileSystemRpcStatus::OK) return stagingCleanup;
    return removeIfExists(files, JOURNAL_PATH);
}

FLASHMEM ExecutionResult executeReplace(
    core::persistence::ProductFileService& files,
    const ConditionalMutationJournal& journal
) {
    auto current = readDigest(files, journal.currentPath);
    if (current.status == FileSystemRpcStatus::OK &&
        digestEquals(current.sha256, journal.replacementSha256)) {
        if (removeIfExists(files, journal.stagingPath) != FileSystemRpcStatus::OK ||
            removeIfExists(files, BACKUP_PATH) != FileSystemRpcStatus::OK ||
            removeJournalLast(files) != FileSystemRpcStatus::OK) {
            return {FileSystemRpcStatus::STORAGE_ERROR, true};
        }
        return {FileSystemRpcStatus::OK, true};
    }

    if (current.status == FileSystemRpcStatus::OK) {
        if (!digestEquals(current.sha256, journal.expectedSourceSha256)) {
            return {FileSystemRpcStatus::PRECONDITION_FAILED, false};
        }
        auto staging = readDigest(files, journal.stagingPath);
        if (staging.status != FileSystemRpcStatus::OK ||
            !digestEquals(staging.sha256, journal.replacementSha256)) {
            return {
                staging.status == FileSystemRpcStatus::OK
                    ? FileSystemRpcStatus::PRECONDITION_FAILED
                    : staging.status,
                false,
            };
        }
        auto backup = files.stat(BACKUP_PATH);
        if (backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
            return {FileSystemRpcStatus::INVALID_STATE, false};
        }

        auto backedUp = files.rename(journal.currentPath, BACKUP_PATH);
        if (!backedUp) return {mapError(backedUp.error()), false};
        auto promoted = files.rename(journal.stagingPath, journal.currentPath);
        if (!promoted) {
            auto restored = files.rename(BACKUP_PATH, journal.currentPath);
            if (restored) {
                (void)removeJournalLast(files);
            }
            return {mapError(promoted.error()), false};
        }

        auto committed = readDigest(files, journal.currentPath);
        if (committed.status != FileSystemRpcStatus::OK ||
            !digestEquals(committed.sha256, journal.replacementSha256)) {
            (void)files.remove(journal.currentPath);
            auto restored = files.rename(BACKUP_PATH, journal.currentPath);
            if (restored) (void)removeJournalLast(files);
            return {FileSystemRpcStatus::STORAGE_ERROR, false};
        }
        if (removeIfExists(files, BACKUP_PATH) != FileSystemRpcStatus::OK ||
            removeJournalLast(files) != FileSystemRpcStatus::OK) {
            return {FileSystemRpcStatus::STORAGE_ERROR, true};
        }
        return {FileSystemRpcStatus::OK, true};
    }

    if (current.status != FileSystemRpcStatus::NOT_FOUND) {
        return {current.status, false};
    }

    auto backup = readDigest(files, BACKUP_PATH);
    if (backup.status == FileSystemRpcStatus::NOT_FOUND) {
        return {FileSystemRpcStatus::STORAGE_ERROR, false};
    }
    if (backup.status != FileSystemRpcStatus::OK ||
        !digestEquals(backup.sha256, journal.expectedSourceSha256)) {
        return {
            backup.status == FileSystemRpcStatus::OK
                ? FileSystemRpcStatus::PRECONDITION_FAILED
                : backup.status,
            false,
        };
    }

    auto staging = readDigest(files, journal.stagingPath);
    if (staging.status == FileSystemRpcStatus::OK &&
        digestEquals(staging.sha256, journal.replacementSha256)) {
        auto promoted = files.rename(journal.stagingPath, journal.currentPath);
        if (!promoted) return {mapError(promoted.error()), false};
        if (removeIfExists(files, BACKUP_PATH) != FileSystemRpcStatus::OK ||
            removeJournalLast(files) != FileSystemRpcStatus::OK) {
            return {FileSystemRpcStatus::STORAGE_ERROR, true};
        }
        return {FileSystemRpcStatus::OK, true};
    }

    auto restored = files.rename(BACKUP_PATH, journal.currentPath);
    if (!restored) return {mapError(restored.error()), false};
    if (removeJournalLast(files) != FileSystemRpcStatus::OK) {
        return {FileSystemRpcStatus::STORAGE_ERROR, false};
    }
    return {FileSystemRpcStatus::STORAGE_ERROR, false};
}

FLASHMEM ExecutionResult executeDelete(
    core::persistence::ProductFileService& files,
    const ConditionalMutationJournal& journal
) {
    auto current = readDigest(files, journal.currentPath);
    if (current.status == FileSystemRpcStatus::OK) {
        if (!digestEquals(current.sha256, journal.expectedSourceSha256)) {
            return {FileSystemRpcStatus::PRECONDITION_FAILED, false};
        }
        auto backup = files.stat(BACKUP_PATH);
        if (backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
            return {FileSystemRpcStatus::INVALID_STATE, false};
        }
        auto moved = files.rename(journal.currentPath, BACKUP_PATH);
        if (!moved) return {mapError(moved.error()), false};
    } else if (current.status != FileSystemRpcStatus::NOT_FOUND) {
        return {current.status, false};
    }

    auto backup = readDigest(files, BACKUP_PATH);
    if (backup.status == FileSystemRpcStatus::OK) {
        if (!digestEquals(backup.sha256, journal.expectedSourceSha256)) {
            return {FileSystemRpcStatus::PRECONDITION_FAILED, false};
        }
        auto removed = files.remove(BACKUP_PATH);
        if (!removed) return {mapError(removed.error()), false};
    } else if (backup.status != FileSystemRpcStatus::NOT_FOUND) {
        return {backup.status, false};
    }

    if (removeJournalLast(files) != FileSystemRpcStatus::OK) {
        return {FileSystemRpcStatus::STORAGE_ERROR, true};
    }
    return {FileSystemRpcStatus::OK, true};
}

FLASHMEM ExecutionResult executeJournal(
    core::persistence::ProductFileService& files,
    const ConditionalMutationJournal& journal
) {
    return journal.kind == ConditionalMutationKind::REPLACE
        ? executeReplace(files, journal)
        : executeDelete(files, journal);
}

FLASHMEM Result<size_t> encodeConditionalResponse(
    FileSystemRpcMessageId messageId,
    uint16_t requestId,
    FileSystemRpcStatus status,
    FileSystemRpcMutationOutcome outcome,
    FileSystemRpcMutationSubject subject,
    uint32_t operationId,
    const uint8_t* observedSha256,
    uint8_t* response,
    size_t responseSize
) {
    static constexpr uint8_t zeroDigest[FILESYSTEM_RPC_SHA256_SIZE] = {};
    ByteWriter writer(response, responseSize);
    if (!writeFrameHeader(writer, messageId, requestId) ||
        !writer.writeU8(static_cast<uint8_t>(status)) ||
        !writer.writeU8(static_cast<uint8_t>(outcome)) ||
        !writer.writeU8(static_cast<uint8_t>(subject)) ||
        !writer.writeU32(operationId) ||
        !writer.writeBytes(
            observedSha256 ? observedSha256 : zeroDigest,
            FILESYSTEM_RPC_SHA256_SIZE
        )) {
        return bufferTooSmall();
    }
    return Result<size_t>::ok(writer.position());
}

FLASHMEM FileSystemRpcStatus normalizeMutationPath(
    core::persistence::ProductFileService& files,
    const char* path,
    char* normalized,
    size_t normalizedSize
) {
    auto result = files.resolvePath(path, normalized, normalizedSize);
    return result ? FileSystemRpcStatus::OK : mapError(result.error());
}

bool isReservedPath(const char* normalized) {
    // Reserve the complete protocol-owned rpc-* namespace. The prefix is
    // compared with FAT case semantics and also catches the normal RPC-CO~n
    // short-name aliases generated for the conditional journal and backup.
    return pathStartsWith(normalized, RESOLVED_PROTOCOL_TMP_PREFIX);
}

bool isStagingPath(const char* normalized) {
    return pathStartsWith(normalized, RESOLVED_TMP_PREFIX) &&
           normalized[std::strlen(RESOLVED_TMP_PREFIX)] != '\0';
}

bool containsFatShortNameAliasSyntax(const char* normalized) {
    // Conditional CAS cannot prove physical identity through the current
    // lexical filesystem interface. Reject the '~' syntax used by FAT short
    // aliases so current/staging cannot name one LFN through two spellings.
    // Legitimate '~' filenames are therefore intentionally unsupported by CAS.
    return normalized != nullptr && std::strchr(normalized, '~') != nullptr;
}

}  // namespace

FLASHMEM bool internal::isConditionalMutationReservedPath(
    core::persistence::ProductFileService& files,
    const char* productPath
) {
    char normalized[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    auto resolved = files.resolvePath(productPath, normalized, sizeof(normalized));
    return resolved && isReservedPath(normalized);
}

FLASHMEM FileSystemRpcStatus FileSystemRpcHandler::recoverConditionalMutation_() {
    if (writeSession_.active || files_.writeSessionActive()) {
        return FileSystemRpcStatus::BUSY;
    }
    ConditionalMutationJournal journal{};
    bool present = false;
    bool corrupt = false;
    const auto loaded = readJournal(files_, journal, present, corrupt);
    if (loaded != FileSystemRpcStatus::OK) {
        if (!corrupt) return loaded;
        const auto quarantined = quarantineCorruptJournal(files_);
        if (quarantined != FileSystemRpcStatus::OK) return quarantined;
        conditionalRecoveryState_ =
            FileSystemRpcConditionalRecoveryState::CORRUPT_JOURNAL_QUARANTINED;
        // This is an orphaned journal stream, not a user staging asset.
        return removeIfExists(files_, JOURNAL_STAGING_PATH);
    }
    const auto stagingCleanup = removeIfExists(files_, JOURNAL_STAGING_PATH);
    if (stagingCleanup != FileSystemRpcStatus::OK) return stagingCleanup;
    if (!present) return FileSystemRpcStatus::OK;
    const auto executed = executeJournal(files_, journal);
    return executed.status;
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleConditionalReplace_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    uint32_t operationId = 0;
    const uint8_t* expected = nullptr;
    const uint8_t* replacement = nullptr;
    char currentRaw[PATH_BUFFER_SIZE] = {};
    char stagingRaw[PATH_BUFFER_SIZE] = {};
    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU32(operationId) ||
        !reader.readBytes(expected, FILESYSTEM_RPC_SHA256_SIZE) ||
        !reader.readBytes(replacement, FILESYSTEM_RPC_SHA256_SIZE) ||
        !readPath(reader, currentRaw, sizeof(currentRaw)) ||
        !readPath(reader, stagingRaw, sizeof(stagingRaw)) ||
        reader.remaining() != 0) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    if (writeSession_.active || files_.writeSessionActive()) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::BUSY,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }

    ConditionalMutationJournal journal{};
    journal.kind = ConditionalMutationKind::REPLACE;
    journal.operationId = operationId;
    copyDigest(journal.expectedSourceSha256, expected);
    copyDigest(journal.replacementSha256, replacement);
    const auto currentPathStatus = normalizeMutationPath(
        files_, currentRaw, journal.currentPath, sizeof(journal.currentPath)
    );
    const auto stagingPathStatus = normalizeMutationPath(
        files_, stagingRaw, journal.stagingPath, sizeof(journal.stagingPath)
    );
    if (currentPathStatus != FileSystemRpcStatus::OK ||
        stagingPathStatus != FileSystemRpcStatus::OK ||
        // FAT is case-insensitive: differently cased spellings may still name
        // the same physical file. Never let the idempotent cleanup path unlink
        // the canonical source through such an alias.
        pathEquals(journal.currentPath, journal.stagingPath) ||
        isReservedPath(journal.currentPath) ||
        isReservedPath(journal.stagingPath) ||
        containsFatShortNameAliasSyntax(journal.currentPath) ||
        containsFatShortNameAliasSyntax(journal.stagingPath) ||
        !isStagingPath(journal.stagingPath)) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }

    auto current = readDigest(files_, journal.currentPath);
    if (current.status == FileSystemRpcStatus::OK &&
        digestEquals(current.sha256, journal.replacementSha256)) {
        auto backup = files_.stat(BACKUP_PATH);
        const bool unexpectedBackup = backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND;
        const auto stagingCleanup = removeIfExists(files_, journal.stagingPath);
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            unexpectedBackup
                ? FileSystemRpcStatus::INVALID_STATE
                : stagingCleanup,
            !unexpectedBackup && stagingCleanup == FileSystemRpcStatus::OK
                ? FileSystemRpcMutationOutcome::ALREADY_APPLIED
                : FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    if (current.status != FileSystemRpcStatus::OK ||
        !digestEquals(current.sha256, journal.expectedSourceSha256)) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            current.status == FileSystemRpcStatus::OK
                ? FileSystemRpcStatus::PRECONDITION_FAILED
                : current.status,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::SOURCE,
            operationId,
            current.status == FileSystemRpcStatus::OK ? current.sha256 : nullptr,
            response,
            responseSize
        );
    }
    auto staging = readDigest(files_, journal.stagingPath);
    if (staging.status != FileSystemRpcStatus::OK ||
        !digestEquals(staging.sha256, journal.replacementSha256)) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            staging.status == FileSystemRpcStatus::OK
                ? FileSystemRpcStatus::PRECONDITION_FAILED
                : staging.status,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::STAGING,
            operationId,
            staging.status == FileSystemRpcStatus::OK ? staging.sha256 : nullptr,
            response,
            responseSize
        );
    }

    auto backup = files_.stat(BACKUP_PATH);
    if (backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_STATE,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }

    conditionalRecoveryChecked_ = false;
    conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::NOT_CHECKED;
    conditionalRecoveryStatus_ = FileSystemRpcStatus::OK;
    auto journalStatus = writeJournal(files_, journal);
    if (journalStatus == FileSystemRpcStatus::OK) {
        const auto executed = executeJournal(files_, journal);
        journalStatus = executed.status;
        if (executed.status == FileSystemRpcStatus::OK) {
            conditionalRecoveryChecked_ = true;
            conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::READY;
        }
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            executed.status,
            executed.status == FileSystemRpcStatus::OK
                ? FileSystemRpcMutationOutcome::APPLIED
                : FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    return encodeConditionalResponse(
        FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
        frame.requestId,
        journalStatus,
        FileSystemRpcMutationOutcome::NONE,
        FileSystemRpcMutationSubject::NONE,
        operationId,
        nullptr,
        response,
        responseSize
    );
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleConditionalDelete_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    uint32_t operationId = 0;
    const uint8_t* expected = nullptr;
    char currentRaw[PATH_BUFFER_SIZE] = {};
    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU32(operationId) ||
        !reader.readBytes(expected, FILESYSTEM_RPC_SHA256_SIZE) ||
        !readPath(reader, currentRaw, sizeof(currentRaw)) ||
        reader.remaining() != 0) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    if (writeSession_.active || files_.writeSessionActive()) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::BUSY,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }

    ConditionalMutationJournal journal{};
    journal.kind = ConditionalMutationKind::DELETE;
    journal.operationId = operationId;
    copyDigest(journal.expectedSourceSha256, expected);
    const auto currentPathStatus = normalizeMutationPath(
        files_, currentRaw, journal.currentPath, sizeof(journal.currentPath)
    );
    if (currentPathStatus != FileSystemRpcStatus::OK ||
        isReservedPath(journal.currentPath) ||
        containsFatShortNameAliasSyntax(journal.currentPath)) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }

    auto current = readDigest(files_, journal.currentPath);
    if (current.status == FileSystemRpcStatus::NOT_FOUND) {
        auto backup = files_.stat(BACKUP_PATH);
        const bool unexpectedBackup = backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND;
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            unexpectedBackup
                ? FileSystemRpcStatus::INVALID_STATE
                : FileSystemRpcStatus::OK,
            unexpectedBackup
                ? FileSystemRpcMutationOutcome::NONE
                : FileSystemRpcMutationOutcome::ALREADY_APPLIED,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    if (current.status != FileSystemRpcStatus::OK ||
        !digestEquals(current.sha256, journal.expectedSourceSha256)) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            current.status == FileSystemRpcStatus::OK
                ? FileSystemRpcStatus::PRECONDITION_FAILED
                : current.status,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::SOURCE,
            operationId,
            current.status == FileSystemRpcStatus::OK ? current.sha256 : nullptr,
            response,
            responseSize
        );
    }

    auto backup = files_.stat(BACKUP_PATH);
    if (backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_STATE,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }

    conditionalRecoveryChecked_ = false;
    conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::NOT_CHECKED;
    conditionalRecoveryStatus_ = FileSystemRpcStatus::OK;
    auto journalStatus = writeJournal(files_, journal);
    if (journalStatus == FileSystemRpcStatus::OK) {
        const auto executed = executeJournal(files_, journal);
        journalStatus = executed.status;
        if (executed.status == FileSystemRpcStatus::OK) {
            conditionalRecoveryChecked_ = true;
            conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::READY;
        }
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            executed.status,
            executed.status == FileSystemRpcStatus::OK
                ? FileSystemRpcMutationOutcome::APPLIED
                : FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    return encodeConditionalResponse(
        FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
        frame.requestId,
        journalStatus,
        FileSystemRpcMutationOutcome::NONE,
        FileSystemRpcMutationSubject::NONE,
        operationId,
        nullptr,
        response,
        responseSize
    );
}

}  // namespace core::protocol::filesystem

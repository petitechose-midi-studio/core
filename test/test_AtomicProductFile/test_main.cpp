#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include <oc/impl/HostFileSystem.hpp>
#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "../../src/persistence/AtomicProductFile.hpp"
#include "../../src/persistence/PersistenceChecksum.hpp"
#include "../../src/persistence/ProductFileCommitPlan.hpp"
#include "../../src/persistence/ProductFileRecoveryPlan.hpp"

namespace {

using core::persistence::AtomicProductFilePaths;
using core::persistence::ProductFileService;
using core::persistence::ProductFileTransactionPhase;
using core::persistence::ProductMutationOwner;
using core::persistence::ProductStorageState;
using oc::type::ErrorCode;

constexpr char DIRECTORY[] = "projects";
constexpr char CURRENT[] = "projects/atomic.bin";
constexpr char BACKUP[] = "projects/atomic.bin.bak";
constexpr char TEMPORARY[] = "tmp/atomic.bin.tmp";
constexpr char RESOLVED_CURRENT[] = "/midi-studio/projects/atomic.bin";
constexpr char RESOLVED_BACKUP[] = "/midi-studio/projects/atomic.bin.bak";
constexpr char RESOLVED_TEMPORARY[] = "/midi-studio/tmp/atomic.bin.tmp";
constexpr char RESOLVED_JOURNAL_A[] =
    "/midi-studio/tmp/rpc-product-file-a.journal";
constexpr char RESOLVED_JOURNAL_B[] =
    "/midi-studio/tmp/rpc-product-file-b.journal";

constexpr uint8_t OLD_DATA[] = {
    'o', 'l', 'd', '-', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
};
constexpr uint8_t NEW_DATA[] = {
    'n', 'e', 'w', '-', 'a', 'b', 'c', 'd', 'e', 'f', '0', '1', '2', '3', '4', '5',
};
constexpr uint8_t FINAL_DATA[] = {
    'f', 'i', 'n', '-', '5', '4', '3', '2', '1', '0', 'f', 'e', 'd', 'c', 'b', 'a',
};

static_assert(sizeof(OLD_DATA) == sizeof(NEW_DATA));
static_assert(sizeof(OLD_DATA) == sizeof(FINAL_DATA));

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           "midi-studio-core-atomic-product-file-test";
}

std::filesystem::path productPath(const char* relative) {
    return testRoot() / "midi-studio" / relative;
}

void resetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(testRoot(), error);
}

template <typename T>
oc::type::Result<T> mediaLost() {
    return oc::type::Result<T>::err(
        {ErrorCode::HARDWARE_NOT_FOUND, "simulated power loss"}
    );
}

enum class CutMode : uint8_t {
    BEFORE = 0,
    AFTER,
};

class BoundaryFaultFileSystem final : public oc::interface::IFileSystem {
public:
    explicit BoundaryFaultFileSystem(const char* root)
        : delegate_(root) {}

    ~BoundaryFaultFileSystem() override {
        delegate_.abortWrite();
    }

    void arm(uint32_t targetBoundary, CutMode mode) {
        target_boundary_ = targetBoundary;
        mode_ = mode;
        boundary_count_ = 0;
        journal_record_count_ = 0;
        journal_bytes_ = 0;
        canonical_rename_count_ = 0;
        backup_rename_count_ = 0;
        promotion_count_ = 0;
        backup_cleanup_count_ = 0;
        flush_count_ = 0;
        armed_ = true;
        cut_ = false;
    }

    uint32_t boundaryCount() const { return boundary_count_; }
    bool cut() const { return cut_; }
    uint32_t journalRecordCount() const { return journal_record_count_; }
    uint32_t journalBytes() const { return journal_bytes_; }
    uint32_t canonicalRenameCount() const { return canonical_rename_count_; }
    uint32_t backupRenameCount() const { return backup_rename_count_; }
    uint32_t promotionCount() const { return promotion_count_; }
    uint32_t backupCleanupCount() const { return backup_cleanup_count_; }
    uint32_t flushCount() const { return flush_count_; }
    void corruptNextPromotion() { corrupt_next_promotion_ = true; }

    oc::type::Result<void> init() override {
        if (cut_) return mediaLost<void>();
        return delegate_.init();
    }

    bool available() const override {
        return !cut_ && delegate_.available();
    }

    oc::type::Result<oc::interface::FileInfo> stat(const char* path) override {
        if (cut_) return mediaLost<oc::interface::FileInfo>();
        return delegate_.stat(path);
    }

    oc::type::Result<void> list(
        const char* path,
        oc::interface::DirectoryEntryVisitor visitor,
        void* context
    ) override {
        if (cut_) return mediaLost<void>();
        return delegate_.list(path, visitor, context);
    }

    oc::type::Result<void> createDirectory(const char* path) override {
        if (cut_) return mediaLost<void>();
        return delegate_.createDirectory(path);
    }

    oc::type::Result<void> remove(
        const char* path,
        oc::interface::RemoveMode mode =
            oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
    ) override {
        if (tripBefore_()) return mediaLost<void>();
        auto result = delegate_.remove(path, mode);
        if (result && std::strcmp(path, RESOLVED_BACKUP) == 0) {
            ++backup_cleanup_count_;
        }
        if (tripAfter_()) return mediaLost<void>();
        return result;
    }

    oc::type::Result<void> rename(
        const char* fromPath,
        const char* toPath
    ) override {
        if (tripBefore_()) return mediaLost<void>();
        auto result = delegate_.rename(fromPath, toPath);
        if (result && std::strcmp(fromPath, RESOLVED_CURRENT) == 0 &&
            std::strcmp(toPath, RESOLVED_BACKUP) == 0) {
            ++canonical_rename_count_;
            ++backup_rename_count_;
        } else if (result && std::strcmp(fromPath, RESOLVED_TEMPORARY) == 0 &&
                   std::strcmp(toPath, RESOLVED_CURRENT) == 0) {
            ++canonical_rename_count_;
            ++promotion_count_;
            if (corrupt_next_promotion_) {
                uint8_t firstByte = 0U;
                auto read = delegate_.read(toPath, 0U, &firstByte, 1U);
                assert(read && read.value() == 1U);
                firstByte ^= 0x80U;
                auto written = delegate_.write(toPath, 0U, &firstByte, 1U);
                assert(written && written.value() == 1U);
                assert(delegate_.flush(toPath));
                corrupt_next_promotion_ = false;
            }
        }
        if (tripAfter_()) return mediaLost<void>();
        return result;
    }

    oc::type::Result<size_t> read(
        const char* path,
        uint32_t offset,
        uint8_t* buffer,
        size_t size
    ) override {
        if (cut_) return mediaLost<size_t>();
        return delegate_.read(path, offset, buffer, size);
    }

    oc::type::Result<size_t> write(
        const char* path,
        uint32_t offset,
        const uint8_t* data,
        size_t size
    ) override {
        if (cut_) return mediaLost<size_t>();
        return delegate_.write(path, offset, data, size);
    }

    oc::type::Result<void> flush(const char* path) override {
        if (tripBefore_()) return mediaLost<void>();
        auto result = delegate_.flush(path);
        if (result) ++flush_count_;
        if (tripAfter_()) return mediaLost<void>();
        return result;
    }

    oc::type::Result<void> beginWrite(
        const char* path,
        uint32_t expectedSize
    ) override {
        if (tripBefore_()) return mediaLost<void>();
        auto result = delegate_.beginWrite(path, expectedSize);
        if (result && (std::strcmp(path, RESOLVED_JOURNAL_A) == 0 ||
                       std::strcmp(path, RESOLVED_JOURNAL_B) == 0)) {
            ++journal_record_count_;
            journal_bytes_ += expectedSize;
        }
        if (tripAfter_()) return mediaLost<void>();
        return result;
    }

    oc::type::Result<size_t> appendWrite(
        const uint8_t* data,
        size_t size
    ) override {
        if (tripBefore_()) return mediaLost<size_t>();
        auto result = delegate_.appendWrite(data, size);
        if (tripAfter_()) return mediaLost<size_t>();
        return result;
    }

    oc::type::Result<void> finishWrite() override {
        if (tripBefore_()) return mediaLost<void>();
        auto result = delegate_.finishWrite();
        if (tripAfter_()) return mediaLost<void>();
        return result;
    }

    void abortWrite() override {
        delegate_.abortWrite();
    }

private:
    void trip_() {
        delegate_.abortWrite();
        cut_ = true;
    }

    bool tripBefore_() {
        if (cut_) return true;
        if (!armed_) return false;
        ++boundary_count_;
        if (target_boundary_ != 0U &&
            boundary_count_ == target_boundary_ &&
            mode_ == CutMode::BEFORE) {
            trip_();
            return true;
        }
        return false;
    }

    bool tripAfter_() {
        if (!armed_ || cut_ || target_boundary_ == 0U) return cut_;
        if (boundary_count_ == target_boundary_ && mode_ == CutMode::AFTER) {
            trip_();
            return true;
        }
        return false;
    }

    oc::impl::HostFileSystem delegate_;
    uint32_t target_boundary_ = 0;
    uint32_t boundary_count_ = 0;
    uint32_t journal_record_count_ = 0;
    uint32_t journal_bytes_ = 0;
    uint32_t canonical_rename_count_ = 0;
    uint32_t backup_rename_count_ = 0;
    uint32_t promotion_count_ = 0;
    uint32_t backup_cleanup_count_ = 0;
    uint32_t flush_count_ = 0;
    CutMode mode_ = CutMode::BEFORE;
    bool armed_ = false;
    bool cut_ = false;
    bool corrupt_next_promotion_ = false;
};

oc::type::Result<void> replace(
    ProductFileService& files,
    const uint8_t* data,
    uint32_t size
) {
    auto acquired = files.acquireMutation(ProductMutationOwner::PROJECT);
    if (!acquired) return oc::type::Result<void>::err(acquired.error());
    auto lease = std::move(acquired.value());
    auto result = core::persistence::replaceProductFileAtomically(
        files,
        lease,
        {DIRECTORY, CURRENT, BACKUP, TEMPORARY},
        data,
        size,
        7U
    );
    if (files.owns(lease)) {
        auto released = files.releaseMutation(lease);
        if (result && !released) return released;
    }
    return result;
}

void seedCurrent(ProductFileService& files) {
    auto acquired = files.acquireMutation(ProductMutationOwner::PROJECT);
    assert(acquired);
    auto lease = std::move(acquired.value());
    auto written = files.write(lease, CURRENT, 0, OLD_DATA, sizeof(OLD_DATA));
    assert(written && written.value() == sizeof(OLD_DATA));
    assert(files.flush(lease, CURRENT));
    assert(files.releaseMutation(lease));
}

bool missing(ProductFileService& files, const char* path) {
    auto info = files.stat(path);
    return !info && info.error().code == ErrorCode::RESOURCE_NOT_FOUND;
}

void assertFileEquals(
    ProductFileService& files,
    const uint8_t* expected,
    size_t expectedSize
) {
    auto info = files.stat(CURRENT);
    assert(info);
    assert(info.value().type == oc::interface::FileType::FILE);
    assert(info.value().sizeBytes == expectedSize);
    uint8_t actual[sizeof(FINAL_DATA)] = {};
    assert(expectedSize <= sizeof(actual));
    auto read = files.read(CURRENT, 0, actual, expectedSize);
    assert(read && read.value() == expectedSize);
    assert(std::memcmp(actual, expected, expectedSize) == 0);
}

void assertCanonicalAfterRecovery(
    ProductFileService& files,
    bool hadCurrent
) {
    auto info = files.stat(CURRENT);
    if (!info) {
        assert(!hadCurrent);
        assert(info.error().code == ErrorCode::RESOURCE_NOT_FOUND);
        return;
    }
    assert(info.value().type == oc::interface::FileType::FILE);
    assert(info.value().sizeBytes == sizeof(OLD_DATA));
    uint8_t actual[sizeof(OLD_DATA)] = {};
    auto read = files.read(CURRENT, 0, actual, sizeof(actual));
    assert(read && read.value() == sizeof(actual));
    const bool isOld = std::memcmp(actual, OLD_DATA, sizeof(actual)) == 0;
    const bool isNew = std::memcmp(actual, NEW_DATA, sizeof(actual)) == 0;
    assert(isNew || (hadCurrent && isOld));
}

void assertJournalBounded() {
    const auto slotA = productPath("tmp/rpc-product-file-a.journal");
    const auto slotB = productPath("tmp/rpc-product-file-b.journal");
    assert(std::filesystem::is_regular_file(slotA));
    assert(std::filesystem::is_regular_file(slotB));
    assert(std::filesystem::file_size(slotA) <=
           core::persistence::PRODUCT_FILE_JOURNAL_MAX_RECORD_SIZE);
    assert(std::filesystem::file_size(slotB) <=
           core::persistence::PRODUCT_FILE_JOURNAL_MAX_RECORD_SIZE);
}

void assertSuccessEnvelope(const BoundaryFaultFileSystem& backend, bool hadCurrent) {
    const uint32_t expectedRecords = hadCurrent ? 4U : 3U;
    assert(backend.journalRecordCount() == expectedRecords);
    assert(backend.journalBytes() <=
           expectedRecords * core::persistence::PRODUCT_FILE_JOURNAL_MAX_RECORD_SIZE);
    assert(backend.canonicalRenameCount() == (hadCurrent ? 2U : 1U));
    assert(backend.backupRenameCount() == (hadCurrent ? 1U : 0U));
    assert(backend.promotionCount() == 1U);
    assert(backend.backupCleanupCount() == (hadCurrent ? 1U : 0U));
    assert(backend.flushCount() <= (hadCurrent ? 7U : 5U));
}

uint32_t executeBoundaryScenario(
    bool hadCurrent,
    uint32_t targetBoundary,
    CutMode mode
) {
    resetTestRoot();
    uint32_t observedBoundaries = 0;
    {
        BoundaryFaultFileSystem backend(testRoot().string().c_str());
        ProductFileService files(backend);
        assert(files.init());
        if (hadCurrent) seedCurrent(files);

        backend.arm(targetBoundary, mode);
        auto result = replace(files, NEW_DATA, sizeof(NEW_DATA));
        observedBoundaries = backend.boundaryCount();
        if (targetBoundary == 0U) {
            assert(result);
            assert(!backend.cut());
            assertSuccessEnvelope(backend, hadCurrent);
        } else {
            assert(!result);
            assert(backend.cut());
            assert(observedBoundaries == targetBoundary);
        }
    }

    if (targetBoundary == 0U) return observedBoundaries;

    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    auto initialized = files.init();
    if (!initialized) {
        std::cerr << "recovery failed at boundary " << targetBoundary
                  << (mode == CutMode::BEFORE ? " before" : " after")
                  << " with " << oc::type::errorCodeToString(initialized.error().code)
                  << '\n';
    }
    assert(initialized);
    assert(files.storageState() == ProductStorageState::READY);
    assertCanonicalAfterRecovery(files, hadCurrent);

    assert(replace(files, FINAL_DATA, sizeof(FINAL_DATA)));
    assertFileEquals(files, FINAL_DATA, sizeof(FINAL_DATA));
    assert(missing(files, TEMPORARY));
    assert(missing(files, BACKUP));
    assertJournalBounded();
    return observedBoundaries;
}

void createProductLayout() {
    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    assert(files.init());
}

void writeRaw(const std::filesystem::path& path, const uint8_t* data, size_t size) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    output.flush();
    assert(output);
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (uint8_t index = 0; index < 4U; ++index) {
        bytes.push_back(static_cast<uint8_t>(value >> (8U * index)));
    }
}

void appendU64(std::vector<uint8_t>& bytes, uint64_t value) {
    for (uint8_t index = 0; index < 8U; ++index) {
        bytes.push_back(static_cast<uint8_t>(value >> (8U * index)));
    }
}

std::vector<uint8_t> journalRecord(
    ProductFileTransactionPhase phase,
    bool hadCurrent,
    uint64_t sequence,
    uint32_t expectedSize,
    uint32_t expectedCrc32
) {
    std::vector<uint8_t> bytes;
    bytes.reserve(160);
    bytes.insert(bytes.end(), {'P', 'F', 'T', 'X'});
    bytes.push_back(core::persistence::PRODUCT_FILE_JOURNAL_VERSION);
    bytes.push_back(static_cast<uint8_t>(phase));
    bytes.push_back(hadCurrent ? 0x01U : 0U);
    bytes.push_back(0U);
    appendU64(bytes, sequence);
    appendU32(bytes, expectedSize);
    appendU32(bytes, expectedCrc32);
    for (const char* path : {RESOLVED_CURRENT, RESOLVED_TEMPORARY, RESOLVED_BACKUP}) {
        const size_t length = std::strlen(path);
        assert(length <= UINT8_MAX);
        bytes.push_back(static_cast<uint8_t>(length));
        bytes.insert(bytes.end(), path, path + length);
    }
    appendU32(
        bytes,
        core::persistence::checksum::crc32(bytes.data(), bytes.size())
    );
    return bytes;
}

std::vector<uint8_t> terminalJournal(uint64_t sequence) {
    return journalRecord(
        ProductFileTransactionPhase::COMMITTED,
        true,
        sequence,
        sizeof(OLD_DATA),
        core::persistence::checksum::crc32(OLD_DATA, sizeof(OLD_DATA))
    );
}

void test_every_durable_boundary_recovers_old_or_new() {
    const uint32_t replaceBoundaries = executeBoundaryScenario(true, 0U, CutMode::BEFORE);
    const uint32_t createBoundaries = executeBoundaryScenario(false, 0U, CutMode::BEFORE);
    assert(replaceBoundaries > 0U);
    assert(createBoundaries > 0U);

    uint32_t scenarios = 0;
    for (CutMode mode : {CutMode::BEFORE, CutMode::AFTER}) {
        for (uint32_t boundary = 1; boundary <= replaceBoundaries; ++boundary) {
            executeBoundaryScenario(true, boundary, mode);
            ++scenarios;
        }
        for (uint32_t boundary = 1; boundary <= createBoundaries; ++boundary) {
            executeBoundaryScenario(false, boundary, mode);
            ++scenarios;
        }
    }

    std::cout << "[PASS] test_every_durable_boundary_recovers_old_or_new ("
              << scenarios << " cuts; replace=" << replaceBoundaries
              << ", create=" << createBoundaries << ")\n";
}

void test_single_corrupt_slot_is_cleaned() {
    resetTestRoot();
    createProductLayout();
    const uint8_t corrupt[] = {'P', 'F', 'T', 'X'};
    const auto slot = productPath("tmp/rpc-product-file-a.journal");
    writeRaw(slot, corrupt, sizeof(corrupt));

    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    assert(files.init());
    assert(!std::filesystem::exists(slot));

    std::cout << "[PASS] test_single_corrupt_slot_is_cleaned\n";
}

void test_both_corrupt_slots_block_and_are_preserved() {
    resetTestRoot();
    createProductLayout();
    const uint8_t corruptA[] = {'b', 'a', 'd', 'a'};
    const uint8_t corruptB[] = {'b', 'a', 'd', 'b'};
    const auto slotA = productPath("tmp/rpc-product-file-a.journal");
    const auto slotB = productPath("tmp/rpc-product-file-b.journal");
    writeRaw(slotA, corruptA, sizeof(corruptA));
    writeRaw(slotB, corruptB, sizeof(corruptB));

    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    auto initialized = files.init();
    assert(!initialized);
    assert(initialized.error().code == ErrorCode::STORAGE_CORRUPT);
    assert(files.storageState() == ProductStorageState::DEGRADED);
    assert(std::filesystem::is_regular_file(slotA));
    assert(std::filesystem::is_regular_file(slotB));

    std::cout << "[PASS] test_both_corrupt_slots_block_and_are_preserved\n";
}

void test_unsupported_journal_version_blocks_and_is_preserved() {
    for (const uint8_t version : {
             static_cast<uint8_t>(core::persistence::PRODUCT_FILE_JOURNAL_VERSION - 1U),
             static_cast<uint8_t>(core::persistence::PRODUCT_FILE_JOURNAL_VERSION + 1U),
         }) {
        resetTestRoot();
        createProductLayout();
        uint8_t unsupported[30] = {};
        std::memcpy(unsupported, "PFTX", 4);
        unsupported[4] = version;
        const auto slot = productPath("tmp/rpc-product-file-a.journal");
        writeRaw(slot, unsupported, sizeof(unsupported));

        oc::impl::HostFileSystem backend(testRoot().string().c_str());
        ProductFileService files(backend);
        auto initialized = files.init();
        assert(!initialized);
        assert(initialized.error().code == ErrorCode::INVALID_STATE);
        assert(files.storageState() == ProductStorageState::DEGRADED);
        assert(std::filesystem::is_regular_file(slot));
    }

    std::cout << "[PASS] test_unsupported_journal_version_blocks_and_is_preserved\n";
}

void test_sequence_exhaustion_is_non_mutating() {
    resetTestRoot();
    {
        oc::impl::HostFileSystem backend(testRoot().string().c_str());
        ProductFileService files(backend);
        assert(files.init());
        seedCurrent(files);
    }
    const auto record = terminalJournal(std::numeric_limits<uint64_t>::max() - 3U);
    const auto slot = productPath("tmp/rpc-product-file-a.journal");
    writeRaw(slot, record.data(), record.size());

    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    assert(files.init());
    auto result = replace(files, NEW_DATA, sizeof(NEW_DATA));
    assert(!result);
    assert(result.error().code == ErrorCode::RESOURCE_EXHAUSTED);
    assert(files.storageState() == ProductStorageState::READY);
    assertFileEquals(files, OLD_DATA, sizeof(OLD_DATA));
    assert(missing(files, TEMPORARY));
    assert(std::filesystem::is_regular_file(slot));

    std::cout << "[PASS] test_sequence_exhaustion_is_non_mutating\n";
}

void test_same_size_corrupt_temporary_is_rejected_before_mapping() {
    resetTestRoot();
    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    assert(files.init());
    seedCurrent(files);

    auto acquired = files.acquireMutation(ProductMutationOwner::PROJECT);
    assert(acquired);
    auto lease = std::move(acquired.value());
    auto written = files.write(
        lease,
        TEMPORARY,
        0U,
        FINAL_DATA,
        sizeof(FINAL_DATA)
    );
    assert(written && written.value() == sizeof(FINAL_DATA));

    auto committed = core::persistence::commitProductFileTemp(
        files,
        lease,
        CURRENT,
        BACKUP,
        TEMPORARY,
        sizeof(NEW_DATA),
        core::persistence::checksum::crc32(NEW_DATA, sizeof(NEW_DATA))
    );
    assert(!committed);
    assert(committed.error().code == ErrorCode::STORAGE_CORRUPT);
    assert(files.storageState() == ProductStorageState::READY);
    assert(files.releaseMutation(lease));

    assertFileEquals(files, OLD_DATA, sizeof(OLD_DATA));
    assert(missing(files, BACKUP));
    assert(missing(files, core::persistence::PRODUCT_FILE_JOURNAL_SLOT_A));
    assert(missing(files, core::persistence::PRODUCT_FILE_JOURNAL_SLOT_B));

    std::cout << "[PASS] same-size corrupt temporary rejected before mapping\n";
}

void test_backed_up_corrupt_temporary_restores_backup() {
    resetTestRoot();
    createProductLayout();
    writeRaw(productPath("projects/atomic.bin.bak"), OLD_DATA, sizeof(OLD_DATA));
    writeRaw(productPath("tmp/atomic.bin.tmp"), FINAL_DATA, sizeof(FINAL_DATA));
    const auto record = journalRecord(
        ProductFileTransactionPhase::BACKED_UP,
        true,
        17U,
        sizeof(NEW_DATA),
        core::persistence::checksum::crc32(NEW_DATA, sizeof(NEW_DATA))
    );
    writeRaw(
        productPath("tmp/rpc-product-file-a.journal"),
        record.data(),
        record.size()
    );

    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    assert(files.init());
    assert(files.storageState() == ProductStorageState::READY);
    assertFileEquals(files, OLD_DATA, sizeof(OLD_DATA));
    assert(missing(files, TEMPORARY));
    assert(missing(files, BACKUP));

    std::cout << "[PASS] backed-up corrupt temporary restores backup\n";
}

void test_promoted_corrupt_current_restores_backup() {
    resetTestRoot();
    createProductLayout();
    writeRaw(productPath("projects/atomic.bin"), FINAL_DATA, sizeof(FINAL_DATA));
    writeRaw(productPath("projects/atomic.bin.bak"), OLD_DATA, sizeof(OLD_DATA));
    const auto record = journalRecord(
        ProductFileTransactionPhase::PROMOTED,
        true,
        23U,
        sizeof(NEW_DATA),
        core::persistence::checksum::crc32(NEW_DATA, sizeof(NEW_DATA))
    );
    writeRaw(
        productPath("tmp/rpc-product-file-a.journal"),
        record.data(),
        record.size()
    );

    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    assert(files.init());
    assert(files.storageState() == ProductStorageState::READY);
    assertFileEquals(files, OLD_DATA, sizeof(OLD_DATA));
    assert(missing(files, TEMPORARY));
    assert(missing(files, BACKUP));

    std::cout << "[PASS] promoted corrupt current restores backup\n";
}

void test_create_rollback_does_not_restore_stale_backup() {
    resetTestRoot();
    createProductLayout();
    writeRaw(productPath("projects/atomic.bin"), FINAL_DATA, sizeof(FINAL_DATA));
    writeRaw(productPath("projects/atomic.bin.bak"), OLD_DATA, sizeof(OLD_DATA));
    const auto record = journalRecord(
        ProductFileTransactionPhase::COMMITTED,
        false,
        47U,
        sizeof(NEW_DATA),
        core::persistence::checksum::crc32(NEW_DATA, sizeof(NEW_DATA))
    );
    writeRaw(
        productPath("tmp/rpc-product-file-a.journal"),
        record.data(),
        record.size()
    );

    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    assert(files.init());
    assert(missing(files, CURRENT));
    assert(missing(files, BACKUP));

    std::cout << "[PASS] create rollback discards stale backup\n";
}

void test_metadata_alias_and_nondistinct_paths_are_rejected() {
    resetTestRoot();
    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    assert(files.init());

    auto acquired = files.acquireMutation(ProductMutationOwner::PROJECT);
    assert(acquired);
    auto lease = std::move(acquired.value());
    auto expectInvalid = [&](const char* current, const char* backup, const char* tmp) {
        auto result = core::persistence::commitProductFileTemp(
            files,
            lease,
            current,
            backup,
            tmp,
            sizeof(NEW_DATA),
            core::persistence::checksum::crc32(NEW_DATA, sizeof(NEW_DATA))
        );
        assert(!result);
        assert(result.error().code == ErrorCode::INVALID_ARGUMENT);
    };
    expectInvalid(
        core::persistence::PRODUCT_FILE_JOURNAL_SLOT_A,
        BACKUP,
        TEMPORARY
    );
    expectInvalid("projects/~atomic.bin", BACKUP, TEMPORARY);
    expectInvalid("projects/atomic.bin", "projects/ATOMIC.BIN", TEMPORARY);
    expectInvalid(CURRENT, BACKUP, CURRENT);
    assert(files.releaseMutation(lease));

    std::cout << "[PASS] test_metadata_alias_and_nondistinct_paths_are_rejected\n";
}

void test_two_successive_transactions_reuse_bounded_slots() {
    resetTestRoot();
    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    assert(files.init());
    seedCurrent(files);
    assert(replace(files, NEW_DATA, sizeof(NEW_DATA)));
    assert(replace(files, FINAL_DATA, sizeof(FINAL_DATA)));
    assertFileEquals(files, FINAL_DATA, sizeof(FINAL_DATA));
    assert(missing(files, TEMPORARY));
    assert(missing(files, BACKUP));
    assertJournalBounded();

    std::cout << "[PASS] test_two_successive_transactions_reuse_bounded_slots\n";
}

void test_cooperative_commit_uses_one_bounded_durable_phase_per_advance() {
    static_assert(sizeof(core::persistence::ProductFileCommitPlan) <= 2048U);
    resetTestRoot();
    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    assert(files.init());
    seedCurrent(files);

    auto acquired = files.acquireMutation(ProductMutationOwner::FILESYSTEM_RPC);
    assert(acquired);
    auto lease = std::move(acquired.value());
    assert(core::persistence::writeProductFileTemp(
        files,
        lease,
        TEMPORARY,
        NEW_DATA,
        sizeof(NEW_DATA),
        sizeof(NEW_DATA)
    ));

    core::persistence::ProductFileCommitPlan plan;
    assert(plan.begin(
        files,
        lease,
        CURRENT,
        BACKUP,
        TEMPORARY,
        sizeof(NEW_DATA),
        core::persistence::checksum::crc32(NEW_DATA, sizeof(NEW_DATA))
    ));

    uint8_t advances = 0U;
    uint8_t scratch[core::persistence::PRODUCT_FILE_INTEGRITY_CHUNK_SIZE] = {};
    bool complete = false;
    while (!complete && advances < 32U) {
        core::persistence::ProductPersistenceWorkUsage usage{};
        oc::type::Result<bool> advanced = oc::type::Result<bool>::ok(false);
        {
            auto measuredResult = files.measurePersistenceWork(usage);
            assert(measuredResult);
            auto measured = std::move(measuredResult.value());
            advanced = plan.advance(files, lease, scratch, sizeof(scratch));
        }
        assert(advanced);
        complete = advanced.value();
        ++advances;
        assert(usage.bytes <=
               core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE.maxBytes());
        assert(usage.filesystemCalls <=
               core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE
                   .maxFilesystemCalls());
        assert(usage.allocations == 0U);
        assert(usage.entries == 0U);
        assert(usage.nodes == 0U);
    }

    assert(complete);
    assert(plan.complete());
    assert(plan.mapped());
    assert(plan.requiresRecoveryOnFailure());
    assert(advances > 1U);
    assert(files.releaseMutation(lease));
    assertFileEquals(files, NEW_DATA, sizeof(NEW_DATA));
    assert(missing(files, TEMPORARY));
    assert(missing(files, BACKUP));

    std::cout << "[PASS] cooperative commit phase quotas ("
              << static_cast<unsigned>(advances) << " advances)\n";
}

void test_cooperative_recovery_uses_one_bounded_durable_phase_per_advance() {
    static_assert(sizeof(core::persistence::ProductFileRecoveryPlan) <= 768U);
    uint32_t durableBoundaries = 0U;
    resetTestRoot();
    {
        BoundaryFaultFileSystem backend(testRoot().string().c_str());
        ProductFileService files(backend);
        assert(files.init());
        seedCurrent(files);
        backend.arm(0U, CutMode::BEFORE);
        assert(replace(files, NEW_DATA, sizeof(NEW_DATA)));
        durableBoundaries = backend.boundaryCount();
    }
    assert(durableBoundaries > 2U);

    resetTestRoot();
    {
        BoundaryFaultFileSystem backend(testRoot().string().c_str());
        ProductFileService files(backend);
        assert(files.init());
        seedCurrent(files);
        backend.arm(durableBoundaries - 1U, CutMode::AFTER);
        assert(!replace(files, NEW_DATA, sizeof(NEW_DATA)));
        assert(backend.cut());
    }

    oc::impl::HostFileSystem backend(testRoot().string().c_str());
    ProductFileService files(backend);
    assert(files.initForRecovery());
    auto acquired = files.beginRecovery();
    assert(acquired);
    auto lease = std::move(acquired.value());

    core::persistence::ProductFileRecoveryPlan plan;
    assert(plan.begin(files, lease));
    uint8_t advances = 0U;
    uint8_t scratch[core::persistence::PRODUCT_FILE_INTEGRITY_CHUNK_SIZE] = {};
    bool complete = false;
    while (!complete && advances < 32U) {
        core::persistence::ProductPersistenceWorkUsage usage{};
        oc::type::Result<bool> advanced = oc::type::Result<bool>::ok(false);
        {
            auto measuredResult = files.measurePersistenceWork(usage);
            assert(measuredResult);
            auto measured = std::move(measuredResult.value());
            advanced = plan.advance(files, lease, scratch, sizeof(scratch));
        }
        assert(advanced);
        complete = advanced.value();
        ++advances;
        assert(usage.bytes <=
               core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE.maxBytes());
        assert(usage.filesystemCalls <=
               core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE
                   .maxFilesystemCalls());
        assert(usage.allocations == 0U);
        assert(usage.entries == 0U);
        assert(usage.nodes == 0U);
    }

    assert(complete);
    assert(plan.complete());
    assert(advances > 1U);
    assert(files.completeRecovery(lease, true));
    assert(files.storageState() == ProductStorageState::READY);
    assertCanonicalAfterRecovery(files, true);

    std::cout << "[PASS] cooperative recovery phase quotas ("
              << static_cast<unsigned>(advances) << " advances)\n";
}

void test_cooperative_recovery_restores_newly_created_backup_after_bad_promotion() {
    resetTestRoot();
    createProductLayout();
    writeRaw(productPath("projects/atomic.bin"), OLD_DATA, sizeof(OLD_DATA));
    writeRaw(productPath("tmp/atomic.bin.tmp"), NEW_DATA, sizeof(NEW_DATA));
    const auto record = journalRecord(
        ProductFileTransactionPhase::PREPARED,
        true,
        53U,
        sizeof(NEW_DATA),
        core::persistence::checksum::crc32(NEW_DATA, sizeof(NEW_DATA))
    );
    writeRaw(
        productPath("tmp/rpc-product-file-a.journal"),
        record.data(),
        record.size()
    );

    BoundaryFaultFileSystem backend(testRoot().string().c_str());
    backend.corruptNextPromotion();
    ProductFileService files(backend);
    assert(files.initForRecovery());
    auto acquired = files.beginRecovery();
    assert(acquired);
    auto lease = std::move(acquired.value());

    core::persistence::ProductFileRecoveryPlan plan;
    assert(plan.begin(files, lease));
    uint8_t scratch[core::persistence::PRODUCT_FILE_INTEGRITY_CHUNK_SIZE] = {};
    uint8_t advances = 0U;
    while (plan.active() && advances < 64U) {
        auto advanced = plan.advance(files, lease, scratch, sizeof(scratch));
        assert(advanced);
        ++advances;
    }
    assert(plan.complete());
    assert(files.completeRecovery(lease, true));
    assertFileEquals(files, OLD_DATA, sizeof(OLD_DATA));
    assert(missing(files, TEMPORARY));
    assert(missing(files, BACKUP));

    std::cout << "[PASS] cooperative recovery restores newly created backup "
                 "after bad promotion\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "AtomicProductFile tests\n";
    std::cout << "==============================================\n\n";

    test_every_durable_boundary_recovers_old_or_new();
    test_single_corrupt_slot_is_cleaned();
    test_both_corrupt_slots_block_and_are_preserved();
    test_unsupported_journal_version_blocks_and_is_preserved();
    test_sequence_exhaustion_is_non_mutating();
    test_same_size_corrupt_temporary_is_rejected_before_mapping();
    test_backed_up_corrupt_temporary_restores_backup();
    test_promoted_corrupt_current_restores_backup();
    test_create_rollback_does_not_restore_stale_backup();
    test_metadata_alias_and_nondistinct_paths_are_rejected();
    test_two_successive_transactions_reuse_bounded_slots();
    test_cooperative_commit_uses_one_bounded_durable_phase_per_advance();
    test_cooperative_recovery_uses_one_bounded_durable_phase_per_advance();
    test_cooperative_recovery_restores_newly_created_backup_after_bad_promotion();

    resetTestRoot();
    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

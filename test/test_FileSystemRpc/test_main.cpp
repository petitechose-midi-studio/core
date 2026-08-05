#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <array>
#include <vector>

#include <filesystem>
#include <iostream>
#include <iterator>
#include <oc/impl/HostFileSystem.hpp>
#include <oc/interface/ITransport.hpp>

#include "../../src/persistence/AtomicProductFile.hpp"
#include "../../src/persistence/ProductFileRecoveryPlan.hpp"
#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/persistence/ProjectSessionAutosaveService.hpp"
#include "../../src/persistence/ProjectSessionStore.hpp"
#include "../../src/protocol/filesystem/FileSystemJobRpc.hpp"
#include "../../src/protocol/filesystem/FileSystemRpc.hpp"
#include "../../src/protocol/filesystem/FileSystemRpcConditionalPlan.hpp"
#include "../../src/protocol/filesystem/FileSystemRpcConditionalTransaction.hpp"
#include "../../src/protocol/filesystem/FileSystemRpcInternal.hpp"
#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/ProductFileTestMutation.hpp"

namespace {

using core::persistence::ProductDirectoryCatalog;
using core::persistence::ProductFileService;
using core::protocol::filesystem::FILESYSTEM_RPC_CONDITIONAL_JOURNAL_QUARANTINE_PATH;
using core::protocol::filesystem::FILESYSTEM_RPC_FEATURE_CAPABILITIES;
using core::protocol::filesystem::FILESYSTEM_RPC_FEATURE_CONDITIONAL_MUTATIONS;
using core::protocol::filesystem::FILESYSTEM_RPC_FEATURE_FILE_MANAGEMENT;
using core::protocol::filesystem::FILESYSTEM_RPC_FEATURE_PERSISTENCE_JOBS;
using core::protocol::filesystem::FILESYSTEM_RPC_FEATURE_WRITE_SESSIONS;
using core::protocol::filesystem::FILESYSTEM_RPC_MAX_CHUNK_SIZE;
using core::protocol::filesystem::FILESYSTEM_RPC_MAX_LIST_ENTRIES;
using core::protocol::filesystem::FILESYSTEM_RPC_MAX_UPLOAD_SIZE;
using core::protocol::filesystem::FILESYSTEM_RPC_RESPONSE_BUFFER_SIZE;
using core::protocol::filesystem::FILESYSTEM_RPC_SCHEMA;
using core::protocol::filesystem::FILESYSTEM_RPC_TOTAL_WRITE_TIMEOUT_MS;
using core::protocol::filesystem::FileSystemJobCommand;
using core::protocol::filesystem::FileSystemJobError;
using core::protocol::filesystem::FileSystemJobResponse;
using core::protocol::filesystem::FileSystemJobRpcCodec;
using core::protocol::filesystem::FileSystemJobState;
using core::protocol::filesystem::FileSystemRpcCodec;
using core::protocol::filesystem::FileSystemRpcConditionalRecoveryState;
using core::protocol::filesystem::FileSystemRpcEndpoint;
using core::protocol::filesystem::FileSystemRpcFileType;
using core::protocol::filesystem::FileSystemRpcHandler;
using core::protocol::filesystem::FileSystemRpcMessageId;
using core::protocol::filesystem::FileSystemRpcMutationOutcome;
using core::protocol::filesystem::FileSystemRpcMutationSubject;
using core::protocol::filesystem::FileSystemRpcStatus;

constexpr uint8_t SHA256_OLD[32] = {
    0xcb, 0xa0, 0x6b, 0x57, 0x36, 0xfa, 0xf6, 0x7e,
    0x54, 0xb0, 0x7b, 0x56, 0x1e, 0xae, 0x94, 0x39,
    0x5e, 0x77, 0x4c, 0x51, 0x7a, 0x7d, 0x91, 0x0a,
    0x54, 0x36, 0x9e, 0x12, 0x63, 0xcc, 0xfb, 0xd4,
};
constexpr uint8_t SHA256_NEW[32] = {
    0x11, 0x50, 0x7a, 0x0e, 0x2f, 0x5e, 0x69, 0xd5,
    0xdf, 0xa4, 0x0a, 0x62, 0xa1, 0xbd, 0x7b, 0x6e,
    0xe5, 0x7e, 0x6b, 0xcd, 0x85, 0xc6, 0x7c, 0x9b,
    0x84, 0x31, 0xb3, 0x6f, 0xff, 0x21, 0xc4, 0x37,
};
constexpr uint8_t SHA256_TAMPERED[32] = {
    0xd1, 0x21, 0xbe, 0x31, 0x03, 0x00, 0x7b, 0x41,
    0xed, 0xf9, 0x6f, 0x82, 0x62, 0x92, 0x5f, 0x8c,
    0x7d, 0x61, 0x89, 0x4a, 0xfe, 0x9a, 0x04, 0x18,
    0x43, 0xb6, 0x31, 0xf6, 0x94, 0x45, 0xbc, 0x57,
};
constexpr uint8_t SHA256_DELETE_ME[32] = {
    0xaf, 0x77, 0xc0, 0x87, 0x9d, 0x9c, 0xc6, 0x96,
    0x81, 0x60, 0x0b, 0xb1, 0x7f, 0xa8, 0xfe, 0xdc,
    0xcf, 0xe7, 0xae, 0x60, 0x5b, 0x65, 0x97, 0x4f,
    0xee, 0x79, 0x25, 0x81, 0xed, 0x63, 0xa8, 0x4c,
};

uint32_t journalCrc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

void appendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
    bytes.push_back(static_cast<uint8_t>(value >> 16U));
    bytes.push_back(static_cast<uint8_t>(value >> 24U));
}

void appendU16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8U));
}

std::vector<uint8_t> makeCanonicalLegacyRequest(FileSystemRpcMessageId messageId) {
    const char* name = FileSystemRpcCodec::messageName(messageId);
    const size_t nameLength = std::strlen(name);
    assert(nameLength <= UINT8_MAX);
    std::vector<uint8_t> frame;
    frame.reserve(5U + nameLength);
    frame.push_back(static_cast<uint8_t>(messageId));
    frame.push_back(static_cast<uint8_t>(nameLength));
    frame.insert(frame.end(), name, name + nameLength);
    frame.push_back(FILESYSTEM_RPC_SCHEMA);
    appendU16(frame, 0x1234U);
    return frame;
}

std::vector<uint8_t> makeJobRequest(FileSystemJobCommand command, uint16_t requestId,
                                    uint32_t clientNonce, uint32_t jobId, uint32_t totalDeadlineMs,
                                    const uint8_t* innerRequest = nullptr,
                                    size_t innerRequestSize = 0U) {
    using namespace core::protocol::filesystem;
    std::vector<uint8_t> frame;
    constexpr size_t nameLength = sizeof(FILESYSTEM_JOB_RPC_REQUEST_NAME) - 1U;
    frame.reserve(1U + 1U + nameLength + 1U + 2U + FILESYSTEM_JOB_RPC_REQUEST_HEADER_BYTES +
                  innerRequestSize);
    frame.push_back(FILESYSTEM_JOB_RPC_REQUEST_ID);
    frame.push_back(static_cast<uint8_t>(nameLength));
    frame.insert(frame.end(), FILESYSTEM_JOB_RPC_REQUEST_NAME,
                 FILESYSTEM_JOB_RPC_REQUEST_NAME + nameLength);
    frame.push_back(FILESYSTEM_JOB_RPC_SCHEMA);
    appendU16(frame, requestId);
    frame.push_back(static_cast<uint8_t>(command));
    frame.push_back(0U);
    appendU16(frame, 0U);
    appendU32(frame, clientNonce);
    appendU32(frame, jobId);
    appendU32(frame, totalDeadlineMs);
    if (innerRequestSize != 0U) {
        assert(innerRequest);
        frame.insert(frame.end(), innerRequest, innerRequest + innerRequestSize);
    }
    return frame;
}

std::vector<uint8_t> makeCrcInvalidConditionalDeleteJournal() {
    static constexpr uint8_t magic[] = {'F', 'S', 'T', 'X'};
    static constexpr uint8_t zeroDigest[32] = {};
    static constexpr char currentPath[] = "projects/preserved.bin";

    std::vector<uint8_t> bytes;
    bytes.insert(bytes.end(), std::begin(magic), std::end(magic));
    bytes.push_back(1);  // Journal version.
    bytes.push_back(2);  // Conditional delete.
    appendU32(bytes, 0x10203040U);
    bytes.insert(bytes.end(), std::begin(SHA256_OLD), std::end(SHA256_OLD));
    bytes.insert(bytes.end(), std::begin(zeroDigest), std::end(zeroDigest));
    bytes.push_back(static_cast<uint8_t>(sizeof(currentPath) - 1U));
    bytes.insert(bytes.end(), currentPath, currentPath + sizeof(currentPath) - 1U);
    bytes.push_back(0);  // Delete journals have no staging path.
    appendU32(bytes, journalCrc32(bytes.data(), bytes.size()));
    bytes.back() ^= 0x80U;
    return bytes;
}

uint32_t g_now_ms = 0;

uint32_t nowMs() {
    return g_now_ms;
}

struct FakeTransport : oc::interface::ITransport {
    ReceiveCallback onReceive;
    uint8_t sent[1024] = {};
    size_t sentSize = 0;
    uint32_t sendCount = 0;

    oc::type::Result<void> init() override {
        return oc::type::Result<void>::ok();
    }

    void update() override {}

    void send(const uint8_t* data, size_t length) override {
        assert(data);
        assert(length <= sizeof(sent));
        std::memcpy(sent, data, length);
        sentSize = length;
        ++sendCount;
    }

    void setOnReceive(ReceiveCallback cb) override {
        onReceive = cb;
    }

    void emit(const uint8_t* data, size_t size) {
        assert(onReceive);
        onReceive(data, size);
    }
};

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() / "midi-studio-core-filesystem-rpc-test";
}

void resetTestRoot() {
    std::error_code ec;
    std::filesystem::remove_all(testRoot(), ec);
}

struct Harness {
    oc::impl::HostFileSystem filesystem;
    ProductFileService service;
    ProductDirectoryCatalog catalog;
    FileSystemRpcHandler handler;
    uint8_t request[1024] = {};
    uint8_t response[1024] = {};

    Harness()
        : filesystem(testRoot().string().c_str()),
          service(filesystem),
          catalog(service),
          handler(service, catalog, FileSystemRpcHandler::Config{100}) {
        auto init = service.init();
        assert(init);
    }

    size_t transact(size_t requestSize, uint32_t nowMs = 0) {
        auto handled = handler.handleFrame(
            request,
            requestSize,
            nowMs,
            response,
            sizeof(response)
        );
        assert(handled);
        assert(handled.value() > 0);
        return handled.value();
    }
};

void assertProductFileEquals(
    ProductFileService& service,
    const char* path,
    const uint8_t* expected,
    size_t expectedSize
) {
    const auto info = service.stat(path);
    assert(info);
    assert(info.value().type == oc::interface::FileType::FILE);
    assert(info.value().sizeBytes == expectedSize);
    std::vector<uint8_t> actual(expectedSize);
    if (expectedSize > 0) {
        const auto read = service.read(path, 0, actual.data(), actual.size());
        assert(read && read.value() == expectedSize);
        assert(std::memcmp(actual.data(), expected, expectedSize) == 0);
    }
}

void assertProductReadBlocked(
    ProductFileService& service,
    const char* path
) {
    const auto info = service.stat(path);
    assert(!info);
    assert(info.error().code == oc::type::ErrorCode::HARDWARE_BUSY);
}

void completeExternalProductRecovery(ProductFileService& service) {
    namespace conditional =
        core::protocol::filesystem::conditional_mutation;
    auto acquired = service.beginRecovery();
    assert(acquired);
    auto lease = std::move(acquired.value());
    assert(service.ensureLayout(lease));

    core::persistence::ProductFileRecoveryPlan ordinary;
    assert(ordinary.begin(service, lease));
    std::array<uint8_t, core::persistence::PRODUCT_FILE_INTEGRITY_CHUNK_SIZE>
        ordinaryScratch{};
    while (ordinary.active()) {
        const auto advanced = ordinary.advance(
            service,
            lease,
            ordinaryScratch.data(),
            ordinaryScratch.size()
        );
        assert(advanced);
    }
    assert(ordinary.complete());

    conditional::Journal journal{};
    bool present = false;
    bool corrupt = false;
    const auto loaded = conditional::readJournal(
        service,
        lease,
        journal,
        present,
        corrupt
    );
    bool quarantined = false;
    if (loaded != FileSystemRpcStatus::OK) {
        assert(corrupt);
        assert(conditional::recoverPendingMutation(
            service,
            lease,
            quarantined
        ) == FileSystemRpcStatus::OK);
    } else {
        assert(conditional::removeIfExists(
            service,
            lease,
            conditional::JOURNAL_STAGING_PATH
        ) == FileSystemRpcStatus::OK);
        if (present) {
            conditional::ConditionalMutationPlan plan;
            assert(plan.beginRecovery(service, lease, journal));
            std::array<uint8_t, FILESYSTEM_RPC_MAX_CHUNK_SIZE> scratch{};
            while (plan.active()) {
                const auto workClass = plan.nextWorkClass();
                const auto quota = workClass ==
                        conditional::ConditionalPlanWorkClass::ORDINARY_IO
                    ? core::persistence::PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO
                    : core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE;
                core::persistence::ProductPersistenceWorkUsage usage{};
                {
                    auto measuredResult = service.measurePersistenceWork(usage);
                    assert(measuredResult);
                    auto measurement = std::move(measuredResult.value());
                    (void)plan.advance(
                        service,
                        scratch.data(),
                        scratch.size()
                    );
                }
                assert(usage.bytes <= quota.maxBytes());
                assert(usage.filesystemCalls <= quota.maxFilesystemCalls());
            }
            assert(plan.terminal());
            assert(plan.status() == FileSystemRpcStatus::OK);
        }
    }
    assert(service.completeRecovery(lease, true));
}

void assertCorruptJournalIsQuarantinedOnce(
    const uint8_t* journal,
    size_t journalSize
) {
    resetTestRoot();
    Harness h;
    static constexpr uint8_t projectData[] = {'m', 'u', 's', 'i', 'c'};
    static constexpr uint8_t backupData[] = {'b', 'a', 'c', 'k', 'u', 'p'};
    static constexpr uint8_t stagingData[] = {'s', 't', 'a', 'g', 'e'};
    static constexpr uint8_t staleEvidence[] = {'o', 'l', 'd'};
    assert(core::test::writeProductFileFixture(
        h.service,
        "projects/preserved.bin",
        0,
        projectData,
        sizeof(projectData)
    ));
    assert(core::test::writeProductFileFixture(
        h.service,
        "tmp/rpc-conditional.backup",
        0,
        backupData,
        sizeof(backupData)
    ));
    assert(core::test::writeProductFileFixture(
        h.service,
        "tmp/user-step-preset-stage.mssp",
        0,
        stagingData,
        sizeof(stagingData)
    ));
    assert(core::test::writeProductFileFixture(
        h.service,
        FILESYSTEM_RPC_CONDITIONAL_JOURNAL_QUARANTINE_PATH,
        0,
        staleEvidence,
        sizeof(staleEvidence)
    ));
    assert(core::test::writeProductFileFixture(
        h.service,
        "tmp/rpc-conditional.journal",
        0,
        journal,
        journalSize
    ));

    assert(h.handler.conditionalRecoveryState() ==
           FileSystemRpcConditionalRecoveryState::NOT_CHECKED);
    size_t requestSize = FileSystemRpcCodec::encodeCapabilitiesRequest(
        80,
        h.request,
        sizeof(h.request)
    );
    size_t responseSize = h.transact(requestSize);
    const auto capabilities = FileSystemRpcCodec::decodeCapabilitiesResponse(
        h.response,
        responseSize
    );
    assert(capabilities && capabilities.value().status == FileSystemRpcStatus::OK);
    assert(h.handler.conditionalRecoveryState() ==
           FileSystemRpcConditionalRecoveryState::CORRUPT_JOURNAL_QUARANTINED);
    assert(h.handler.conditionalRecoveryStatus() == FileSystemRpcStatus::OK);

    assert(!h.service.stat("tmp/rpc-conditional.journal"));
    assertProductFileEquals(
        h.service,
        FILESYSTEM_RPC_CONDITIONAL_JOURNAL_QUARANTINE_PATH,
        journal,
        journalSize
    );
    assertProductFileEquals(
        h.service,
        "projects/preserved.bin",
        projectData,
        sizeof(projectData)
    );
    assertProductFileEquals(
        h.service,
        "tmp/rpc-conditional.backup",
        backupData,
        sizeof(backupData)
    );
    assertProductFileEquals(
        h.service,
        "tmp/user-step-preset-stage.mssp",
        stagingData,
        sizeof(stagingData)
    );

    requestSize = FileSystemRpcCodec::encodeStatRequest(
        81,
        "projects/preserved.bin",
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize);
    const auto stat = FileSystemRpcCodec::decodeStatResponse(h.response, responseSize);
    assert(stat && stat.value().status == FileSystemRpcStatus::OK);
    assert(stat.value().sizeBytes == sizeof(projectData));

    requestSize = FileSystemRpcCodec::encodeReadRequest(
        82,
        "projects/preserved.bin",
        0,
        sizeof(projectData),
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize);
    const auto read = FileSystemRpcCodec::decodeReadResponse(h.response, responseSize);
    assert(read && read.value().status == FileSystemRpcStatus::OK);
    assert(read.value().bytesRead == sizeof(projectData));
    assert(std::memcmp(read.value().data, projectData, sizeof(projectData)) == 0);

    // A fresh handler sees no durable journal to replay. The fixed quarantine
    // evidence remains intact, so the same corruption cannot loop on reboot.
    FileSystemRpcHandler restarted(h.service, h.catalog);
    requestSize = FileSystemRpcCodec::encodeCapabilitiesRequest(
        83,
        h.request,
        sizeof(h.request)
    );
    const auto handled = restarted.handleFrame(
        h.request,
        requestSize,
        0,
        h.response,
        sizeof(h.response)
    );
    assert(handled);
    const auto restartedCapabilities = FileSystemRpcCodec::decodeCapabilitiesResponse(
        h.response,
        handled.value()
    );
    assert(restartedCapabilities &&
           restartedCapabilities.value().status == FileSystemRpcStatus::OK);
    assert(restarted.conditionalRecoveryState() ==
           FileSystemRpcConditionalRecoveryState::READY);
    assertProductFileEquals(
        h.service,
        FILESYSTEM_RPC_CONDITIONAL_JOURNAL_QUARANTINE_PATH,
        journal,
        journalSize
    );
}

struct FaultInjectingFileSystem : oc::interface::IFileSystem {
    explicit FaultInjectingFileSystem(const char* rootPath)
        : delegate(rootPath) {}

    void resetWorkCounters() {
        filesystemCalls = 0U;
        ioBytes = 0U;
    }

    oc::type::Result<void> init() override {
        ++filesystemCalls;
        return delegate.init();
    }
    bool available() const override { return delegate.available(); }
    oc::type::Result<oc::interface::FileInfo> stat(const char* path) override {
        ++filesystemCalls;
        if (failFinalStat && path &&
            std::strcmp(path, "/midi-studio/projects/stat-fail.bin") == 0) {
            return oc::type::Result<oc::interface::FileInfo>::err(
                {oc::type::ErrorCode::STORAGE_READ_FAILED, "forced final stat failure"}
            );
        }
        return delegate.stat(path);
    }
    oc::type::Result<void> list(
        const char* path,
        oc::interface::DirectoryEntryVisitor visitor,
        void* context
    ) override {
        ++filesystemCalls;
        return delegate.list(path, visitor, context);
    }
    oc::type::Result<void> createDirectory(const char* path) override {
        ++filesystemCalls;
        return delegate.createDirectory(path);
    }
    oc::type::Result<void> remove(
        const char* path,
        oc::interface::RemoveMode mode = oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
    ) override {
        ++filesystemCalls;
        if (failConditionalBackupRemove && path &&
            std::strcmp(path, "/midi-studio/tmp/rpc-conditional.backup") == 0) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "forced conditional cleanup failure"}
            );
        }
        return delegate.remove(path, mode);
    }
    oc::type::Result<void> rename(const char* fromPath, const char* toPath) override {
        ++filesystemCalls;
        if (failConditionalJournalPromotion && fromPath && toPath &&
            std::strcmp(
                fromPath,
                "/midi-studio/tmp/rpc-conditional.journal.tmp"
            ) == 0 &&
            std::strcmp(toPath, "/midi-studio/tmp/rpc-conditional.journal") == 0) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::STORAGE_WRITE_FAILED,
                 "forced conditional journal promotion failure"}
            );
        }
        if (failTmpPromotion && fromPath && toPath &&
            std::strstr(fromPath, "/midi-studio/tmp/rpc-write-") != nullptr &&
            std::strstr(toPath, "/midi-studio/projects/") != nullptr) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "forced tmp promotion failure"}
            );
        }
        if (failBackupRestore && fromPath && toPath &&
            std::strstr(fromPath, "/midi-studio/tmp/rpc-backup-") != nullptr &&
            std::strstr(toPath, "/midi-studio/projects/") != nullptr) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "forced backup restore failure"}
            );
        }
        if (failConditionalPromotion && fromPath && toPath &&
            std::strcmp(fromPath, "/midi-studio/tmp/step-preset-stage.mssp") == 0 &&
            std::strcmp(toPath, "/midi-studio/library/step-presets/demo.mssp") == 0) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "forced conditional promotion failure"}
            );
        }
        if (failConditionalRestore && fromPath && toPath &&
            std::strcmp(fromPath, "/midi-studio/tmp/rpc-conditional.backup") == 0 &&
            std::strcmp(toPath, "/midi-studio/library/step-presets/demo.mssp") == 0) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "forced conditional restore failure"}
            );
        }
        return delegate.rename(fromPath, toPath);
    }
    oc::type::Result<size_t> read(
        const char* path,
        uint32_t offset,
        uint8_t* buffer,
        size_t size
    ) override {
        ++filesystemCalls;
        auto result = delegate.read(path, offset, buffer, size);
        if (result) ioBytes += result.value();
        return result;
    }
    oc::type::Result<size_t> write(
        const char* path,
        uint32_t offset,
        const uint8_t* data,
        size_t size
    ) override {
        ++filesystemCalls;
        auto result = delegate.write(path, offset, data, size);
        if (result) ioBytes += result.value();
        return result;
    }
    oc::type::Result<void> flush(const char* path) override {
        ++filesystemCalls;
        return delegate.flush(path);
    }
    oc::type::Result<void> beginWrite(const char* path, uint32_t expectedSize) override {
        ++filesystemCalls;
        corruptingUploadStream = corruptUploadAppend && path &&
            std::strstr(path, "/midi-studio/tmp/rpc-write-") != nullptr;
        return delegate.beginWrite(path, expectedSize);
    }
    oc::type::Result<size_t> appendWrite(const uint8_t* data, size_t size) override {
        ++filesystemCalls;
        if (corruptingUploadStream && size > 0U) {
            assert(size <= FILESYSTEM_RPC_MAX_CHUNK_SIZE);
            std::array<uint8_t, FILESYSTEM_RPC_MAX_CHUNK_SIZE> corrupted{};
            std::memcpy(corrupted.data(), data, size);
            corrupted[0] ^= 0x80U;
            auto result = delegate.appendWrite(corrupted.data(), size);
            if (result) ioBytes += result.value();
            return result;
        }
        if (!shortAppend || size == 0) {
            auto result = delegate.appendWrite(data, size);
            if (result) ioBytes += result.value();
            return result;
        }
        const size_t shortSize = size - 1U;
        auto written = delegate.appendWrite(data, shortSize);
        if (!written) {
            return written;
        }
        ioBytes += written.value();
        return oc::type::Result<size_t>::ok(shortSize);
    }
    oc::type::Result<void> finishWrite() override {
        ++filesystemCalls;
        auto result = delegate.finishWrite();
        corruptingUploadStream = false;
        return result;
    }
    void abortWrite() override {
        ++filesystemCalls;
        delegate.abortWrite();
        corruptingUploadStream = false;
    }

    oc::impl::HostFileSystem delegate;
    bool shortAppend = false;
    bool corruptUploadAppend = false;
    bool corruptingUploadStream = false;
    bool failFinalStat = false;
    bool failTmpPromotion = false;
    bool failBackupRestore = false;
    bool failConditionalPromotion = false;
    bool failConditionalRestore = false;
    bool failConditionalBackupRemove = false;
    bool failConditionalJournalPromotion = false;
    uint32_t filesystemCalls = 0U;
    size_t ioBytes = 0U;
};

FileSystemRpcStatus writeFileViaRpc(FileSystemRpcHandler& handler, uint16_t sessionId,
                                    const char* path, const uint8_t* data, uint16_t size) {
    uint8_t request[1024] = {};
    uint8_t response[1024] = {};
    size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(100, sessionId, path, size,
                                                                     request, sizeof(request));
    assert(requestSize > 0);
    auto handled = handler.handleFrame(request, requestSize, 10, response, sizeof(response));
    assert(handled);
    auto write = FileSystemRpcCodec::decodeWriteResponse(response, handled.value());
    assert(write && write.value().status == FileSystemRpcStatus::OK);

    if (size > 0) {
        requestSize = FileSystemRpcCodec::encodeWriteChunkRequest(101, sessionId, 0, data, size,
                                                                  request, sizeof(request));
        assert(requestSize > 0);
        handled = handler.handleFrame(request, requestSize, 20, response, sizeof(response));
        assert(handled);
        write = FileSystemRpcCodec::decodeWriteResponse(response, handled.value());
        assert(write && write.value().status == FileSystemRpcStatus::OK);
    }

    requestSize =
        FileSystemRpcCodec::encodeWriteCommitRequest(102, sessionId, request, sizeof(request));
    assert(requestSize > 0);
    handled = handler.handleFrame(request, requestSize, 30, response, sizeof(response));
    assert(handled);
    write = FileSystemRpcCodec::decodeWriteResponse(response, handled.value());
    assert(write);
    return write.value().status;
}

bool listContains(const core::protocol::filesystem::FileSystemRpcListResponse& response,
                  const char* name) {
    for (uint8_t i = 0; i < response.entryCount; ++i) {
        if (std::strcmp(response.entries[i].name, name) == 0) { return true; }
    }
    return false;
}

void test_job_codec_matches_bridge_golden_vectors() {
    using namespace core::protocol::filesystem;
    constexpr std::array<uint8_t, 33> requestGolden = {
        0xFC, 0x0C, 0x46, 0x73, 0x4A, 0x6F, 0x62, 0x52, 0x65, 0x71, 0x75,
        0x65, 0x73, 0x74, 0x01, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const auto request =
        FileSystemJobRpcCodec::decodeRequest(requestGolden.data(), requestGolden.size());
    assert(request);
    assert(request.value().requestId == 0x1234U);
    assert(request.value().command == FileSystemJobCommand::CAPABILITIES);
    assert(request.value().clientNonce == 0U);
    assert(request.value().jobId == 0U);
    assert(request.value().totalDeadlineMs == 0U);
    assert(request.value().innerRequestSize == 0U);
    assert(FileSystemJobRpcCodec::isJobRequestId(0xFCU));
    assert(!FileSystemJobRpcCodec::isJobRequestId(0xFBU));

    FileSystemJobResponse capabilities{};
    capabilities.requestId = 0x1234U;
    capabilities.command = FileSystemJobCommand::CAPABILITIES;
    std::array<uint8_t, 96> encoded{};
    const auto encodedSize =
        FileSystemJobRpcCodec::encodeResponse(capabilities, encoded.data(), encoded.size());
    assert(encodedSize);
    constexpr std::array<uint8_t, 62> responseGolden = {
        0xFD, 0x0D, 0x46, 0x73, 0x4A, 0x6F, 0x62, 0x52, 0x65, 0x73, 0x70, 0x6F, 0x6E,
        0x73, 0x65, 0x01, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x02, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x7F,
        0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x30, 0x75, 0x00, 0x00,
    };
    assert(encodedSize.value() == responseGolden.size());
    assert(std::memcmp(encoded.data(), responseGolden.data(), responseGolden.size()) == 0);

    const auto decoded = FileSystemJobRpcCodec::decodeResponse(encoded.data(), encodedSize.value());
    assert(decoded);
    assert(decoded.value().requestId == 0x1234U);
    assert(decoded.value().command == FileSystemJobCommand::CAPABILITIES);
    assert(decoded.value().body == encoded.data() + 38U);
    assert(decoded.value().bodySize == FILESYSTEM_JOB_RPC_CAPABILITIES_BYTES);

    constexpr uint8_t abc[] = {'a', 'b', 'c'};
    constexpr std::array<uint8_t, FILESYSTEM_RPC_SHA256_SIZE> abcSha256 = {
        0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA, 0x41, 0x41, 0x40,
        0xDE, 0x5D, 0xAE, 0x22, 0x23, 0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17,
        0x7A, 0x9C, 0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD,
    };
    std::array<uint8_t, FILESYSTEM_RPC_SHA256_SIZE> digest{};
    assert(core::protocol::filesystem::conditional_mutation::hashBytes(abc, sizeof(abc),
                                                                       digest.data()));
    assert(digest == abcSha256);

    std::cout << "[PASS] job codec matches Bridge golden capability vectors\n";
}

void test_job_codec_start_subset_and_request_validation() {
    using namespace core::protocol::filesystem;
    constexpr std::array<FileSystemRpcMessageId, 6> supported = {
        FileSystemRpcMessageId::WRITE_COMMIT_REQUEST,
        FileSystemRpcMessageId::MKDIR_REQUEST,
        FileSystemRpcMessageId::DELETE_REQUEST,
        FileSystemRpcMessageId::RENAME_REQUEST,
        FileSystemRpcMessageId::CONDITIONAL_REPLACE_REQUEST,
        FileSystemRpcMessageId::CONDITIONAL_DELETE_REQUEST,
    };
    constexpr std::array<FileSystemRpcMessageId, 7> ordinary = {
        FileSystemRpcMessageId::STAT_REQUEST,         FileSystemRpcMessageId::LIST_REQUEST,
        FileSystemRpcMessageId::READ_REQUEST,         FileSystemRpcMessageId::WRITE_BEGIN_REQUEST,
        FileSystemRpcMessageId::WRITE_CHUNK_REQUEST,  FileSystemRpcMessageId::WRITE_ABORT_REQUEST,
        FileSystemRpcMessageId::CAPABILITIES_REQUEST,
    };
    for (const auto messageId : supported) {
        const auto inner = makeCanonicalLegacyRequest(messageId);
        assert(FileSystemJobRpcCodec::isSupportedStartRequest(inner.data(), inner.size()));
    }
    for (const auto messageId : ordinary) {
        const auto inner = makeCanonicalLegacyRequest(messageId);
        assert(!FileSystemJobRpcCodec::isSupportedStartRequest(inner.data(), inner.size()));
    }

    const auto inner = makeCanonicalLegacyRequest(FileSystemRpcMessageId::DELETE_REQUEST);
    const auto start =
        makeJobRequest(FileSystemJobCommand::START, 7U, 0x10203040U, 0U,
                       FILESYSTEM_JOB_RPC_MAX_DEADLINE_MS, inner.data(), inner.size());
    const auto decoded = FileSystemJobRpcCodec::decodeRequest(start.data(), start.size());
    assert(decoded);
    assert(decoded.value().requestId == 7U);
    assert(decoded.value().command == FileSystemJobCommand::START);
    assert(decoded.value().clientNonce == 0x10203040U);
    assert(decoded.value().jobId == 0U);
    assert(decoded.value().totalDeadlineMs == FILESYSTEM_JOB_RPC_MAX_DEADLINE_MS);
    assert(decoded.value().innerRequest == start.data() + 33U);
    assert(decoded.value().innerRequestSize == inner.size());

    constexpr size_t applicationOffset = 17U;
    auto malformed = start;
    malformed[0] = FILESYSTEM_JOB_RPC_RESPONSE_ID;
    assert(!FileSystemJobRpcCodec::decodeRequest(malformed.data(), malformed.size()));
    malformed = start;
    malformed[2] ^= 0x01U;
    assert(!FileSystemJobRpcCodec::decodeRequest(malformed.data(), malformed.size()));
    malformed = start;
    malformed[14] = 2U;
    assert(!FileSystemJobRpcCodec::decodeRequest(malformed.data(), malformed.size()));
    malformed = start;
    malformed[applicationOffset] = 0xFFU;
    assert(!FileSystemJobRpcCodec::decodeRequest(malformed.data(), malformed.size()));
    malformed = start;
    malformed[applicationOffset + 1U] = 1U;
    assert(!FileSystemJobRpcCodec::decodeRequest(malformed.data(), malformed.size()));
    malformed = start;
    malformed[applicationOffset + 2U] = 1U;
    assert(!FileSystemJobRpcCodec::decodeRequest(malformed.data(), malformed.size()));
    malformed = start;
    std::fill(malformed.begin() + applicationOffset + 4U,
              malformed.begin() + applicationOffset + 8U, 0U);
    assert(!FileSystemJobRpcCodec::decodeRequest(malformed.data(), malformed.size()));

    const auto zeroDeadline =
        makeJobRequest(FileSystemJobCommand::START, 8U, 1U, 0U, 0U, inner.data(), inner.size());
    assert(!FileSystemJobRpcCodec::decodeRequest(zeroDeadline.data(), zeroDeadline.size()));
    const auto excessiveDeadline =
        makeJobRequest(FileSystemJobCommand::START, 8U, 1U, 0U,
                       FILESYSTEM_JOB_RPC_MAX_DEADLINE_MS + 1U, inner.data(), inner.size());
    assert(
        !FileSystemJobRpcCodec::decodeRequest(excessiveDeadline.data(), excessiveDeadline.size()));
    const auto pollWithBody =
        makeJobRequest(FileSystemJobCommand::POLL, 9U, 1U, 2U, 0U, inner.data(), inner.size());
    assert(!FileSystemJobRpcCodec::decodeRequest(pollWithBody.data(), pollWithBody.size()));
    for (size_t truncated = 0U; truncated < 33U; ++truncated) {
        assert(!FileSystemJobRpcCodec::decodeRequest(start.data(), truncated));
    }

    std::cout << "[PASS] job codec start subset and request validation\n";
}

void test_job_codec_response_semantics_and_retention() {
    using namespace core::protocol::filesystem;
    std::array<uint8_t, 96> encoded{};
    FileSystemJobResponse accepted{};
    accepted.requestId = 10U;
    accepted.command = FileSystemJobCommand::START;
    accepted.state = FileSystemJobState::ACCEPTED;
    accepted.clientNonce = 0xABCDEF01U;
    accepted.jobId = 42U;
    accepted.retryAfterMs = FILESYSTEM_JOB_RPC_RETRY_AFTER_MS;
    auto encodedSize =
        FileSystemJobRpcCodec::encodeResponse(accepted, encoded.data(), encoded.size());
    assert(encodedSize);
    auto decoded = FileSystemJobRpcCodec::decodeResponse(encoded.data(), encodedSize.value());
    assert(decoded);
    assert(decoded.value().state == FileSystemJobState::ACCEPTED);
    assert(decoded.value().jobId == 42U);

    std::array<uint8_t, 96> legacyResponse{};
    const size_t legacyResponseSize = core::protocol::filesystem::internal::encodeStatusOnly(
        FileSystemRpcMessageId::DELETE_RESPONSE, 99U, FileSystemRpcStatus::OK,
        legacyResponse.data(), legacyResponse.size());
    assert(legacyResponseSize > 0U);
    FileSystemJobResponse completed{};
    completed.requestId = 11U;
    completed.command = FileSystemJobCommand::POLL;
    completed.state = FileSystemJobState::COMPLETED;
    completed.flags = FILESYSTEM_JOB_RPC_FLAG_TERMINAL_RETAINED;
    completed.clientNonce = 0xABCDEF01U;
    completed.jobId = 42U;
    completed.progressPerMille = FILESYSTEM_JOB_RPC_MAX_PROGRESS_PER_MILLE;
    completed.body = legacyResponse.data();
    completed.bodySize = legacyResponseSize;
    encodedSize = FileSystemJobRpcCodec::encodeResponse(completed, encoded.data(), encoded.size());
    assert(encodedSize);
    decoded = FileSystemJobRpcCodec::decodeResponse(encoded.data(), encodedSize.value());
    assert(decoded);
    assert(decoded.value().state == FileSystemJobState::COMPLETED);
    assert(decoded.value().bodySize == legacyResponseSize);
    assert(std::memcmp(decoded.value().body, legacyResponse.data(), legacyResponseSize) == 0);

    auto invalid = completed;
    invalid.flags = 0x80U;
    assert(!FileSystemJobRpcCodec::encodeResponse(invalid, encoded.data(), encoded.size()));
    invalid = completed;
    invalid.body = nullptr;
    invalid.bodySize = 0U;
    assert(!FileSystemJobRpcCodec::encodeResponse(invalid, encoded.data(), encoded.size()));
    invalid = completed;
    invalid.state = FileSystemJobState::CANCELLED;
    invalid.flags = 0U;
    invalid.body = nullptr;
    invalid.bodySize = 0U;
    assert(!FileSystemJobRpcCodec::encodeResponse(invalid, encoded.data(), encoded.size()));
    invalid = accepted;
    invalid.state = FileSystemJobState::REJECTED;
    assert(!FileSystemJobRpcCodec::encodeResponse(invalid, encoded.data(), encoded.size()));
    std::array<uint8_t, 8> tooSmall{};
    assert(!FileSystemJobRpcCodec::encodeResponse(accepted, tooSmall.data(), tooSmall.size()));

    FileSystemJobResponse capabilities{};
    capabilities.requestId = 12U;
    encodedSize =
        FileSystemJobRpcCodec::encodeResponse(capabilities, encoded.data(), encoded.size());
    assert(encodedSize);
    auto malformed = encoded;
    malformed[42U] |= 0x40U;
    assert(!FileSystemJobRpcCodec::decodeResponse(malformed.data(), encodedSize.value()));

    assert(FileSystemJobRpcCodec::terminalRetained(40'000U, 10'000U));
    assert(!FileSystemJobRpcCodec::terminalRetained(40'001U, 10'000U));
    assert(FileSystemJobRpcCodec::terminalRetained(29'995U, UINT32_MAX - 4U));
    assert(!FileSystemJobRpcCodec::terminalRetained(29'996U, UINT32_MAX - 4U));

    std::cout << "[PASS] job codec response semantics and terminal retention\n";
}

void test_endpoint_job_control_is_immediate_bounded_and_io_free() {
    using namespace core::protocol::filesystem;
    resetTestRoot();
    g_now_ms = 100U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    core::persistence::ProductPersistenceWorkUsage receiveUsage{};
    auto measuredResult = service.measurePersistenceWork(receiveUsage);
    assert(measuredResult);
    {
        auto measured = std::move(measuredResult.value());
        const auto capabilities =
            makeJobRequest(FileSystemJobCommand::CAPABILITIES, 80U, 0U, 0U, 0U);
        transport.emit(capabilities.data(), capabilities.size());
        assert(transport.sendCount == 1U);
        const auto decodedCapabilities =
            FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(decodedCapabilities);
        assert(decodedCapabilities.value().requestId == 80U);
        assert(decodedCapabilities.value().command == FileSystemJobCommand::CAPABILITIES);

        std::array<uint8_t, 128> innerOne{};
        const size_t innerOneSize = FileSystemRpcCodec::encodeMkdirRequest(
            81U, "projects/job-one", innerOne.data(), innerOne.size());
        assert(innerOneSize > 0U);
        const auto startOne = makeJobRequest(FileSystemJobCommand::START, 81U, 0x10000001U, 0U,
                                             1'000U, innerOne.data(), innerOneSize);
        transport.emit(startOne.data(), startOne.size());
        assert(transport.sendCount == 2U);
        auto response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(response);
        assert(response.value().state == FileSystemJobState::ACCEPTED);
        assert(response.value().clientNonce == 0x10000001U);
        const uint32_t firstJobId = response.value().jobId;
        assert(firstJobId != 0U);

        std::array<uint8_t, 128> innerTwo{};
        const size_t innerTwoSize = FileSystemRpcCodec::encodeMkdirRequest(
            82U, "projects/job-two", innerTwo.data(), innerTwo.size());
        assert(innerTwoSize > 0U);
        const auto startTwo = makeJobRequest(FileSystemJobCommand::START, 82U, 0x10000002U, 0U,
                                             1'000U, innerTwo.data(), innerTwoSize);
        transport.emit(startTwo.data(), startTwo.size());
        assert(transport.sendCount == 3U);
        response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(response && response.value().state == FileSystemJobState::ACCEPTED);
        assert(response.value().jobId != firstJobId);
        assert(service.persistenceJobs().depth() == 2U);

        std::array<uint8_t, 128> innerThree{};
        const size_t innerThreeSize = FileSystemRpcCodec::encodeMkdirRequest(
            83U, "projects/job-three", innerThree.data(), innerThree.size());
        assert(innerThreeSize > 0U);
        const auto startThree = makeJobRequest(FileSystemJobCommand::START, 83U, 0x10000003U, 0U,
                                               1'000U, innerThree.data(), innerThreeSize);
        transport.emit(startThree.data(), startThree.size());
        response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(response);
        assert(response.value().state == FileSystemJobState::REJECTED);
        assert(response.value().error == FileSystemJobError::RESOURCE_EXHAUSTED);

        auto duplicate = startOne;
        duplicate[15U] = 84U;
        duplicate[16U] = 0U;
        transport.emit(duplicate.data(), duplicate.size());
        response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(response);
        assert(response.value().jobId == firstJobId);
        assert(response.value().state == FileSystemJobState::PENDING);
        assert((response.value().flags & FILESYSTEM_JOB_RPC_FLAG_DUPLICATE_START) != 0U);

        auto conflictInner = innerOne;
        conflictInner[innerOneSize - 1U] ^= 0x01U;
        const auto conflict = makeJobRequest(FileSystemJobCommand::START, 85U, 0x10000001U, 0U,
                                             1'000U, conflictInner.data(), innerOneSize);
        transport.emit(conflict.data(), conflict.size());
        response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(response);
        assert(response.value().state == FileSystemJobState::REJECTED);
        assert(response.value().error == FileSystemJobError::CONFLICT);
        assert(response.value().jobId == firstJobId);

        const auto wrongTuple =
            makeJobRequest(FileSystemJobCommand::POLL, 86U, 0x10000001U, firstJobId + 1U, 0U);
        transport.emit(wrongTuple.data(), wrongTuple.size());
        response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(response);
        assert(response.value().state == FileSystemJobState::REJECTED);
        assert(response.value().error == FileSystemJobError::NOT_FOUND);

        const auto poll =
            makeJobRequest(FileSystemJobCommand::POLL, 87U, 0x10000001U, firstJobId, 0U);
        transport.emit(poll.data(), poll.size());
        response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(response);
        assert(response.value().state == FileSystemJobState::PENDING);
        assert(response.value().retryAfterMs == FILESYSTEM_JOB_RPC_RETRY_AFTER_MS);

        const auto cancel =
            makeJobRequest(FileSystemJobCommand::CANCEL, 88U, 0x10000001U, firstJobId, 0U);
        transport.emit(cancel.data(), cancel.size());
        response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(response);
        assert(response.value().state == FileSystemJobState::CANCEL_PENDING);
    }
    assert(receiveUsage.filesystemCalls == 0U);
    assert(receiveUsage.bytes == 0U);
    assert(receiveUsage.allocations == 0U);

    endpoint.end();
    std::cout << "[PASS] endpoint job control is immediate, bounded and I/O-free\n";
}

void test_endpoint_job_malformed_frames_fail_before_admission() {
    using namespace core::protocol::filesystem;
    resetTestRoot();
    g_now_ms = 90U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    const auto inner = makeCanonicalLegacyRequest(FileSystemRpcMessageId::MKDIR_REQUEST);
    const auto valid = makeJobRequest(FileSystemJobCommand::START, 89U, 0x09000001U, 0U, 100U,
                                      inner.data(), inner.size());
    core::persistence::ProductPersistenceWorkUsage receiveUsage{};
    {
        auto measuredResult = service.measurePersistenceWork(receiveUsage);
        assert(measuredResult);
        auto measured = std::move(measuredResult.value());

        auto invalidFlags = valid;
        invalidFlags[18U] = 1U;
        transport.emit(invalidFlags.data(), invalidFlags.size());
        auto response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(response);
        assert(response.value().state == FileSystemJobState::REJECTED);
        assert(response.value().error == FileSystemJobError::INVALID_MESSAGE);
        assert(service.persistenceJobs().depth() == 0U);

        auto invalidInner = valid;
        invalidInner[35U] ^= 0x01U;
        transport.emit(invalidInner.data(), invalidInner.size());
        response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(response);
        assert(response.value().state == FileSystemJobState::REJECTED);
        assert(response.value().error == FileSystemJobError::INVALID_MESSAGE);
        assert(service.persistenceJobs().depth() == 0U);

        std::vector<uint8_t> oversizedInner(FILESYSTEM_JOB_RPC_MAX_INNER_REQUEST_BYTES + 1U, 0U);
        std::copy(inner.begin(), inner.end(), oversizedInner.begin());
        const auto oversized = makeJobRequest(FileSystemJobCommand::START, 90U, 0x09000002U, 0U,
                                              100U, oversizedInner.data(), oversizedInner.size());
        transport.emit(oversized.data(), oversized.size());
        response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(response);
        assert(response.value().state == FileSystemJobState::REJECTED);
        assert(response.value().error == FileSystemJobError::RESOURCE_EXHAUSTED);
        assert(service.persistenceJobs().depth() == 0U);

        const uint32_t sentBeforeUnaddressable = transport.sendCount;
        auto zeroNonce = valid;
        std::fill(zeroNonce.begin() + 21U, zeroNonce.begin() + 25U, 0U);
        transport.emit(zeroNonce.data(), zeroNonce.size());
        assert(transport.sendCount == sentBeforeUnaddressable);
        auto badEnvelope = valid;
        badEnvelope[2U] ^= 0x01U;
        transport.emit(badEnvelope.data(), badEnvelope.size());
        assert(transport.sendCount == sentBeforeUnaddressable);
    }
    assert(receiveUsage.filesystemCalls == 0U);
    assert(receiveUsage.bytes == 0U);
    assert(receiveUsage.allocations == 0U);

    endpoint.end();
    std::cout << "[PASS] malformed job frames fail before admission or payload copy\n";
}

void test_endpoint_job_completion_is_polled_retained_and_not_unsolicited() {
    using namespace core::protocol::filesystem;
    resetTestRoot();
    g_now_ms = 100U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    std::array<uint8_t, 128> inner{};
    const size_t innerSize = FileSystemRpcCodec::encodeMkdirRequest(90U, "projects/polled-job",
                                                                    inner.data(), inner.size());
    assert(innerSize > 0U);
    const auto start = makeJobRequest(FileSystemJobCommand::START, 90U, 0x20000001U, 0U, 1'000U,
                                      inner.data(), innerSize);
    transport.emit(start.data(), start.size());
    assert(transport.sendCount == 1U);
    auto response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t jobId = response.value().jobId;

    core::persistence::ProductPersistenceWorkUsage playbackUsage{};
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    {
        auto measuredResult = service.measurePersistenceWork(playbackUsage);
        assert(measuredResult);
        auto measured = std::move(measuredResult.value());
        endpoint.advance(g_now_ms, true);
    }
    assert(playbackUsage.filesystemCalls == 0U);
    assert(transport.sendCount == 1U);
    assert(!service.stat("projects/polled-job"));
    assert(service.persistenceJobs().depth() == 1U);

    const auto poll = makeJobRequest(FileSystemJobCommand::POLL, 91U, 0x20000001U, jobId, 0U);
    transport.emit(poll.data(), poll.size());
    assert(transport.sendCount == 2U);
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::PENDING);

    ++g_now_ms;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(service.persistenceJobs().depth() == 0U);
    assert(service.stat("projects/polled-job"));
    assert(transport.sendCount == 2U);

    transport.emit(poll.data(), poll.size());
    assert(transport.sendCount == 3U);
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::COMPLETED);
    assert(response.value().progressPerMille == 1'000U);
    assert((response.value().flags & FILESYSTEM_JOB_RPC_FLAG_TERMINAL_RETAINED) != 0U);
    const auto innerResponse =
        FileSystemRpcCodec::decodeStatusResponse(response.value().body, response.value().bodySize);
    assert(innerResponse);
    assert(innerResponse.value().requestId == 90U);
    assert(innerResponse.value().status == FileSystemRpcStatus::OK);

    auto duplicate = start;
    duplicate[15U] = 92U;
    transport.emit(duplicate.data(), duplicate.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::COMPLETED);
    assert(response.value().jobId == jobId);
    assert((response.value().flags & FILESYSTEM_JOB_RPC_FLAG_DUPLICATE_START) != 0U);

    g_now_ms = 101U + FILESYSTEM_JOB_RPC_TERMINAL_RETENTION_MS;
    transport.emit(poll.data(), poll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::COMPLETED);

    ++g_now_ms;
    transport.emit(poll.data(), poll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::REJECTED);
    assert(response.value().error == FileSystemJobError::NOT_FOUND);

    g_now_ms = UINT32_MAX - 5U;
    const size_t rolloverInnerSize = FileSystemRpcCodec::encodeMkdirRequest(
        93U, "projects/rollover-job", inner.data(), inner.size());
    const auto rolloverStart = makeJobRequest(FileSystemJobCommand::START, 93U, 0x20000002U, 0U,
                                              1'000U, inner.data(), rolloverInnerSize);
    transport.emit(rolloverStart.data(), rolloverStart.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t rolloverJobId = response.value().jobId;
    ++g_now_ms;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    const auto rolloverPoll =
        makeJobRequest(FileSystemJobCommand::POLL, 94U, 0x20000002U, rolloverJobId, 0U);
    g_now_ms = 29'995U;
    transport.emit(rolloverPoll.data(), rolloverPoll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::COMPLETED);
    ++g_now_ms;
    transport.emit(rolloverPoll.data(), rolloverPoll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::REJECTED);
    assert(response.value().error == FileSystemJobError::NOT_FOUND);

    endpoint.end();
    std::cout << "[PASS] job completion is poll-only and retained inclusively\n";
}

void test_endpoint_job_safe_cancel_deadline_and_media_are_typed() {
    using namespace core::protocol::filesystem;
    resetTestRoot();
    g_now_ms = 1'000U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    std::array<uint8_t, 128> inner{};
    size_t innerSize = FileSystemRpcCodec::encodeMkdirRequest(100U, "projects/cancelled-job",
                                                              inner.data(), inner.size());
    assert(innerSize > 0U);
    auto start = makeJobRequest(FileSystemJobCommand::START, 100U, 0x30000001U, 0U, 1'000U,
                                inner.data(), innerSize);
    transport.emit(start.data(), start.size());
    auto response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t cancelJobId = response.value().jobId;
    const auto cancel =
        makeJobRequest(FileSystemJobCommand::CANCEL, 101U, 0x30000001U, cancelJobId, 0U);
    transport.emit(cancel.data(), cancel.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::CANCEL_PENDING);
    const uint32_t sentBeforeCancelUnwind = transport.sendCount;
    g_now_ms += 1'000U;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(transport.sendCount == sentBeforeCancelUnwind);
    assert(service.persistenceJobs().depth() == 0U);
    assert(!service.stat("projects/cancelled-job"));
    const auto cancelPoll =
        makeJobRequest(FileSystemJobCommand::POLL, 102U, 0x30000001U, cancelJobId, 0U);
    transport.emit(cancelPoll.data(), cancelPoll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::CANCELLED);
    assert(response.value().error == FileSystemJobError::CANCELLED);

    g_now_ms = 3'000U;
    innerSize = FileSystemRpcCodec::encodeMkdirRequest(103U, "projects/expired-job", inner.data(),
                                                       inner.size());
    start = makeJobRequest(FileSystemJobCommand::START, 103U, 0x30000002U, 0U, 10U, inner.data(),
                           innerSize);
    transport.emit(start.data(), start.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t deadlineJobId = response.value().jobId;
    g_now_ms += 10U;
    const auto lateCancel =
        makeJobRequest(FileSystemJobCommand::CANCEL, 104U, 0x30000002U, deadlineJobId, 0U);
    transport.emit(lateCancel.data(), lateCancel.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::PENDING);
    assert(response.value().error == FileSystemJobError::NONE);
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(!service.stat("projects/expired-job"));
    const auto deadlinePoll =
        makeJobRequest(FileSystemJobCommand::POLL, 104U, 0x30000002U, deadlineJobId, 0U);
    transport.emit(deadlinePoll.data(), deadlinePoll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::FAILED);
    assert(response.value().error == FileSystemJobError::DEADLINE_EXCEEDED);

    g_now_ms = 4'000U;
    innerSize = FileSystemRpcCodec::encodeMkdirRequest(105U, "projects/media-job", inner.data(),
                                                       inner.size());
    start = makeJobRequest(FileSystemJobCommand::START, 105U, 0x30000003U, 0U, 1'000U, inner.data(),
                           innerSize);
    transport.emit(start.data(), start.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t mediaJobId = response.value().jobId;
    service.markMediaUnavailable();
    endpoint.advance(g_now_ms, true);
    const auto mediaPoll =
        makeJobRequest(FileSystemJobCommand::POLL, 106U, 0x30000003U, mediaJobId, 0U);
    transport.emit(mediaPoll.data(), mediaPoll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::FAILED);
    assert(response.value().error == FileSystemJobError::MEDIA_CHANGED);

    endpoint.end();
    std::cout << "[PASS] safe cancel, deadline and media failures are typed\n";
}

void test_endpoint_job_terminal_cache_has_exact_bounded_capacity() {
    using namespace core::protocol::filesystem;
    resetTestRoot();
    g_now_ms = 4'000U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    std::array<uint8_t, 128> inner{};
    char path[48] = {};
    for (uint8_t index = 0U; index < 32U; ++index) {
        const int pathLength =
            std::snprintf(path, sizeof(path), "projects/cache-%02u", static_cast<unsigned>(index));
        assert(pathLength > 0 && static_cast<size_t>(pathLength) < sizeof(path));
        const size_t innerSize = FileSystemRpcCodec::encodeMkdirRequest(
            static_cast<uint16_t>(120U + index), path, inner.data(), inner.size());
        assert(innerSize > 0U);
        const auto start =
            makeJobRequest(FileSystemJobCommand::START, static_cast<uint16_t>(120U + index),
                           0x40000001U + index, 0U, 1'000U, inner.data(), innerSize);
        transport.emit(start.data(), start.size());
        const auto accepted =
            FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
        assert(accepted);
        assert(accepted.value().state == FileSystemJobState::ACCEPTED);
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        assert(service.persistenceJobs().depth() == 0U);
        ++g_now_ms;
    }

    const size_t overflowInnerSize = FileSystemRpcCodec::encodeMkdirRequest(
        152U, "projects/cache-overflow", inner.data(), inner.size());
    const auto overflow = makeJobRequest(FileSystemJobCommand::START, 152U, 0x40000021U, 0U, 1'000U,
                                         inner.data(), overflowInnerSize);
    transport.emit(overflow.data(), overflow.size());
    auto response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::REJECTED);
    assert(response.value().error == FileSystemJobError::RESOURCE_EXHAUSTED);
    assert(service.persistenceJobs().depth() == 0U);

    g_now_ms += FILESYSTEM_JOB_RPC_TERMINAL_RETENTION_MS + 1U;
    transport.emit(overflow.data(), overflow.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::ACCEPTED);
    assert(service.persistenceJobs().depth() == 1U);

    endpoint.end();
    std::cout << "[PASS] terminal cache retains exactly 32 records without eviction\n";
}

void test_endpoint_job_write_commit_retains_late_completion_and_identity() {
    using namespace core::protocol::filesystem;
    resetTestRoot();
    g_now_ms = 5'000U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    constexpr uint16_t sessionId = 0x4321U;
    constexpr uint8_t payload[] = {'j', 'o', 'b'};
    std::array<uint8_t, 256> request{};
    size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        160U, sessionId, "projects/job-upload.bin", sizeof(payload), request.data(),
        request.size());
    assert(requestSize > 0U);
    transport.emit(request.data(), requestSize);
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    auto write = FileSystemRpcCodec::decodeWriteResponse(transport.sent, transport.sentSize);
    assert(write && write.value().status == FileSystemRpcStatus::OK);
    assert(service.persistenceJobs().depth() == 1U);
    const uint32_t uploadJobId = service.persistenceJobs().activeJobId();
    assert(uploadJobId != 0U);

    ++g_now_ms;
    requestSize = FileSystemRpcCodec::encodeWriteChunkRequest(
        161U, sessionId, 0U, payload, sizeof(payload), request.data(), request.size());
    transport.emit(request.data(), requestSize);
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    write = FileSystemRpcCodec::decodeWriteResponse(transport.sent, transport.sentSize);
    assert(write && write.value().status == FileSystemRpcStatus::OK);
    assert(service.persistenceJobs().depth() == 1U);
    assert(service.persistenceJobs().activeJobId() == uploadJobId);

    std::array<uint8_t, 96> commitInner{};
    const size_t commitInnerSize = FileSystemRpcCodec::encodeWriteCommitRequest(
        162U, sessionId, commitInner.data(), commitInner.size());
    const uint32_t commitAdmittedAtMs = g_now_ms;
    const auto start = makeJobRequest(FileSystemJobCommand::START, 162U, 0x50000001U, 0U, 1'000U,
                                      commitInner.data(), commitInnerSize);
    transport.emit(start.data(), start.size());
    auto response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    assert(response.value().jobId == uploadJobId);
    assert(service.persistenceJobs().depth() == 1U);

    const auto secondOwner = makeJobRequest(FileSystemJobCommand::START, 163U, 0x50000002U, 0U,
                                            1'000U, commitInner.data(), commitInnerSize);
    transport.emit(secondOwner.data(), secondOwner.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::REJECTED);
    assert(response.value().error == FileSystemJobError::CONFLICT);
    assert(response.value().jobId == uploadJobId);
    assert(service.persistenceJobs().depth() == 1U);

    bool lateCancelObserved = false;
    uint32_t sentAfterLateCancel = transport.sendCount;
    for (uint8_t advances = 0U; advances < 32U && service.persistenceJobs().depth() != 0U;
         ++advances) {
        ++g_now_ms;
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        const bool journalPresent = std::filesystem::exists(testRoot() / "midi-studio" / "tmp" /
                                                            "rpc-product-file-a.journal") ||
                                    std::filesystem::exists(testRoot() / "midi-studio" / "tmp" /
                                                            "rpc-product-file-b.journal");
        if (!lateCancelObserved && journalPresent && service.persistenceJobs().depth() != 0U) {
            const auto cancel =
                makeJobRequest(FileSystemJobCommand::CANCEL, 163U, 0x50000001U, uploadJobId, 0U);
            transport.emit(cancel.data(), cancel.size());
            response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
            assert(response);
            assert(response.value().state == FileSystemJobState::PENDING);
            assert((response.value().flags & FILESYSTEM_JOB_RPC_FLAG_CANCEL_TOO_LATE) != 0U);
            lateCancelObserved = true;
            sentAfterLateCancel = transport.sendCount;
            // Cross the deadline only after the journal made the commit
            // irreversible; the eventual canonical response must still win.
            g_now_ms = commitAdmittedAtMs + 1'000U;
        }
    }
    assert(lateCancelObserved);
    assert(service.persistenceJobs().depth() == 0U);
    assert(transport.sendCount == sentAfterLateCancel);
    assertProductFileEquals(service, "projects/job-upload.bin", payload, sizeof(payload));

    const auto poll =
        makeJobRequest(FileSystemJobCommand::POLL, 164U, 0x50000001U, uploadJobId, 0U);
    transport.emit(poll.data(), poll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::COMPLETED);
    assert(response.value().error == FileSystemJobError::NONE);
    write =
        FileSystemRpcCodec::decodeWriteResponse(response.value().body, response.value().bodySize);
    assert(write);
    assert(write.value().requestId == 162U);
    assert(write.value().status == FileSystemRpcStatus::OK);
    assert(write.value().sessionId == sessionId);

    const auto terminalCancel =
        makeJobRequest(FileSystemJobCommand::CANCEL, 165U, 0x50000001U, uploadJobId, 0U);
    transport.emit(terminalCancel.data(), terminalCancel.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::COMPLETED);
    assert((response.value().flags & FILESYSTEM_JOB_RPC_FLAG_CANCEL_TOO_LATE) != 0U);

    endpoint.end();
    std::cout << "[PASS] job commit retains late durable completion and upload identity\n";
}

void test_endpoint_job_conditional_journal_retains_late_completion() {
    using namespace core::protocol::filesystem;
    resetTestRoot();
    g_now_ms = 5'500U;

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    assert(core::test::writeProductFileFixture(service, "library/step-presets/job-current.mssp", 0U,
                                               reinterpret_cast<const uint8_t*>("old"), 3U));
    assert(core::test::writeProductFileFixture(service, "tmp/job-staging.mssp", 0U,
                                               reinterpret_cast<const uint8_t*>("new"), 3U));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();
    std::array<uint8_t, 256> inner{};
    const size_t innerSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        166U, 0x434F4E44U, "library/step-presets/job-current.mssp", "tmp/job-staging.mssp",
        SHA256_OLD, SHA256_NEW, inner.data(), inner.size());
    assert(innerSize > 0U);
    const auto start = makeJobRequest(FileSystemJobCommand::START, 166U, 0x55000001U, 0U, 1'000U,
                                      inner.data(), innerSize);
    transport.emit(start.data(), start.size());
    auto response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t jobId = response.value().jobId;

    bool journalDurable = false;
    for (uint8_t advances = 0U; advances < 32U && !journalDurable; ++advances) {
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        journalDurable = static_cast<bool>(service.stat("tmp/rpc-conditional.journal"));
        ++g_now_ms;
    }
    assert(journalDurable);
    assert(service.persistenceJobs().depth() == 1U);

    const auto cancel = makeJobRequest(FileSystemJobCommand::CANCEL, 167U, 0x55000001U, jobId, 0U);
    transport.emit(cancel.data(), cancel.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::PENDING);
    assert((response.value().flags & FILESYSTEM_JOB_RPC_FLAG_CANCEL_TOO_LATE) != 0U);
    const uint32_t sentAfterCancel = transport.sendCount;

    for (uint8_t advances = 0U; advances < 64U && service.persistenceJobs().depth() != 0U;
         ++advances) {
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        ++g_now_ms;
    }
    assert(service.persistenceJobs().depth() == 0U);
    assert(transport.sendCount == sentAfterCancel);
    assertProductFileEquals(service, "library/step-presets/job-current.mssp",
                            reinterpret_cast<const uint8_t*>("new"), 3U);

    const auto poll = makeJobRequest(FileSystemJobCommand::POLL, 168U, 0x55000001U, jobId, 0U);
    transport.emit(poll.data(), poll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::COMPLETED);
    const auto mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        response.value().body, response.value().bodySize);
    assert(mutation);
    assert(mutation.value().status == FileSystemRpcStatus::OK);
    assert(mutation.value().outcome == FileSystemRpcMutationOutcome::APPLIED);

    assert(core::test::writeProductFileFixture(service,
                                               "library/step-presets/job-deadline-current.mssp", 0U,
                                               reinterpret_cast<const uint8_t*>("old"), 3U));
    assert(core::test::writeProductFileFixture(service, "tmp/job-deadline-stage.mssp", 0U,
                                               reinterpret_cast<const uint8_t*>("new"), 3U));
    const uint32_t deadlineAdmittedAt = g_now_ms;
    const size_t deadlineInnerSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        169U, 0x44454144U, "library/step-presets/job-deadline-current.mssp",
        "tmp/job-deadline-stage.mssp", SHA256_OLD, SHA256_NEW, inner.data(), inner.size());
    const auto deadlineStart = makeJobRequest(FileSystemJobCommand::START, 169U, 0x55000002U, 0U,
                                              100U, inner.data(), deadlineInnerSize);
    transport.emit(deadlineStart.data(), deadlineStart.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t deadlineJobId = response.value().jobId;
    journalDurable = false;
    for (uint8_t advances = 0U; advances < 32U && !journalDurable; ++advances) {
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        journalDurable = static_cast<bool>(service.stat("tmp/rpc-conditional.journal"));
        ++g_now_ms;
    }
    assert(journalDurable);
    g_now_ms = deadlineAdmittedAt + 100U;
    for (uint8_t advances = 0U; advances < 64U && service.persistenceJobs().depth() != 0U;
         ++advances) {
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        ++g_now_ms;
    }
    assert(service.persistenceJobs().depth() == 0U);
    assertProductFileEquals(service, "library/step-presets/job-deadline-current.mssp",
                            reinterpret_cast<const uint8_t*>("new"), 3U);
    const auto deadlinePoll =
        makeJobRequest(FileSystemJobCommand::POLL, 170U, 0x55000002U, deadlineJobId, 0U);
    transport.emit(deadlinePoll.data(), deadlinePoll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::COMPLETED);
    assert(response.value().error == FileSystemJobError::NONE);
    const auto deadlineMutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        response.value().body, response.value().bodySize);
    assert(deadlineMutation);
    assert(deadlineMutation.value().status == FileSystemRpcStatus::OK);
    assert(deadlineMutation.value().outcome == FileSystemRpcMutationOutcome::APPLIED);

    endpoint.end();
    std::cout << "[PASS] conditional journal retains late durable completion\n";
}

void test_endpoint_job_rename_and_conditional_delete_complete() {
    using namespace core::protocol::filesystem;
    resetTestRoot();
    g_now_ms = 5'800U;

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    const uint8_t oldBytes[] = {'o', 'l', 'd'};
    assert(core::test::writeProductFileFixture(service, "projects/job-rename-source.bin", 0U,
                                               oldBytes, sizeof(oldBytes)));
    assert(core::test::writeProductFileFixture(service, "library/step-presets/job-delete.mssp", 0U,
                                               oldBytes, sizeof(oldBytes)));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();
    std::array<uint8_t, 256> inner{};
    size_t innerSize = FileSystemRpcCodec::encodeRenameRequest(
        169U, "projects/job-rename-source.bin", "projects/job-rename-target.bin", inner.data(),
        inner.size());
    auto start = makeJobRequest(FileSystemJobCommand::START, 169U, 0x58000001U, 0U, 1'000U,
                                inner.data(), innerSize);
    transport.emit(start.data(), start.size());
    auto response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t renameJobId = response.value().jobId;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(service.persistenceJobs().depth() == 0U);
    assert(!service.stat("projects/job-rename-source.bin"));
    assertProductFileEquals(service, "projects/job-rename-target.bin", oldBytes, sizeof(oldBytes));
    const auto renamePoll =
        makeJobRequest(FileSystemJobCommand::POLL, 170U, 0x58000001U, renameJobId, 0U);
    transport.emit(renamePoll.data(), renamePoll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::COMPLETED);
    auto status =
        FileSystemRpcCodec::decodeStatusResponse(response.value().body, response.value().bodySize);
    assert(status && status.value().status == FileSystemRpcStatus::OK);

    ++g_now_ms;
    innerSize = FileSystemRpcCodec::encodeConditionalDeleteRequest(
        171U, 0x44454C45U, "library/step-presets/job-delete.mssp", SHA256_OLD, inner.data(),
        inner.size());
    start = makeJobRequest(FileSystemJobCommand::START, 171U, 0x58000002U, 0U, 1'000U, inner.data(),
                           innerSize);
    transport.emit(start.data(), start.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t deleteJobId = response.value().jobId;
    for (uint8_t advances = 0U; advances < 64U && service.persistenceJobs().depth() != 0U;
         ++advances) {
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        ++g_now_ms;
    }
    assert(service.persistenceJobs().depth() == 0U);
    assert(!service.stat("library/step-presets/job-delete.mssp"));
    const auto deletePoll =
        makeJobRequest(FileSystemJobCommand::POLL, 172U, 0x58000002U, deleteJobId, 0U);
    transport.emit(deletePoll.data(), deletePoll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::COMPLETED);
    const auto mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        response.value().body, response.value().bodySize);
    assert(mutation);
    assert(mutation.value().status == FileSystemRpcStatus::OK);
    assert(mutation.value().outcome == FileSystemRpcMutationOutcome::APPLIED);

    innerSize = FileSystemRpcCodec::encodeRenameRequest(173U, "projects/missing-source.bin",
                                                        "projects/unused-target.bin", inner.data(),
                                                        inner.size());
    start = makeJobRequest(FileSystemJobCommand::START, 173U, 0x58000003U, 0U, 1'000U, inner.data(),
                           innerSize);
    transport.emit(start.data(), start.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t failedLegacyJobId = response.value().jobId;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    const auto failedLegacyPoll =
        makeJobRequest(FileSystemJobCommand::POLL, 174U, 0x58000003U, failedLegacyJobId, 0U);
    transport.emit(failedLegacyPoll.data(), failedLegacyPoll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::COMPLETED);
    assert(response.value().error == FileSystemJobError::NONE);
    status =
        FileSystemRpcCodec::decodeStatusResponse(response.value().body, response.value().bodySize);
    assert(status && status.value().status == FileSystemRpcStatus::NOT_FOUND);

    endpoint.end();
    std::cout << "[PASS] job rename and conditional delete complete exactly\n";
}

void test_endpoint_job_safe_cancel_unwinds_reversible_conditional_plan() {
    using namespace core::protocol::filesystem;
    resetTestRoot();
    g_now_ms = 5'900U;

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    assert(core::test::writeProductFileFixture(service,
                                               "library/step-presets/job-safe-current.mssp", 0U,
                                               reinterpret_cast<const uint8_t*>("old"), 3U));
    assert(core::test::writeProductFileFixture(service, "tmp/job-safe-stage.mssp", 0U,
                                               reinterpret_cast<const uint8_t*>("new"), 3U));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();
    std::array<uint8_t, 256> inner{};
    const size_t innerSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        173U, 0x53414645U, "library/step-presets/job-safe-current.mssp", "tmp/job-safe-stage.mssp",
        SHA256_OLD, SHA256_NEW, inner.data(), inner.size());
    const auto start = makeJobRequest(FileSystemJobCommand::START, 173U, 0x59000001U, 0U, 1'000U,
                                      inner.data(), innerSize);
    transport.emit(start.data(), start.size());
    auto response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t jobId = response.value().jobId;

    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(service.persistenceJobs().depth() == 1U);
    assert(!service.stat("tmp/rpc-conditional.journal"));

    const auto cancel = makeJobRequest(FileSystemJobCommand::CANCEL, 174U, 0x59000001U, jobId, 0U);
    transport.emit(cancel.data(), cancel.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::CANCEL_PENDING);

    ++g_now_ms;
    core::persistence::ProductPersistenceWorkUsage playbackUsage{};
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    {
        auto measuredResult = service.measurePersistenceWork(playbackUsage);
        assert(measuredResult);
        auto measured = std::move(measuredResult.value());
        endpoint.advance(g_now_ms, true);
    }
    assert(playbackUsage.filesystemCalls == 0U);
    assert(service.persistenceJobs().depth() == 1U);

    ++g_now_ms;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(service.persistenceJobs().depth() == 0U);
    assertProductFileEquals(service, "library/step-presets/job-safe-current.mssp",
                            reinterpret_cast<const uint8_t*>("old"), 3U);
    assertProductFileEquals(service, "tmp/job-safe-stage.mssp",
                            reinterpret_cast<const uint8_t*>("new"), 3U);
    assert(!service.stat("tmp/rpc-conditional.journal"));

    const auto poll = makeJobRequest(FileSystemJobCommand::POLL, 175U, 0x59000001U, jobId, 0U);
    transport.emit(poll.data(), poll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::CANCELLED);
    assert(response.value().error == FileSystemJobError::CANCELLED);

    assert(core::test::writeProductFileFixture(service,
                                               "library/step-presets/job-media-current.mssp", 0U,
                                               reinterpret_cast<const uint8_t*>("old"), 3U));
    assert(core::test::writeProductFileFixture(service, "tmp/job-media-stage.mssp", 0U,
                                               reinterpret_cast<const uint8_t*>("new"), 3U));
    ++g_now_ms;
    const size_t mediaInnerSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        176U, 0x4D454449U, "library/step-presets/job-media-current.mssp",
        "tmp/job-media-stage.mssp", SHA256_OLD, SHA256_NEW, inner.data(), inner.size());
    const auto mediaStart = makeJobRequest(FileSystemJobCommand::START, 176U, 0x59000002U, 0U,
                                           1'000U, inner.data(), mediaInnerSize);
    transport.emit(mediaStart.data(), mediaStart.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t mediaJobId = response.value().jobId;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(service.persistenceJobs().depth() == 1U);

    service.markMediaUnavailable();
    filesystem.resetWorkCounters();
    endpoint.advance(g_now_ms, true);
    assert(filesystem.filesystemCalls == 0U);
    assert(filesystem.ioBytes == 0U);
    const auto mediaPoll =
        makeJobRequest(FileSystemJobCommand::POLL, 177U, 0x59000002U, mediaJobId, 0U);
    transport.emit(mediaPoll.data(), mediaPoll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::FAILED);
    assert(response.value().error == FileSystemJobError::MEDIA_CHANGED);

    endpoint.end();
    std::cout << "[PASS] reversible plan cancel/media unwind stays playback-safe\n";
}

void test_endpoint_job_recursive_delete_cancel_too_late_continues() {
    using namespace core::protocol::filesystem;
    resetTestRoot();
    g_now_ms = 6'000U;

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    auto leaseResult = service.acquireMutation(core::persistence::ProductMutationOwner::PROJECT);
    assert(leaseResult);
    auto lease = std::move(leaseResult.value());
    assert(service.createDirectory(lease, "projects/job-delete/a"));
    const uint8_t payload[] = {'x'};
    assert(service.write(lease, "projects/job-delete/a/value.bin", 0U, payload, sizeof(payload)));
    assert(service.releaseMutation(lease));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();
    std::array<uint8_t, 256> inner{};
    const size_t innerSize = FileSystemRpcCodec::encodeDeleteRequest(
        170U, "projects/job-delete", true, inner.data(), inner.size());
    const auto start = makeJobRequest(FileSystemJobCommand::START, 170U, 0x60000001U, 0U, 1'000U,
                                      inner.data(), innerSize);
    transport.emit(start.data(), start.size());
    auto response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t jobId = response.value().jobId;

    for (uint8_t advance = 0U; advance < 3U; ++advance) {
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        ++g_now_ms;
    }
    assert(!std::filesystem::exists(testRoot() / "midi-studio" / "projects" / "job-delete"));
    assert(std::filesystem::exists(testRoot() / "midi-studio" / "tmp" / "rpc-d"));

    const auto cancel = makeJobRequest(FileSystemJobCommand::CANCEL, 171U, 0x60000001U, jobId, 0U);
    transport.emit(cancel.data(), cancel.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::PENDING);
    assert((response.value().flags & FILESYSTEM_JOB_RPC_FLAG_CANCEL_TOO_LATE) != 0U);
    const uint32_t sentAfterCancel = transport.sendCount;

    for (uint8_t advances = 0U; advances < 32U && service.persistenceJobs().depth() != 0U;
         ++advances) {
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        ++g_now_ms;
    }
    assert(service.persistenceJobs().depth() == 0U);
    assert(transport.sendCount == sentAfterCancel);
    assert(!std::filesystem::exists(testRoot() / "midi-studio" / "tmp" / "rpc-d"));

    const auto poll = makeJobRequest(FileSystemJobCommand::POLL, 172U, 0x60000001U, jobId, 0U);
    transport.emit(poll.data(), poll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::COMPLETED);
    const auto deleted =
        FileSystemRpcCodec::decodeStatusResponse(response.value().body, response.value().bodySize);
    assert(deleted && deleted.value().status == FileSystemRpcStatus::OK);

    transport.emit(cancel.data(), cancel.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::COMPLETED);
    assert((response.value().flags & FILESYSTEM_JOB_RPC_FLAG_CANCEL_TOO_LATE) != 0U);

    endpoint.end();
    std::cout << "[PASS] hidden recursive delete rejects late cancel and continues\n";
}

void test_endpoint_job_deadline_after_hide_retains_completion() {
    using namespace core::protocol::filesystem;
    resetTestRoot();
    constexpr uint32_t admittedAtMs = 7'000U;
    g_now_ms = admittedAtMs;

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    auto leaseResult = service.acquireMutation(core::persistence::ProductMutationOwner::PROJECT);
    assert(leaseResult);
    auto lease = std::move(leaseResult.value());
    assert(service.createDirectory(lease, "projects/job-deadline/a"));
    const uint8_t payload[] = {'d'};
    assert(service.write(lease, "projects/job-deadline/a/value.bin", 0U, payload, sizeof(payload)));
    assert(service.releaseMutation(lease));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();
    std::array<uint8_t, 256> inner{};
    const size_t innerSize = FileSystemRpcCodec::encodeDeleteRequest(
        180U, "projects/job-deadline", true, inner.data(), inner.size());
    const auto start = makeJobRequest(FileSystemJobCommand::START, 180U, 0x70000001U, 0U, 100U,
                                      inner.data(), innerSize);
    transport.emit(start.data(), start.size());
    auto response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response && response.value().state == FileSystemJobState::ACCEPTED);
    const uint32_t jobId = response.value().jobId;

    for (uint8_t advance = 0U; advance < 3U; ++advance) {
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        ++g_now_ms;
    }
    assert(!std::filesystem::exists(testRoot() / "midi-studio" / "projects" / "job-deadline"));
    assert(std::filesystem::exists(testRoot() / "midi-studio" / "tmp" / "rpc-d"));

    g_now_ms = admittedAtMs + 100U;
    for (uint8_t advances = 0U; advances < 32U && service.persistenceJobs().depth() != 0U;
         ++advances) {
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        ++g_now_ms;
    }
    assert(service.persistenceJobs().depth() == 0U);
    assert(!std::filesystem::exists(testRoot() / "midi-studio" / "tmp" / "rpc-d"));
    assert(service.storageState() == core::persistence::ProductStorageState::READY);

    const auto poll = makeJobRequest(FileSystemJobCommand::POLL, 181U, 0x70000001U, jobId, 0U);
    transport.emit(poll.data(), poll.size());
    response = FileSystemJobRpcCodec::decodeResponse(transport.sent, transport.sentSize);
    assert(response);
    assert(response.value().state == FileSystemJobState::COMPLETED);
    assert(response.value().error == FileSystemJobError::NONE);
    const auto deleted =
        FileSystemRpcCodec::decodeStatusResponse(response.value().body, response.value().bodySize);
    assert(deleted && deleted.value().status == FileSystemRpcStatus::OK);

    endpoint.end();
    std::cout << "[PASS] deadline after hide retains completed cleanup result\n";
}

void test_stat_and_read_roundtrip() {
    resetTestRoot();
    Harness h;

    const uint8_t payload[] = {'p', 'r', 'o', 'j', 'e', 'c', 't'};
    auto written = core::test::writeProductFileFixture(
        h.service, "projects/demo.bin", 0, payload, sizeof(payload)
    );
    assert(written);

    size_t requestSize = FileSystemRpcCodec::encodeStatRequest(
        7,
        "projects/demo.bin",
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    size_t responseSize = h.transact(requestSize);
    auto stat = FileSystemRpcCodec::decodeStatResponse(h.response, responseSize);
    assert(stat);
    assert(stat.value().requestId == 7);
    assert(stat.value().status == FileSystemRpcStatus::OK);
    assert(stat.value().type == FileSystemRpcFileType::FILE);
    assert(stat.value().sizeBytes == sizeof(payload));

    requestSize = FileSystemRpcCodec::encodeReadRequest(
        8,
        "projects/demo.bin",
        1,
        4,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    responseSize = h.transact(requestSize);
    auto read = FileSystemRpcCodec::decodeReadResponse(h.response, responseSize);
    assert(read);
    assert(read.value().requestId == 8);
    assert(read.value().status == FileSystemRpcStatus::OK);
    assert(read.value().offset == 1);
    assert(read.value().bytesRead == 4);
    assert(std::memcmp(read.value().data, "roje", 4) == 0);

    std::cout << "[PASS] test_stat_and_read_roundtrip\n";
}

void test_capabilities_roundtrip() {
    resetTestRoot();
    Harness h;

    const size_t requestSize = FileSystemRpcCodec::encodeCapabilitiesRequest(
        9,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    const size_t responseSize = h.transact(requestSize);
    auto caps = FileSystemRpcCodec::decodeCapabilitiesResponse(h.response, responseSize);
    assert(caps);
    assert(caps.value().requestId == 9);
    assert(caps.value().status == FileSystemRpcStatus::OK);
    assert(caps.value().rpcSchema == FILESYSTEM_RPC_SCHEMA);
    assert(caps.value().maxChunkSize == FILESYSTEM_RPC_MAX_CHUNK_SIZE);
    assert(caps.value().responseBufferSize == FILESYSTEM_RPC_RESPONSE_BUFFER_SIZE);
    assert(caps.value().maxListEntries == FILESYSTEM_RPC_MAX_LIST_ENTRIES);
    assert(caps.value().maxPathLength == oc::interface::FILESYSTEM_MAX_PATH_LENGTH);
    assert((caps.value().featureFlags & FILESYSTEM_RPC_FEATURE_CAPABILITIES) != 0);
    assert((caps.value().featureFlags & FILESYSTEM_RPC_FEATURE_WRITE_SESSIONS) != 0);
    assert((caps.value().featureFlags & FILESYSTEM_RPC_FEATURE_FILE_MANAGEMENT) != 0);
    assert((caps.value().featureFlags & FILESYSTEM_RPC_FEATURE_CONDITIONAL_MUTATIONS) != 0);
    assert((caps.value().featureFlags & FILESYSTEM_RPC_FEATURE_PERSISTENCE_JOBS) != 0);

    std::cout << "[PASS] test_capabilities_roundtrip\n";
}

void test_list_is_paginated_and_bounded() {
    resetTestRoot();
    Harness h;

    const uint8_t byte = 1;
    assert(core::test::writeProductFileFixture(h.service, "projects/a.bin", 0, &byte, 1));
    assert(core::test::writeProductFileFixture(h.service, "projects/b.bin", 0, &byte, 1));
    assert(core::test::writeProductFileFixture(h.service, "projects/c.bin", 0, &byte, 1));

    const size_t requestSize = FileSystemRpcCodec::encodeListRequest(
        11,
        "projects",
        0,
        2,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    const size_t responseSize = h.transact(requestSize);
    auto list = FileSystemRpcCodec::decodeListResponse(h.response, responseSize);
    assert(list);
    assert(list.value().requestId == 11);
    assert(list.value().status == FileSystemRpcStatus::OK);
    assert(list.value().startIndex == 0);
    assert(list.value().entryCount == 2);
    assert(list.value().hasMore);

    const bool hasA = listContains(list.value(), "a.bin");
    const bool hasB = listContains(list.value(), "b.bin");
    const bool hasC = listContains(list.value(), "c.bin");
    assert((hasA ? 1 : 0) + (hasB ? 1 : 0) + (hasC ? 1 : 0) == 2);

    std::cout << "[PASS] test_list_is_paginated_and_bounded\n";
}

void test_write_session_commits_atomically() {
    resetTestRoot();
    Harness h;

    size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        21,
        0x1234,
        "projects/rpc.bin",
        11,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    size_t responseSize = h.transact(requestSize, 10);
    auto write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);
    assert(h.handler.hasActiveWriteSession());

    requestSize = FileSystemRpcCodec::encodeWriteChunkRequest(
        22,
        0x1234,
        0,
        reinterpret_cast<const uint8_t*>("hello "),
        6,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize, 20);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);
    assert(write.value().bytesWritten == 6);

    requestSize = FileSystemRpcCodec::encodeWriteChunkRequest(
        23,
        0x1234,
        6,
        reinterpret_cast<const uint8_t*>("world"),
        5,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize, 30);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);
    assert(write.value().bytesWritten == 5);

    requestSize = FileSystemRpcCodec::encodeWriteCommitRequest(
        24,
        0x1234,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize, 40);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);
    assert(!h.handler.hasActiveWriteSession());

    uint8_t buffer[16] = {};
    auto read = h.service.read("projects/rpc.bin", 0, buffer, sizeof(buffer));
    assert(read);
    assert(read.value() == 11);
    assert(std::memcmp(buffer, "hello world", 11) == 0);

    requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        25,
        0x1235,
        "projects/rpc.bin",
        3,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize, 50);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);

    requestSize = FileSystemRpcCodec::encodeWriteChunkRequest(
        26,
        0x1235,
        0,
        reinterpret_cast<const uint8_t*>("new"),
        3,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize, 60);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);

    requestSize = FileSystemRpcCodec::encodeWriteCommitRequest(
        27,
        0x1235,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize, 70);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);

    std::memset(buffer, 0, sizeof(buffer));
    read = h.service.read("projects/rpc.bin", 0, buffer, sizeof(buffer));
    assert(read);
    assert(read.value() == 3);
    assert(std::memcmp(buffer, "new", 3) == 0);

    std::cout << "[PASS] test_write_session_commits_atomically\n";
}

void test_write_session_commits_empty_file() {
    resetTestRoot();
    Harness h;

    size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        28,
        0x1236,
        "projects/empty.bin",
        0,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    size_t responseSize = h.transact(requestSize, 10);
    auto write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);
    assert(h.handler.hasActiveWriteSession());

    requestSize = FileSystemRpcCodec::encodeWriteCommitRequest(
        29,
        0x1236,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize, 20);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);
    assert(!h.handler.hasActiveWriteSession());

    auto info = h.service.stat("projects/empty.bin");
    assert(info);
    assert(info.value().type == oc::interface::FileType::FILE);
    assert(info.value().sizeBytes == 0);

    std::cout << "[PASS] test_write_session_commits_empty_file\n";
}

void test_write_session_aborts_on_short_append() {
    resetTestRoot();

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    filesystem.shortAppend = true;
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FileSystemRpcHandler handler(service, catalog, FileSystemRpcHandler::Config{100});
    uint8_t request[1024] = {};
    uint8_t response[1024] = {};

    size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        30,
        0x1237,
        "projects/short.bin",
        4,
        request,
        sizeof(request)
    );
    assert(requestSize > 0);
    auto handled = handler.handleFrame(request, requestSize, 10, response, sizeof(response));
    assert(handled);
    auto write = FileSystemRpcCodec::decodeWriteResponse(response, handled.value());
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);
    assert(handler.hasActiveWriteSession());

    requestSize = FileSystemRpcCodec::encodeWriteChunkRequest(
        31,
        0x1237,
        0,
        reinterpret_cast<const uint8_t*>("drop"),
        4,
        request,
        sizeof(request)
    );
    handled = handler.handleFrame(request, requestSize, 20, response, sizeof(response));
    assert(handled);
    write = FileSystemRpcCodec::decodeWriteResponse(response, handled.value());
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::STORAGE_ERROR);
    assert(!handler.hasActiveWriteSession());
    assert(!service.stat("projects/short.bin"));
    assert(!service.stat("tmp/rpc-write-1237.tmp"));

    std::cout << "[PASS] test_write_session_aborts_on_short_append\n";
}

void test_write_commit_rejects_same_size_backend_corruption_before_mapping() {
    resetTestRoot();

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FileSystemRpcHandler handler(service, catalog, FileSystemRpcHandler::Config{100});
    const uint8_t previous[] = {'s', 'a', 'f', 'e'};
    const uint8_t replacement[] = {'n', 'e', 'w', '!'};
    assert(core::test::writeProductFileFixture(
        service,
        "projects/integrity.bin",
        0U,
        previous,
        sizeof(previous)
    ));

    filesystem.corruptUploadAppend = true;
    assert(writeFileViaRpc(
               handler,
               0x1238,
               "projects/integrity.bin",
               replacement,
               sizeof(replacement)
           ) == FileSystemRpcStatus::STORAGE_ERROR);
    assert(!handler.hasActiveWriteSession());
    assert(service.storageState() ==
           core::persistence::ProductStorageState::READY);
    assertProductFileEquals(
        service,
        "projects/integrity.bin",
        previous,
        sizeof(previous)
    );
    assert(!service.stat("tmp/rpc-write-1238.tmp"));
    assert(!service.stat("tmp/rpc-backup-1238.tmp"));
    assert(!service.stat("tmp/rpc-product-file-a.journal"));
    assert(!service.stat("tmp/rpc-product-file-b.journal"));

    std::cout << "[PASS] same-size RPC backend corruption rejected before mapping\n";
}

void test_write_commit_propagates_final_stat_error() {
    resetTestRoot();

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FileSystemRpcHandler handler(service, catalog, FileSystemRpcHandler::Config{100});
    uint8_t request[1024] = {};
    uint8_t response[1024] = {};

    size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        35,
        0x1240,
        "projects/stat-fail.bin",
        4,
        request,
        sizeof(request)
    );
    assert(requestSize > 0);
    auto handled = handler.handleFrame(request, requestSize, 10, response, sizeof(response));
    assert(handled);
    auto write = FileSystemRpcCodec::decodeWriteResponse(response, handled.value());
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);

    requestSize = FileSystemRpcCodec::encodeWriteChunkRequest(
        36,
        0x1240,
        0,
        reinterpret_cast<const uint8_t*>("data"),
        4,
        request,
        sizeof(request)
    );
    handled = handler.handleFrame(request, requestSize, 20, response, sizeof(response));
    assert(handled);
    write = FileSystemRpcCodec::decodeWriteResponse(response, handled.value());
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);

    filesystem.failFinalStat = true;
    requestSize = FileSystemRpcCodec::encodeWriteCommitRequest(
        37,
        0x1240,
        request,
        sizeof(request)
    );
    handled = handler.handleFrame(request, requestSize, 30, response, sizeof(response));
    assert(handled);
    write = FileSystemRpcCodec::decodeWriteResponse(response, handled.value());
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::STORAGE_ERROR);
    assert(!handler.hasActiveWriteSession());
    assert(!service.stat("projects/stat-fail.bin"));
    assert(!service.stat("tmp/rpc-write-1240.tmp"));

    std::cout << "[PASS] test_write_commit_propagates_final_stat_error\n";
}

void test_write_commit_requires_recovery_when_promotion_fails() {
    resetTestRoot();

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FileSystemRpcHandler handler(service, catalog, FileSystemRpcHandler::Config{100});

    const uint8_t previous[] = {'o', 'l', 'd'};
    const uint8_t replacement[] = {'n', 'e', 'w'};
    assert(core::test::writeProductFileFixture(
        service, "projects/rollback.bin", 0, previous, sizeof(previous)
    ));
    filesystem.failTmpPromotion = true;

    assert(writeFileViaRpc(
               handler,
               0x1250,
               "projects/rollback.bin",
               replacement,
               sizeof(replacement)
           ) == FileSystemRpcStatus::STORAGE_ERROR);
    assert(!handler.hasActiveWriteSession());
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assertProductReadBlocked(service, "projects/rollback.bin");
    assertProductReadBlocked(service, "tmp/rpc-write-1250.tmp");
    assertProductReadBlocked(service, "tmp/rpc-backup-1250.tmp");
    assert(!filesystem.delegate.stat("/midi-studio/projects/rollback.bin"));
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-write-1250.tmp"));
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-backup-1250.tmp"));
    assert(
        filesystem.delegate.stat("/midi-studio/tmp/rpc-product-file-a.journal") ||
        filesystem.delegate.stat("/midi-studio/tmp/rpc-product-file-b.journal")
    );

    filesystem.failTmpPromotion = false;
    completeExternalProductRecovery(service);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::READY);

    uint8_t loaded[8] = {};
    auto read = service.read("projects/rollback.bin", 0, loaded, sizeof(loaded));
    assert(read && read.value() == sizeof(replacement));
    assert(std::memcmp(loaded, replacement, sizeof(replacement)) == 0);
    assert(!service.stat("tmp/rpc-write-1250.tmp"));
    assert(!service.stat("tmp/rpc-backup-1250.tmp"));
    assert(
        filesystem.delegate.stat("/midi-studio/tmp/rpc-product-file-a.journal") ||
        filesystem.delegate.stat("/midi-studio/tmp/rpc-product-file-b.journal")
    );

    std::cout << "[PASS] test_write_commit_requires_recovery_when_promotion_fails\n";
}

void test_write_recovery_retains_backup_when_restore_fails() {
    resetTestRoot();

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FileSystemRpcHandler handler(service, catalog, FileSystemRpcHandler::Config{100});

    const uint8_t previous[] = {'s', 'a', 'f', 'e'};
    const uint8_t replacement[] = {'n', 'e', 'w'};
    assert(core::test::writeProductFileFixture(
        service, "projects/backup-retained.bin", 0, previous, sizeof(previous)
    ));
    filesystem.failTmpPromotion = true;
    filesystem.failBackupRestore = true;

    assert(writeFileViaRpc(
               handler,
               0x1251,
               "projects/backup-retained.bin",
               replacement,
               sizeof(replacement)
           ) == FileSystemRpcStatus::STORAGE_ERROR);
    assert(!handler.hasActiveWriteSession());
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assert(!filesystem.delegate.stat(
        "/midi-studio/projects/backup-retained.bin"
    ));
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-write-1251.tmp"));
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-backup-1251.tmp"));

    assert(filesystem.delegate.remove("/midi-studio/tmp/rpc-write-1251.tmp"));
    filesystem.failTmpPromotion = false;

    auto acquired = service.beginRecovery();
    assert(acquired);
    auto lease = std::move(acquired.value());
    assert(service.ensureLayout(lease));
    auto failedRecovery =
        core::persistence::recoverPendingProductFileTransaction(service, lease);
    assert(!failedRecovery);
    assert(failedRecovery.error().code ==
           oc::type::ErrorCode::STORAGE_WRITE_FAILED);
    assert(service.completeRecovery(
        lease,
        false,
        failedRecovery.error().code
    ));
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-backup-1251.tmp"));

    filesystem.failBackupRestore = false;
    completeExternalProductRecovery(service);

    uint8_t loaded[8] = {};
    auto read = service.read(
        "projects/backup-retained.bin",
        0,
        loaded,
        sizeof(loaded)
    );
    assert(read && read.value() == sizeof(previous));
    assert(std::memcmp(loaded, previous, sizeof(previous)) == 0);
    assert(!service.stat("tmp/rpc-write-1251.tmp"));
    assert(!service.stat("tmp/rpc-backup-1251.tmp"));

    std::cout << "[PASS] test_write_recovery_retains_backup_when_restore_fails\n";
}

void test_write_session_abort_and_timeout_cleanup() {
    resetTestRoot();
    Harness h;

    size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        31,
        0x4321,
        "projects/abort.bin",
        4,
        h.request,
        sizeof(h.request)
    );
    size_t responseSize = h.transact(requestSize, 0);
    auto write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);

    requestSize = FileSystemRpcCodec::encodeWriteChunkRequest(
        32,
        0x4321,
        0,
        reinterpret_cast<const uint8_t*>("drop"),
        4,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize, 10);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);

    requestSize = FileSystemRpcCodec::encodeWriteAbortRequest(
        33,
        0x4321,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize, 20);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);
    assert(!h.service.stat("projects/abort.bin"));
    assert(!h.handler.hasActiveWriteSession());

    requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        34,
        0x9999,
        "projects/timeout.bin",
        4,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize, 1000);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::OK);
    assert(h.handler.hasActiveWriteSession());

    h.handler.update(1201);
    assert(!h.handler.hasActiveWriteSession());

    std::cout << "[PASS] test_write_session_abort_and_timeout_cleanup\n";
}

void test_invalid_path_maps_to_error_status() {
    resetTestRoot();
    Harness h;

    const size_t requestSize = FileSystemRpcCodec::encodeStatRequest(
        41,
        "../escape.bin",
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    const size_t responseSize = h.transact(requestSize);
    auto stat = FileSystemRpcCodec::decodeStatResponse(h.response, responseSize);
    assert(stat);
    assert(stat.value().requestId == 41);
    assert(stat.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);

    std::cout << "[PASS] test_invalid_path_maps_to_error_status\n";
}

void test_read_error_response_is_decodable() {
    resetTestRoot();
    Harness h;

    const size_t requestSize = FileSystemRpcCodec::encodeReadRequest(
        45,
        "projects/missing.bin",
        0,
        16,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    const size_t responseSize = h.transact(requestSize);
    auto read = FileSystemRpcCodec::decodeReadResponse(h.response, responseSize);
    assert(read);
    assert(read.value().requestId == 45);
    assert(read.value().status == FileSystemRpcStatus::NOT_FOUND);
    assert(read.value().bytesRead == 0);
    assert(read.value().data == nullptr);

    std::cout << "[PASS] test_read_error_response_is_decodable\n";
}

void test_file_management_operations() {
    resetTestRoot();
    Harness h;

    size_t requestSize = FileSystemRpcCodec::encodeMkdirRequest(
        47,
        "projects/rpc-folder",
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    size_t responseSize = h.transact(requestSize);
    auto status = FileSystemRpcCodec::decodeStatusResponse(h.response, responseSize);
    assert(status);
    assert(status.value().requestId == 47);
    assert(status.value().messageId == FileSystemRpcMessageId::MKDIR_RESPONSE);
    assert(status.value().status == FileSystemRpcStatus::OK);
    auto folder = h.service.stat("projects/rpc-folder");
    assert(folder);
    assert(folder.value().type == oc::interface::FileType::DIRECTORY);

    const uint8_t payload[] = {'r', 'p', 'c'};
    assert(core::test::writeProductFileFixture(
        h.service, "projects/rpc-folder/source.bin", 0, payload, sizeof(payload)
    ));

    requestSize = FileSystemRpcCodec::encodeRenameRequest(
        48,
        "projects/rpc-folder/source.bin",
        "projects/rpc-folder/renamed.bin",
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    responseSize = h.transact(requestSize);
    status = FileSystemRpcCodec::decodeStatusResponse(h.response, responseSize);
    assert(status);
    assert(status.value().requestId == 48);
    assert(status.value().messageId == FileSystemRpcMessageId::RENAME_RESPONSE);
    assert(status.value().status == FileSystemRpcStatus::OK);
    assert(!h.service.stat("projects/rpc-folder/source.bin"));
    auto renamed = h.service.stat("projects/rpc-folder/renamed.bin");
    assert(renamed);
    assert(renamed.value().type == oc::interface::FileType::FILE);

    requestSize = FileSystemRpcCodec::encodeDeleteRequest(
        49,
        "projects/rpc-folder/renamed.bin",
        false,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    responseSize = h.transact(requestSize);
    status = FileSystemRpcCodec::decodeStatusResponse(h.response, responseSize);
    assert(status);
    assert(status.value().requestId == 49);
    assert(status.value().messageId == FileSystemRpcMessageId::DELETE_RESPONSE);
    assert(status.value().status == FileSystemRpcStatus::OK);
    assert(!h.service.stat("projects/rpc-folder/renamed.bin"));

    requestSize = FileSystemRpcCodec::encodeDeleteRequest(
        50,
        "projects/rpc-folder",
        false,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    responseSize = h.transact(requestSize);
    status = FileSystemRpcCodec::decodeStatusResponse(h.response, responseSize);
    assert(status);
    assert(status.value().requestId == 50);
    assert(status.value().status == FileSystemRpcStatus::OK);
    assert(!h.service.stat("projects/rpc-folder"));

    std::cout << "[PASS] test_file_management_operations\n";
}

void test_storage_gate_maps_busy_exhausted_absent_and_io_failures() {
    using core::protocol::filesystem::internal::mapError;
    using oc::type::Error;
    using oc::type::ErrorCode;

    assert(mapError(Error{ErrorCode::HARDWARE_BUSY, "busy"}) ==
           FileSystemRpcStatus::BUSY);
    assert(mapError(Error{ErrorCode::RESOURCE_EXHAUSTED, "exhausted"}) ==
           FileSystemRpcStatus::TOO_LARGE);
    assert(mapError(Error{ErrorCode::HARDWARE_NOT_FOUND, "absent"}) ==
           FileSystemRpcStatus::STORAGE_ERROR);
    assert(mapError(Error{ErrorCode::HARDWARE_INIT_FAILED, "init"}) ==
           FileSystemRpcStatus::STORAGE_ERROR);
    assert(mapError(Error{ErrorCode::HARDWARE_TIMEOUT, "timeout"}) ==
           FileSystemRpcStatus::STORAGE_ERROR);
    assert(mapError(Error{ErrorCode::STORAGE_WRITE_FAILED, "write"}) ==
           FileSystemRpcStatus::STORAGE_ERROR);

    resetTestRoot();
    Harness busyHarness;
    auto heldResult = busyHarness.service.acquireMutation(
        core::persistence::ProductMutationOwner::ASSET
    );
    assert(heldResult);
    auto held = std::move(heldResult.value());
    size_t requestSize = FileSystemRpcCodec::encodeStatRequest(
        58,
        "projects/busy.bin",
        busyHarness.request,
        sizeof(busyHarness.request)
    );
    size_t responseSize = busyHarness.transact(requestSize, 0);
    auto blocked = FileSystemRpcCodec::decodeStatusResponse(
        busyHarness.response,
        responseSize
    );
    assert(blocked && blocked.value().status == FileSystemRpcStatus::BUSY);
    assert(busyHarness.service.releaseMutation(held));

    resetTestRoot();
    Harness absentHarness;
    absentHarness.service.markMediaUnavailable();
    requestSize = FileSystemRpcCodec::encodeStatRequest(
        59,
        "projects/absent.bin",
        absentHarness.request,
        sizeof(absentHarness.request)
    );
    responseSize = absentHarness.transact(requestSize, 0);
    const auto absent = FileSystemRpcCodec::decodeStatusResponse(
        absentHarness.response,
        responseSize
    );
    assert(absent && absent.value().status == FileSystemRpcStatus::STORAGE_ERROR);

    requestSize = FileSystemRpcCodec::encodeCapabilitiesRequest(
        60,
        absentHarness.request,
        sizeof(absentHarness.request)
    );
    responseSize = absentHarness.transact(requestSize, 1);
    const auto capabilities = FileSystemRpcCodec::decodeCapabilitiesResponse(
        absentHarness.response,
        responseSize
    );
    assert(capabilities && capabilities.value().status == FileSystemRpcStatus::OK);

    std::cout
        << "[PASS] test_storage_gate_maps_busy_exhausted_absent_and_io_failures\n";
}

void test_conditional_replace_is_cas_and_idempotent() {
    resetTestRoot();
    Harness h;
    assert(core::test::writeProductFileFixture(
        h.service,
        "library/step-presets/demo.mssp",
        0,
        reinterpret_cast<const uint8_t*>("old"),
        3
    ));
    assert(core::test::writeProductFileFixture(
        h.service,
        "tmp/step-preset-stage.mssp",
        0,
        reinterpret_cast<const uint8_t*>("new"),
        3
    ));

    size_t requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        60,
        0x12345678U,
        "library/step-presets/demo.mssp",
        "tmp/step-preset-stage.mssp",
        SHA256_OLD,
        SHA256_NEW,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    size_t responseSize = h.transact(requestSize);
    auto mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        h.response,
        responseSize
    );
    assert(mutation);
    assert(mutation.value().requestId == 60);
    assert(mutation.value().operationId == 0x12345678U);
    assert(mutation.value().status == FileSystemRpcStatus::OK);
    assert(mutation.value().outcome == FileSystemRpcMutationOutcome::APPLIED);
    assert(mutation.value().subject == FileSystemRpcMutationSubject::NONE);

    uint8_t loaded[8] = {};
    auto read = h.service.read("library/step-presets/demo.mssp", 0, loaded, sizeof(loaded));
    assert(read && read.value() == 3);
    assert(std::memcmp(loaded, "new", 3) == 0);
    assert(!h.service.stat("tmp/step-preset-stage.mssp"));
    assert(!h.service.stat("tmp/rpc-conditional.backup"));
    assert(!h.service.stat("tmp/rpc-conditional.journal"));

    requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        61,
        0x12345678U,
        "library/step-presets/demo.mssp",
        "tmp/step-preset-stage.mssp",
        SHA256_OLD,
        SHA256_NEW,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize);
    mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(h.response, responseSize);
    assert(mutation);
    assert(mutation.value().status == FileSystemRpcStatus::OK);
    assert(mutation.value().outcome == FileSystemRpcMutationOutcome::ALREADY_APPLIED);

    std::cout << "[PASS] test_conditional_replace_is_cas_and_idempotent\n";
}

void test_conditional_replace_rejects_source_and_staging_mismatch() {
    resetTestRoot();
    Harness h;
    assert(core::test::writeProductFileFixture(
        h.service,
        "library/step-presets/demo.mssp",
        0,
        reinterpret_cast<const uint8_t*>("old"),
        3
    ));
    assert(core::test::writeProductFileFixture(
        h.service,
        "tmp/step-preset-stage.mssp",
        0,
        reinterpret_cast<const uint8_t*>("tampered"),
        8
    ));

    size_t requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        62,
        2,
        "library/step-presets/demo.mssp",
        "tmp/step-preset-stage.mssp",
        SHA256_TAMPERED,
        SHA256_NEW,
        h.request,
        sizeof(h.request)
    );
    size_t responseSize = h.transact(requestSize);
    auto mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        h.response,
        responseSize
    );
    assert(mutation);
    assert(mutation.value().status == FileSystemRpcStatus::PRECONDITION_FAILED);
    assert(mutation.value().subject == FileSystemRpcMutationSubject::SOURCE);
    assert(std::memcmp(mutation.value().observedSha256, SHA256_OLD, 32) == 0);

    requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        63,
        3,
        "library/step-presets/demo.mssp",
        "tmp/step-preset-stage.mssp",
        SHA256_OLD,
        SHA256_NEW,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize);
    mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(h.response, responseSize);
    assert(mutation);
    assert(mutation.value().status == FileSystemRpcStatus::PRECONDITION_FAILED);
    assert(mutation.value().subject == FileSystemRpcMutationSubject::STAGING);
    assert(std::memcmp(mutation.value().observedSha256, SHA256_TAMPERED, 32) == 0);

    uint8_t loaded[8] = {};
    auto read = h.service.read("library/step-presets/demo.mssp", 0, loaded, sizeof(loaded));
    assert(read && read.value() == 3 && std::memcmp(loaded, "old", 3) == 0);
    assert(!h.service.stat("tmp/rpc-conditional.backup"));
    assert(!h.service.stat("tmp/rpc-conditional.journal"));

    std::cout << "[PASS] test_conditional_replace_rejects_source_and_staging_mismatch\n";
}

void test_conditional_replace_rejects_case_alias_of_same_fat_path() {
    resetTestRoot();
    Harness h;
    assert(core::test::writeProductFileFixture(
        h.service,
        "tmp/conditional-alias.mssp",
        0,
        reinterpret_cast<const uint8_t*>("new"),
        3
    ));

    const size_t requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        64,
        0x414C4941U,
        "tmp/conditional-alias.mssp",
        "TMP/CONDITIONAL-ALIAS.MSSP",
        SHA256_NEW,
        SHA256_NEW,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0);
    const size_t responseSize = h.transact(requestSize);
    const auto mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        h.response,
        responseSize
    );
    assert(mutation);
    assert(mutation.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);

    uint8_t loaded[8] = {};
    const auto read = h.service.read(
        "tmp/conditional-alias.mssp",
        0,
        loaded,
        sizeof(loaded)
    );
    assert(read && read.value() == 3);
    assert(std::memcmp(loaded, "new", 3) == 0);
    assert(!h.service.stat("tmp/rpc-conditional.backup"));
    assert(!h.service.stat("tmp/rpc-conditional.journal"));

    std::cout << "[PASS] test_conditional_replace_rejects_case_alias_of_same_fat_path\n";
}

void test_conditional_delete_is_cas_and_idempotent() {
    resetTestRoot();
    Harness h;
    assert(core::test::writeProductFileFixture(
        h.service,
        "library/step-presets/delete.mssp",
        0,
        reinterpret_cast<const uint8_t*>("delete-me"),
        9
    ));

    size_t requestSize = FileSystemRpcCodec::encodeConditionalDeleteRequest(
        64,
        0x87654321U,
        "library/step-presets/delete.mssp",
        SHA256_DELETE_ME,
        h.request,
        sizeof(h.request)
    );
    size_t responseSize = h.transact(requestSize);
    auto mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        h.response,
        responseSize
    );
    assert(mutation);
    assert(mutation.value().status == FileSystemRpcStatus::OK);
    assert(mutation.value().outcome == FileSystemRpcMutationOutcome::APPLIED);
    assert(!h.service.stat("library/step-presets/delete.mssp"));
    assert(!h.service.stat("tmp/rpc-conditional.backup"));
    assert(!h.service.stat("tmp/rpc-conditional.journal"));

    requestSize = FileSystemRpcCodec::encodeConditionalDeleteRequest(
        65,
        0x87654321U,
        "library/step-presets/delete.mssp",
        SHA256_DELETE_ME,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize);
    mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(h.response, responseSize);
    assert(mutation);
    assert(mutation.value().status == FileSystemRpcStatus::OK);
    assert(mutation.value().outcome == FileSystemRpcMutationOutcome::ALREADY_APPLIED);

    std::cout << "[PASS] test_conditional_delete_is_cas_and_idempotent\n";
}

void test_conditional_replace_requires_full_recovery_authority() {
    resetTestRoot();
    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FileSystemRpcHandler handler(service, catalog);
    assert(core::test::writeProductFileFixture(
        service,
        "library/step-presets/demo.mssp",
        0,
        reinterpret_cast<const uint8_t*>("old"),
        3
    ));
    assert(core::test::writeProductFileFixture(
        service,
        "tmp/step-preset-stage.mssp",
        0,
        reinterpret_cast<const uint8_t*>("new"),
        3
    ));
    filesystem.failConditionalPromotion = true;
    filesystem.failConditionalRestore = true;

    uint8_t request[1024] = {};
    uint8_t response[1024] = {};
    size_t requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        66,
        4,
        "library/step-presets/demo.mssp",
        "tmp/step-preset-stage.mssp",
        SHA256_OLD,
        SHA256_NEW,
        request,
        sizeof(request)
    );
    auto handled = handler.handleFrame(request, requestSize, 0, response, sizeof(response));
    assert(handled);
    auto mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        response,
        handled.value()
    );
    assert(mutation && mutation.value().status == FileSystemRpcStatus::STORAGE_ERROR);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assertProductReadBlocked(service, "library/step-presets/demo.mssp");
    assertProductReadBlocked(service, "tmp/rpc-conditional.backup");
    assertProductReadBlocked(service, "tmp/rpc-conditional.journal");
    assertProductReadBlocked(service, "tmp/step-preset-stage.mssp");
    assert(!filesystem.delegate.stat(
        "/midi-studio/library/step-presets/demo.mssp"
    ));
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-conditional.backup"));
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-conditional.journal"));
    assert(filesystem.delegate.stat("/midi-studio/tmp/step-preset-stage.mssp"));

    filesystem.failConditionalPromotion = false;
    filesystem.failConditionalRestore = false;
    requestSize = FileSystemRpcCodec::encodeStatRequest(
        67,
        "library/step-presets/demo.mssp",
        request,
        sizeof(request)
    );
    handled = handler.handleFrame(request, requestSize, 1, response, sizeof(response));
    assert(handled);
    auto blocked = FileSystemRpcCodec::decodeStatusResponse(response, handled.value());
    assert(blocked && blocked.value().status == FileSystemRpcStatus::BUSY);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-conditional.backup"));
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-conditional.journal"));

    handled = handler.handleFrame(request, requestSize, 500, response, sizeof(response));
    assert(handled);
    blocked = FileSystemRpcCodec::decodeStatusResponse(response, handled.value());
    assert(blocked && blocked.value().status == FileSystemRpcStatus::BUSY);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);

    completeExternalProductRecovery(service);
    handled = handler.handleFrame(request, requestSize, 501, response, sizeof(response));
    assert(handled);
    auto stat = FileSystemRpcCodec::decodeStatResponse(response, handled.value());
    assert(stat && stat.value().status == FileSystemRpcStatus::OK);

    uint8_t loaded[8] = {};
    auto read = service.read("library/step-presets/demo.mssp", 0, loaded, sizeof(loaded));
    assert(read && read.value() == 3 && std::memcmp(loaded, "new", 3) == 0);
    assert(!service.stat("tmp/rpc-conditional.backup"));
    assert(!service.stat("tmp/rpc-conditional.journal"));

    std::cout
        << "[PASS] test_conditional_replace_requires_full_recovery_authority\n";
}

void test_conditional_delete_requires_full_recovery_authority() {
    resetTestRoot();
    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FileSystemRpcHandler handler(service, catalog);
    assert(core::test::writeProductFileFixture(
        service,
        "library/step-presets/delete.mssp",
        0,
        reinterpret_cast<const uint8_t*>("delete-me"),
        9
    ));
    filesystem.failConditionalBackupRemove = true;

    uint8_t request[1024] = {};
    uint8_t response[1024] = {};
    size_t requestSize = FileSystemRpcCodec::encodeConditionalDeleteRequest(
        68,
        5,
        "library/step-presets/delete.mssp",
        SHA256_DELETE_ME,
        request,
        sizeof(request)
    );
    auto handled = handler.handleFrame(request, requestSize, 0, response, sizeof(response));
    assert(handled);
    auto mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        response,
        handled.value()
    );
    assert(mutation && mutation.value().status == FileSystemRpcStatus::STORAGE_ERROR);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assertProductReadBlocked(service, "library/step-presets/delete.mssp");
    assertProductReadBlocked(service, "tmp/rpc-conditional.backup");
    assertProductReadBlocked(service, "tmp/rpc-conditional.journal");
    assert(!filesystem.delegate.stat(
        "/midi-studio/library/step-presets/delete.mssp"
    ));
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-conditional.backup"));
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-conditional.journal"));

    filesystem.failConditionalBackupRemove = false;
    requestSize = FileSystemRpcCodec::encodeStatRequest(
        69,
        "library/step-presets/delete.mssp",
        request,
        sizeof(request)
    );
    handled = handler.handleFrame(request, requestSize, 1, response, sizeof(response));
    assert(handled);
    auto blocked = FileSystemRpcCodec::decodeStatusResponse(response, handled.value());
    assert(blocked && blocked.value().status == FileSystemRpcStatus::BUSY);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-conditional.backup"));
    assert(filesystem.delegate.stat("/midi-studio/tmp/rpc-conditional.journal"));

    handled = handler.handleFrame(request, requestSize, 500, response, sizeof(response));
    assert(handled);
    blocked = FileSystemRpcCodec::decodeStatusResponse(response, handled.value());
    assert(blocked && blocked.value().status == FileSystemRpcStatus::BUSY);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);

    completeExternalProductRecovery(service);
    handled = handler.handleFrame(request, requestSize, 501, response, sizeof(response));
    assert(handled);
    auto stat = FileSystemRpcCodec::decodeStatResponse(response, handled.value());
    assert(stat && stat.value().status == FileSystemRpcStatus::NOT_FOUND);
    assert(!service.stat("tmp/rpc-conditional.backup"));
    assert(!service.stat("tmp/rpc-conditional.journal"));

    std::cout
        << "[PASS] test_conditional_delete_requires_full_recovery_authority\n";
}

void test_conditional_journal_promotion_failure_is_non_mutating() {
    resetTestRoot();
    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FileSystemRpcHandler handler(service, catalog);
    assert(core::test::writeProductFileFixture(
        service,
        "library/step-presets/demo.mssp",
        0,
        reinterpret_cast<const uint8_t*>("old"),
        3
    ));
    assert(core::test::writeProductFileFixture(
        service,
        "tmp/step-preset-stage.mssp",
        0,
        reinterpret_cast<const uint8_t*>("new"),
        3
    ));
    filesystem.failConditionalJournalPromotion = true;

    uint8_t request[1024] = {};
    uint8_t response[1024] = {};
    const size_t requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        70,
        6,
        "library/step-presets/demo.mssp",
        "tmp/step-preset-stage.mssp",
        SHA256_OLD,
        SHA256_NEW,
        request,
        sizeof(request)
    );
    const auto handled = handler.handleFrame(
        request,
        requestSize,
        0,
        response,
        sizeof(response)
    );
    assert(handled);
    const auto mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        response,
        handled.value()
    );
    assert(mutation);
    assert(mutation.value().status == FileSystemRpcStatus::STORAGE_ERROR);
    assert(mutation.value().outcome == FileSystemRpcMutationOutcome::NONE);

    uint8_t loaded[8] = {};
    auto blockedRead = service.read(
        "library/step-presets/demo.mssp",
        0,
        loaded,
        sizeof(loaded)
    );
    assert(!blockedRead);
    assert(blockedRead.error().code == oc::type::ErrorCode::HARDWARE_BUSY);
    blockedRead = service.read(
        "tmp/step-preset-stage.mssp",
        0,
        loaded,
        sizeof(loaded)
    );
    assert(!blockedRead);
    assert(blockedRead.error().code == oc::type::ErrorCode::HARDWARE_BUSY);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assert(filesystem.delegate.stat(
        "/midi-studio/library/step-presets/demo.mssp"
    ));
    assert(filesystem.delegate.stat("/midi-studio/tmp/step-preset-stage.mssp"));
    assert(!filesystem.delegate.stat("/midi-studio/tmp/rpc-conditional.backup"));
    assert(!filesystem.delegate.stat("/midi-studio/tmp/rpc-conditional.journal"));
    assert(!filesystem.delegate.stat("/midi-studio/tmp/rpc-conditional.journal.tmp"));

    handler.update(499);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    handler.update(500);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);

    completeExternalProductRecovery(service);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::READY);

    auto read = service.read(
        "library/step-presets/demo.mssp",
        0,
        loaded,
        sizeof(loaded)
    );
    assert(read && read.value() == 3 && std::memcmp(loaded, "old", 3) == 0);
    read = service.read(
        "tmp/step-preset-stage.mssp",
        0,
        loaded,
        sizeof(loaded)
    );
    assert(read && read.value() == 3 && std::memcmp(loaded, "new", 3) == 0);
    assert(!service.stat("tmp/rpc-conditional.backup"));
    assert(!service.stat("tmp/rpc-conditional.journal"));
    assert(!service.stat("tmp/rpc-conditional.journal.tmp"));

    std::cout
        << "[PASS] test_conditional_journal_promotion_failure_is_non_mutating\n";
}

void test_orphan_conditional_journal_staging_is_cleaned_on_recovery() {
    resetTestRoot();
    Harness h;
    const uint8_t partial[] = {'F', 'S', 'T', 'X'};
    assert(core::test::writeProductFileFixture(
        h.service,
        "tmp/rpc-conditional.journal.tmp",
        0,
        partial,
        sizeof(partial)
    ));

    const size_t requestSize = FileSystemRpcCodec::encodeStatRequest(
        71,
        "projects/missing.bin",
        h.request,
        sizeof(h.request)
    );
    const size_t responseSize = h.transact(requestSize);
    const auto stat = FileSystemRpcCodec::decodeStatResponse(
        h.response,
        responseSize
    );
    assert(stat && stat.value().status == FileSystemRpcStatus::NOT_FOUND);
    assert(!h.service.stat("tmp/rpc-conditional.journal.tmp"));

    std::cout
        << "[PASS] test_orphan_conditional_journal_staging_is_cleaned_on_recovery\n";
}

void test_truncated_conditional_journal_is_quarantined_once() {
    static constexpr uint8_t truncated[] = {'F', 'S', 'T', 'X'};
    assertCorruptJournalIsQuarantinedOnce(truncated, sizeof(truncated));
    std::cout
        << "[PASS] test_truncated_conditional_journal_is_quarantined_once\n";
}

void test_bad_crc_conditional_journal_is_quarantined_once() {
    const auto badCrc = makeCrcInvalidConditionalDeleteJournal();
    assertCorruptJournalIsQuarantinedOnce(badCrc.data(), badCrc.size());
    std::cout
        << "[PASS] test_bad_crc_conditional_journal_is_quarantined_once\n";
}

void test_conditional_replace_rejects_fat_short_name_alias_syntax() {
    resetTestRoot();
    Harness h;
    size_t requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        72,
        7,
        "tmp/CURRENT~1.MSS",
        "tmp/normal-stage.mssp",
        SHA256_OLD,
        SHA256_NEW,
        h.request,
        sizeof(h.request)
    );
    size_t responseSize = h.transact(requestSize);
    auto mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        h.response,
        responseSize
    );
    assert(mutation);
    assert(mutation.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);

    requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        73,
        8,
        "tmp/current.mssp",
        "tmp/NORMAL~1.MSS",
        SHA256_OLD,
        SHA256_NEW,
        h.request,
        sizeof(h.request)
    );
    responseSize = h.transact(requestSize);
    mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        h.response,
        responseSize
    );
    assert(mutation);
    assert(mutation.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);

    std::cout
        << "[PASS] test_conditional_replace_rejects_fat_short_name_alias_syntax\n";
}

void test_protocol_transaction_paths_are_reserved() {
    resetTestRoot();
    Harness h;
    const size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        70,
        0x2000,
        "tmp/rpc-conditional.journal",
        4,
        h.request,
        sizeof(h.request)
    );
    const size_t responseSize = h.transact(requestSize);
    auto write = FileSystemRpcCodec::decodeWriteResponse(h.response, responseSize);
    assert(write);
    assert(write.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);
    assert(!h.handler.hasActiveWriteSession());

    size_t nextRequestSize = FileSystemRpcCodec::encodeMkdirRequest(
        71,
        "tmp/rpc-conditional.backup",
        h.request,
        sizeof(h.request)
    );
    size_t nextResponseSize = h.transact(nextRequestSize);
    auto status = FileSystemRpcCodec::decodeStatusResponse(
        h.response,
        nextResponseSize
    );
    assert(status && status.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);

    nextRequestSize = FileSystemRpcCodec::encodeDeleteRequest(
        72,
        "tmp/rpc-conditional.journal",
        false,
        h.request,
        sizeof(h.request)
    );
    nextResponseSize = h.transact(nextRequestSize);
    status = FileSystemRpcCodec::decodeStatusResponse(h.response, nextResponseSize);
    assert(status && status.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);

    const uint8_t byte = 1;
    assert(core::test::writeProductFileFixture(h.service, "tmp/source.bin", 0, &byte, 1));
    nextRequestSize = FileSystemRpcCodec::encodeRenameRequest(
        73,
        "tmp/source.bin",
        "tmp/rpc-conditional.backup",
        h.request,
        sizeof(h.request)
    );
    nextResponseSize = h.transact(nextRequestSize);
    status = FileSystemRpcCodec::decodeStatusResponse(h.response, nextResponseSize);
    assert(status && status.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);
    assert(h.service.stat("tmp/source.bin"));

    nextRequestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        74,
        0x2001,
        "TMP/RPC-CONDITIONAL.JOURNAL",
        4,
        h.request,
        sizeof(h.request)
    );
    nextResponseSize = h.transact(nextRequestSize);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, nextResponseSize);
    assert(write && write.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);
    assert(!h.handler.hasActiveWriteSession());

    nextRequestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        75,
        0x2002,
        "TMP/RPC-CO~1.JOU",
        4,
        h.request,
        sizeof(h.request)
    );
    nextResponseSize = h.transact(nextRequestSize);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, nextResponseSize);
    assert(write && write.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);
    assert(!h.handler.hasActiveWriteSession());

    nextRequestSize = FileSystemRpcCodec::encodeDeleteRequest(
        76,
        "tmp/RPC-CO~2.BAC",
        false,
        h.request,
        sizeof(h.request)
    );
    nextResponseSize = h.transact(nextRequestSize);
    status = FileSystemRpcCodec::decodeStatusResponse(h.response, nextResponseSize);
    assert(status && status.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);

    nextRequestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        77,
        0x2003,
        "tmp/rpc-product-file-a.journal",
        4,
        h.request,
        sizeof(h.request)
    );
    nextResponseSize = h.transact(nextRequestSize);
    write = FileSystemRpcCodec::decodeWriteResponse(h.response, nextResponseSize);
    assert(write && write.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);
    assert(!h.handler.hasActiveWriteSession());

    nextRequestSize = FileSystemRpcCodec::encodeDeleteRequest(
        78,
        "TMP/RPC-PRODUCT-FILE-B.JOURNAL",
        false,
        h.request,
        sizeof(h.request)
    );
    nextResponseSize = h.transact(nextRequestSize);
    status = FileSystemRpcCodec::decodeStatusResponse(h.response, nextResponseSize);
    assert(status && status.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);

    nextRequestSize = FileSystemRpcCodec::encodeMkdirRequest(
        79,
        "TMP/RPC-D",
        h.request,
        sizeof(h.request)
    );
    nextResponseSize = h.transact(nextRequestSize);
    status = FileSystemRpcCodec::decodeStatusResponse(h.response, nextResponseSize);
    assert(status && status.value().status == FileSystemRpcStatus::INVALID_ARGUMENT);

    std::cout << "[PASS] test_protocol_transaction_paths_are_reserved\n";
}

void test_endpoint_answers_only_filesystem_requests() {
    resetTestRoot();
    g_now_ms = 0U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());

    const uint8_t payload[] = {'o', 'k'};
    assert(core::test::writeProductFileFixture(
        service, "projects/endpoint.bin", 0, payload, sizeof(payload)
    ));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();
    assert(endpoint.active());
    assert(transport.onReceive);

    uint8_t request[256] = {};
    const uint8_t nonFilesystem[] = {0x01, 0x00, 0x00};
    transport.emit(nonFilesystem, sizeof(nonFilesystem));
    assert(transport.sendCount == 0);

    const size_t requestSize = FileSystemRpcCodec::encodeStatRequest(
        51,
        "projects/endpoint.bin",
        request,
        sizeof(request)
    );
    assert(requestSize > 0);
    core::persistence::ProductPersistenceWorkUsage receiveUsage{};
    {
        auto measuredResult = service.measurePersistenceWork(receiveUsage);
        assert(measuredResult);
        auto measured = std::move(measuredResult.value());
        transport.emit(request, requestSize);
    }
    assert(receiveUsage.filesystemCalls == 0U);
    assert(receiveUsage.bytes == 0U);
    assert(transport.sendCount == 0U);
    assert(service.persistenceJobs().depth() == 1U);

    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(transport.sendCount == 1U);
    assert(service.persistenceJobs().depth() == 0U);

    auto stat = FileSystemRpcCodec::decodeStatResponse(transport.sent, transport.sentSize);
    assert(stat);
    assert(stat.value().requestId == 51);
    assert(stat.value().status == FileSystemRpcStatus::OK);
    assert(stat.value().sizeBytes == sizeof(payload));

    endpoint.end();
    assert(!endpoint.active());
    assert(!transport.onReceive);

    std::cout << "[PASS] test_endpoint_answers_only_filesystem_requests\n";
}

void test_endpoint_recursive_delete_is_hide_first_and_one_node_per_turn() {
    resetTestRoot();
    g_now_ms = 10U;

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    auto leaseResult = service.acquireMutation(
        core::persistence::ProductMutationOwner::PROJECT
    );
    assert(leaseResult);
    auto lease = std::move(leaseResult.value());
    assert(service.createDirectory(lease, "projects/delete-tree/a/b"));
    const uint8_t payload[] = {'d', 'e', 'l'};
    assert(service.write(
        lease,
        "projects/delete-tree/a/b/deep.bin",
        0U,
        payload,
        sizeof(payload)
    ));
    assert(service.write(
        lease,
        "projects/delete-tree/root.bin",
        0U,
        payload,
        sizeof(payload)
    ));
    assert(service.releaseMutation(lease));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    uint8_t request[256] = {};
    const size_t requestSize = FileSystemRpcCodec::encodeDeleteRequest(
        0x5151U,
        "projects/delete-tree",
        true,
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);
    assert(transport.sendCount == 0U);

    // Admission only retains the PSRAM continuation and mutation lease.
    filesystem.resetWorkCounters();
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(filesystem.filesystemCalls == 0U);
    assert(transport.sendCount == 0U);
    assert(std::filesystem::exists(
        testRoot() / "midi-studio" / "projects" / "delete-tree"
    ));

    // The pre-hide check is bounded to the marker and canonical stat.
    ++g_now_ms;
    filesystem.resetWorkCounters();
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(filesystem.filesystemCalls == 2U);
    assert(transport.sendCount == 0U);
    assert(std::filesystem::exists(
        testRoot() / "midi-studio" / "projects" / "delete-tree"
    ));

    // One atomic rename removes the canonical tree before any child deletion.
    ++g_now_ms;
    filesystem.resetWorkCounters();
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(filesystem.filesystemCalls == 1U);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "projects" / "delete-tree"
    ));
    assert(std::filesystem::exists(
        testRoot() / "midi-studio" / "tmp" / "rpc-d"
    ));

    // A retained destructive continuation freezes completely during playback.
    ++g_now_ms;
    filesystem.resetWorkCounters();
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, true);
    assert(filesystem.filesystemCalls == 0U);
    assert(transport.sendCount == 0U);

    uint8_t cleanupAdvances = 0U;
    while (transport.sendCount == 0U && cleanupAdvances < 32U) {
        ++g_now_ms;
        ++cleanupAdvances;
        filesystem.resetWorkCounters();
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        assert(filesystem.filesystemCalls <=
               core::persistence::PRODUCT_PERSISTENCE_QUOTA_TREE_CLEANUP
                   .maxFilesystemCalls());
    }

    assert(cleanupAdvances > 3U);
    assert(cleanupAdvances < 32U);
    assert(transport.sendCount == 1U);
    const auto deleted = FileSystemRpcCodec::decodeStatusResponse(
        transport.sent,
        transport.sentSize
    );
    assert(deleted);
    assert(deleted.value().requestId == 0x5151U);
    assert(deleted.value().messageId == FileSystemRpcMessageId::DELETE_RESPONSE);
    assert(deleted.value().status == FileSystemRpcStatus::OK);
    assert(service.persistenceJobs().depth() == 0U);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::READY);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "tmp" / "rpc-d"
    ));

    endpoint.end();
    std::cout << "[PASS] endpoint recursive delete is hide-first and bounded ("
              << static_cast<unsigned>(cleanupAdvances) << " cleanup advances)\n";
}

void test_endpoint_recursive_delete_deadline_preserves_recovery_marker() {
    resetTestRoot();
    g_now_ms = 100U;

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    auto leaseResult = service.acquireMutation(
        core::persistence::ProductMutationOwner::PROJECT
    );
    assert(leaseResult);
    auto lease = std::move(leaseResult.value());
    assert(service.createDirectory(lease, "projects/deadline-tree/a"));
    const uint8_t payload[] = {'x'};
    assert(service.write(
        lease,
        "projects/deadline-tree/a/value.bin",
        0U,
        payload,
        sizeof(payload)
    ));
    assert(service.releaseMutation(lease));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    uint8_t request[256] = {};
    const size_t requestSize = FileSystemRpcCodec::encodeDeleteRequest(
        0x5252U,
        "projects/deadline-tree",
        true,
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);

    // Construct the continuation, precheck both paths, then atomically hide.
    for (uint8_t advance = 0U; advance < 3U; ++advance) {
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        ++g_now_ms;
    }
    assert(transport.sendCount == 0U);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "projects" / "deadline-tree"
    ));
    assert(std::filesystem::exists(
        testRoot() / "midi-studio" / "tmp" / "rpc-d"
    ));

    // The exact total deadline unwinds only control state: canonical content
    // stays hidden and the fixed marker remains for the recovery-priority job.
    g_now_ms = 100U + FILESYSTEM_RPC_TOTAL_WRITE_TIMEOUT_MS;
    filesystem.resetWorkCounters();
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(filesystem.filesystemCalls == 0U);
    assert(transport.sendCount == 1U);
    const auto expired = FileSystemRpcCodec::decodeStatusResponse(
        transport.sent,
        transport.sentSize
    );
    assert(expired);
    assert(expired.value().requestId == 0x5252U);
    assert(expired.value().status == FileSystemRpcStatus::BUSY);
    assert(service.persistenceJobs().depth() == 0U);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "projects" / "deadline-tree"
    ));
    assert(std::filesystem::exists(
        testRoot() / "midi-studio" / "tmp" / "rpc-d"
    ));

    endpoint.end();
    std::cout
        << "[PASS] recursive delete deadline preserves recovery marker\n";
}

void test_endpoint_advance_expires_abandoned_write_session() {
    resetTestRoot();
    g_now_ms = 0U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(
        transport,
        service,
        catalog,
        nowMs,
        FileSystemRpcHandler::Config{100}
    );
    endpoint.begin();

    uint8_t request[256] = {};
    g_now_ms = 1000;
    const size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        52,
        0x1234,
        "projects/abandoned.bin",
        4,
        request,
        sizeof(request)
    );
    assert(requestSize > 0);
    transport.emit(request, requestSize);
    assert(transport.sendCount == 0U);
    assert(service.persistenceJobs().depth() == 1U);

    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    auto response = FileSystemRpcCodec::decodeWriteResponse(
        transport.sent,
        transport.sentSize
    );
    assert(response);
    assert(response.value().status == FileSystemRpcStatus::OK);
    assert(service.stat("tmp/rpc-write-1234.tmp"));
    assert(service.persistenceJobs().depth() == 1U);

    // Timeout cleanup is filesystem work and therefore remains stopped-only.
    g_now_ms = 1100U;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    core::persistence::ProductPersistenceWorkUsage playbackUsage{};
    {
        auto measuredResult = service.measurePersistenceWork(playbackUsage);
        assert(measuredResult);
        auto measured = std::move(measuredResult.value());
        endpoint.advance(g_now_ms, true);
    }
    assert(playbackUsage.filesystemCalls == 0U);
    assert(service.stat("tmp/rpc-write-1234.tmp"));
    assert(service.persistenceJobs().depth() == 1U);

    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(!service.stat("tmp/rpc-write-1234.tmp"));
    assert(!service.stat("projects/abandoned.bin"));
    assert(service.persistenceJobs().depth() == 0U);

    endpoint.end();
    std::cout << "[PASS] test_endpoint_advance_expires_abandoned_write_session\n";
}

void test_endpoint_retains_one_legacy_frame_and_rejects_excess() {
    resetTestRoot();
    g_now_ms = 0U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    uint8_t request[64] = {};
    core::persistence::ProductPersistenceWorkUsage receiveUsage{};
    {
        auto measuredResult = service.measurePersistenceWork(receiveUsage);
        assert(measuredResult);
        auto measured = std::move(measuredResult.value());
        for (uint16_t requestId = 61U; requestId <= 63U; ++requestId) {
            const size_t requestSize = FileSystemRpcCodec::encodeCapabilitiesRequest(
                requestId,
                request,
                sizeof(request)
            );
            assert(requestSize > 0U);
            transport.emit(request, requestSize);
        }
    }

    assert(receiveUsage.filesystemCalls == 0U);
    assert(receiveUsage.bytes == 0U);
    assert(service.persistenceJobs().depth() == 1U);
    assert(service.persistenceJobs().highWater() == 1U);
    assert(transport.sendCount == 2U);
    auto busy = FileSystemRpcCodec::decodeStatusResponse(
        transport.sent,
        transport.sentSize
    );
    assert(busy);
    assert(busy.value().requestId == 63U);
    assert(busy.value().status == FileSystemRpcStatus::BUSY);

    assert(service.persistenceJobs().beginTurn(0U));
    endpoint.advance(0U, false);
    assert(transport.sendCount == 3U);
    auto first = FileSystemRpcCodec::decodeCapabilitiesResponse(
        transport.sent,
        transport.sentSize
    );
    assert(first && first.value().requestId == 61U);
    assert(service.persistenceJobs().depth() == 0U);

    endpoint.end();
    std::cout << "[PASS] endpoint one-frame legacy lease rejects excess\n";
}

void test_endpoint_playback_rejects_without_filesystem_work() {
    resetTestRoot();
    g_now_ms = 0U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    const uint8_t payload[] = {'p'};
    assert(core::test::writeProductFileFixture(
        service, "projects/playback.bin", 0U, payload, sizeof(payload)
    ));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();
    uint8_t request[128] = {};
    const size_t requestSize = FileSystemRpcCodec::encodeStatRequest(
        64U,
        "projects/playback.bin",
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);
    assert(service.persistenceJobs().beginTurn(0U));

    core::persistence::ProductPersistenceWorkUsage usage{};
    {
        auto measuredResult = service.measurePersistenceWork(usage);
        assert(measuredResult);
        auto measured = std::move(measuredResult.value());
        endpoint.advance(0U, true);
    }
    assert(usage.filesystemCalls == 0U);
    assert(usage.bytes == 0U);
    assert(service.persistenceJobs().depth() == 0U);
    auto busy = FileSystemRpcCodec::decodeStatusResponse(
        transport.sent,
        transport.sentSize
    );
    assert(busy);
    assert(busy.value().requestId == 64U);
    assert(busy.value().status == FileSystemRpcStatus::BUSY);

    endpoint.end();
    std::cout << "[PASS] playback rejection performs zero filesystem work\n";
}

void test_endpoint_total_upload_deadline_is_not_refreshed_by_chunks() {
    resetTestRoot();
    g_now_ms = 1000U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    uint8_t request[128] = {};
    size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        65U,
        0x2345U,
        "projects/total-deadline.bin",
        2U,
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(service.stat("tmp/rpc-write-2345.tmp"));

    const uint8_t firstByte = 0x5AU;
    g_now_ms = 9000U;
    requestSize = FileSystemRpcCodec::encodeWriteChunkRequest(
        66U,
        0x2345U,
        0U,
        &firstByte,
        1U,
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    auto chunk = FileSystemRpcCodec::decodeWriteResponse(
        transport.sent,
        transport.sentSize
    );
    assert(chunk && chunk.value().status == FileSystemRpcStatus::OK);
    assert(chunk.value().bytesWritten == 1U);

    g_now_ms = 1000U + FILESYSTEM_RPC_TOTAL_WRITE_TIMEOUT_MS;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(!service.stat("tmp/rpc-write-2345.tmp"));
    assert(!service.stat("projects/total-deadline.bin"));
    assert(service.persistenceJobs().depth() == 0U);

    endpoint.end();
    std::cout << "[PASS] total upload deadline is absolute\n";
}

void test_aged_autosave_aborts_upload_before_promotion() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    test_support::CoreStorages storages;
    core::state::CoreState state(storages.settings);
    state.project.metadata.hasSavedIdentity = true;
    std::strncpy(
        state.project.metadata.id.data(),
        "rpc-autosave",
        state.project.metadata.id.size() - 1U
    );
    state.sequencer.pattern.setContentLength(8U);
    state.sequencer.setStepDataAt(0U, 85U, 100U, 75U);
    state.sequencer.pattern.toggle(0U);
    state.markProjectMutated();

    core::persistence::ProjectSessionStore sessionStore(service);
    core::persistence::ProjectSessionAutosaveService autosave(sessionStore, 1U);
    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    const uint32_t admittedAt = state.projectSessionSaveTimestampMs() + 1U;
    g_now_ms = admittedAt;
    uint8_t request[128] = {};
    const size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        75U,
        0x4567U,
        "projects/aged-upload.bin",
        4U,
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    const auto admitted = autosave.update(state, g_now_ms);
    assert(admitted.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    assert(service.persistenceJobs().depth() == 2U);
    assert(service.persistenceJobs().highWater() == 2U);
    assert(service.stat("tmp/rpc-write-4567.tmp"));
    core::persistence::ProductPersistenceJobSnapshot autosaveSnapshot{};
    assert(autosave.inspectPersistenceJob(autosaveSnapshot));
    assert(autosaveSnapshot.state ==
           core::persistence::ProductPersistenceJobState::DEFERRED);
    assert(autosaveSnapshot.metrics.advances == 0U);

    g_now_ms = admittedAt +
        core::persistence::PRODUCT_PERSISTENCE_AUTOSAVE_MAX_DEFERRAL_MS - 1U;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    const auto stillDeferred = autosave.update(state, g_now_ms);
    assert(stillDeferred.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    assert(service.stat("tmp/rpc-write-4567.tmp"));
    assert(autosave.inspectPersistenceJob(autosaveSnapshot));
    assert(autosaveSnapshot.state ==
           core::persistence::ProductPersistenceJobState::DEFERRED);

    g_now_ms = admittedAt +
        core::persistence::PRODUCT_PERSISTENCE_AUTOSAVE_MAX_DEFERRAL_MS;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    const auto promoted = autosave.update(state, g_now_ms);
    assert(promoted.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    assert(!service.stat("tmp/rpc-write-4567.tmp"));
    assert(!service.stat("projects/aged-upload.bin"));
    assert(service.persistenceJobs().depth() == 1U);
    assert(autosave.inspectPersistenceJob(autosaveSnapshot));
    assert(autosaveSnapshot.state ==
           core::persistence::ProductPersistenceJobState::ACTIVE);
    assert(autosaveSnapshot.metrics.advances == 0U);

    core::persistence::ProjectSessionAutosaveService::Result saved{};
    for (uint16_t turn = 0U; turn < 384U; ++turn) {
        ++g_now_ms;
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        saved = autosave.update(state, g_now_ms);
        if (saved.status !=
            core::persistence::ProjectSessionAutosaveService::Status::SAVING) {
            break;
        }
    }
    assert(saved.saved());
    assert(service.persistenceJobs().depth() == 0U);
    assert(service.stat("session/current.mspj"));

    endpoint.end();
    std::cout << "[PASS] aged autosave safely aborts upload before promotion\n";
}

void test_upload_size_limit_is_inclusive() {
    resetTestRoot();
    Harness h;

    size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        67U,
        0x3456U,
        "projects/max-upload.bin",
        FILESYSTEM_RPC_MAX_UPLOAD_SIZE,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0U);
    size_t responseSize = h.transact(requestSize);
    auto accepted = FileSystemRpcCodec::decodeWriteResponse(
        h.response,
        responseSize
    );
    assert(accepted && accepted.value().status == FileSystemRpcStatus::OK);
    assert(h.handler.hasActiveWriteSession());
    h.handler.abortWriteSession();

    requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        68U,
        0x3457U,
        "projects/oversize-upload.bin",
        FILESYSTEM_RPC_MAX_UPLOAD_SIZE + 1U,
        h.request,
        sizeof(h.request)
    );
    assert(requestSize > 0U);
    responseSize = h.transact(requestSize);
    auto rejected = FileSystemRpcCodec::decodeWriteResponse(
        h.response,
        responseSize
    );
    assert(rejected && rejected.value().status == FileSystemRpcStatus::TOO_LARGE);
    assert(!h.handler.hasActiveWriteSession());

    std::cout << "[PASS] upload size limit is inclusive\n";
}

void test_endpoint_commit_yields_between_bounded_durable_phases() {
    resetTestRoot();
    g_now_ms = 100U;

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    constexpr uint16_t sessionId = 0x4567U;
    const uint8_t payload[] = {'s', 'l', 'i', 'c', 'e', 'd'};
    uint8_t request[256] = {};
    size_t requestSize = FileSystemRpcCodec::encodeWriteBeginRequest(
        69U,
        sessionId,
        "projects/cooperative-commit.bin",
        sizeof(payload),
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(transport.sendCount == 1U);

    ++g_now_ms;
    requestSize = FileSystemRpcCodec::encodeWriteChunkRequest(
        70U,
        sessionId,
        0U,
        payload,
        sizeof(payload),
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(transport.sendCount == 2U);

    ++g_now_ms;
    requestSize = FileSystemRpcCodec::encodeWriteCommitRequest(
        71U,
        sessionId,
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);

    uint8_t commitAdvances = 0U;
    filesystem.resetWorkCounters();
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    ++commitAdvances;
    assert(filesystem.filesystemCalls <=
           core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE
               .maxFilesystemCalls());
    assert(filesystem.ioBytes <=
           core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE.maxBytes());
    assert(transport.sendCount == 2U);
    ++g_now_ms;

    filesystem.resetWorkCounters();
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, true);
    assert(filesystem.filesystemCalls == 0U);
    assert(filesystem.ioBytes == 0U);
    assert(transport.sendCount == 2U);
    ++g_now_ms;

    while (transport.sendCount == 2U && commitAdvances < 32U) {
        filesystem.resetWorkCounters();
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        ++commitAdvances;
        ++g_now_ms;
        assert(filesystem.filesystemCalls <=
               core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE
                   .maxFilesystemCalls());
        assert(filesystem.ioBytes <=
               core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE
                   .maxBytes());
    }

    assert(commitAdvances > 1U);
    assert(commitAdvances < 32U);
    assert(transport.sendCount == 3U);
    auto committed = FileSystemRpcCodec::decodeWriteResponse(
        transport.sent,
        transport.sentSize
    );
    assert(committed);
    assert(committed.value().requestId == 71U);
    assert(committed.value().status == FileSystemRpcStatus::OK);
    assert(service.persistenceJobs().depth() == 0U);
    assertProductFileEquals(
        service,
        "projects/cooperative-commit.bin",
        payload,
        sizeof(payload)
    );
    assert(!service.stat("tmp/rpc-write-4567.tmp"));
    assert(!service.stat("tmp/rpc-backup-4567.tmp"));

    endpoint.end();
    std::cout << "[PASS] endpoint cooperative commit phases ("
              << static_cast<unsigned>(commitAdvances) << " advances)\n";
}

void test_endpoint_conditional_replace_is_cooperative_and_playback_safe() {
    resetTestRoot();
    g_now_ms = 200U;

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    assert(core::test::writeProductFileFixture(
        service,
        "library/step-presets/cooperative.mssp",
        0U,
        reinterpret_cast<const uint8_t*>("old"),
        3U
    ));
    assert(core::test::writeProductFileFixture(
        service,
        "tmp/cooperative-stage.mssp",
        0U,
        reinterpret_cast<const uint8_t*>("new"),
        3U
    ));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();

    uint8_t request[256] = {};
    constexpr uint32_t operationId = 0x43505243U;
    const size_t requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        72U,
        operationId,
        "library/step-presets/cooperative.mssp",
        "tmp/cooperative-stage.mssp",
        SHA256_OLD,
        SHA256_NEW,
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);

    // The admission turn only parses the frame, acquires the global mutation
    // lease and retains the continuation. It performs no filesystem work.
    filesystem.resetWorkCounters();
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    uint8_t mutationAdvances = 1U;
    assert(filesystem.filesystemCalls == 0U);
    assert(filesystem.ioBytes == 0U);
    assert(transport.sendCount == 0U);
    assert(service.persistenceJobs().depth() == 1U);

    // A retained durable operation is frozen while music is active.
    ++g_now_ms;
    filesystem.resetWorkCounters();
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, true);
    assert(filesystem.filesystemCalls == 0U);
    assert(filesystem.ioBytes == 0U);
    assert(transport.sendCount == 0U);

    while (transport.sendCount == 0U && mutationAdvances < 64U) {
        ++g_now_ms;
        filesystem.resetWorkCounters();
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        ++mutationAdvances;
        assert(filesystem.filesystemCalls <=
               core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE
                   .maxFilesystemCalls());
        assert(filesystem.ioBytes <=
               core::persistence::PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO.maxBytes());
    }

    assert(mutationAdvances > 2U);
    assert(mutationAdvances < 64U);
    assert(transport.sendCount == 1U);
    const auto mutation = FileSystemRpcCodec::decodeConditionalMutationResponse(
        transport.sent,
        transport.sentSize
    );
    assert(mutation);
    assert(mutation.value().requestId == 72U);
    assert(mutation.value().operationId == operationId);
    assert(mutation.value().status == FileSystemRpcStatus::OK);
    assert(mutation.value().outcome == FileSystemRpcMutationOutcome::APPLIED);
    assert(service.persistenceJobs().depth() == 0U);
    assertProductFileEquals(
        service,
        "library/step-presets/cooperative.mssp",
        reinterpret_cast<const uint8_t*>("new"),
        3U
    );
    assert(!service.stat("tmp/cooperative-stage.mssp"));
    assert(!service.stat("tmp/rpc-conditional.backup"));
    assert(!service.stat("tmp/rpc-conditional.journal.tmp"));
    assert(!service.stat("tmp/rpc-conditional.journal"));

    endpoint.end();
    std::cout << "[PASS] endpoint cooperative conditional replace ("
              << static_cast<unsigned>(mutationAdvances) << " advances)\n";
}

void test_endpoint_reaps_conditional_continuation_after_media_invalidation() {
    resetTestRoot();
    g_now_ms = 300U;

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    assert(core::test::writeProductFileFixture(
        service,
        "library/step-presets/media-loss.mssp",
        0U,
        reinterpret_cast<const uint8_t*>("old"),
        3U
    ));
    assert(core::test::writeProductFileFixture(
        service,
        "tmp/media-loss-stage.mssp",
        0U,
        reinterpret_cast<const uint8_t*>("new"),
        3U
    ));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();
    uint8_t request[256] = {};
    const size_t requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        73U,
        0x4D454449U,
        "library/step-presets/media-loss.mssp",
        "tmp/media-loss-stage.mssp",
        SHA256_OLD,
        SHA256_NEW,
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, false);
    assert(transport.sendCount == 0U);
    assert(service.persistenceJobs().depth() == 1U);

    service.markMediaUnavailable();
    filesystem.resetWorkCounters();
    ++g_now_ms;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, true);
    assert(filesystem.filesystemCalls == 0U);
    assert(filesystem.ioBytes == 0U);
    assert(service.persistenceJobs().depth() == 0U);
    assert(transport.sendCount == 1U);
    const auto unavailable = FileSystemRpcCodec::decodeStatusResponse(
        transport.sent,
        transport.sentSize
    );
    assert(unavailable);
    assert(unavailable.value().requestId == 73U);
    assert(unavailable.value().status == FileSystemRpcStatus::STORAGE_ERROR);

    endpoint.end();
    std::cout << "[PASS] media invalidation reaps conditional continuation\n";
}

void test_endpoint_deadline_cancels_durable_conditional_into_recovery() {
    resetTestRoot();
    constexpr uint32_t admittedAtMs = 400U;
    g_now_ms = admittedAtMs;

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    assert(core::test::writeProductFileFixture(
        service,
        "library/step-presets/deadline.mssp",
        0U,
        reinterpret_cast<const uint8_t*>("old"),
        3U
    ));
    assert(core::test::writeProductFileFixture(
        service,
        "tmp/deadline-stage.mssp",
        0U,
        reinterpret_cast<const uint8_t*>("new"),
        3U
    ));

    ProductDirectoryCatalog catalog(service);
    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, catalog, nowMs);
    endpoint.begin();
    uint8_t request[256] = {};
    const size_t requestSize = FileSystemRpcCodec::encodeConditionalReplaceRequest(
        74U,
        0x44454144U,
        "library/step-presets/deadline.mssp",
        "tmp/deadline-stage.mssp",
        SHA256_OLD,
        SHA256_NEW,
        request,
        sizeof(request)
    );
    assert(requestSize > 0U);
    transport.emit(request, requestSize);

    bool journalDurable = false;
    for (uint8_t advances = 0U; advances < 32U && !journalDurable; ++advances) {
        assert(service.persistenceJobs().beginTurn(g_now_ms));
        endpoint.advance(g_now_ms, false);
        journalDurable = static_cast<bool>(
            service.stat("tmp/rpc-conditional.journal")
        );
        ++g_now_ms;
    }
    assert(journalDurable);
    assert(transport.sendCount == 0U);
    assert(service.persistenceJobs().depth() == 1U);

    g_now_ms = admittedAtMs + FILESYSTEM_RPC_TOTAL_WRITE_TIMEOUT_MS;
    assert(service.persistenceJobs().beginTurn(g_now_ms));
    endpoint.advance(g_now_ms, true);
    assert(transport.sendCount == 1U);
    const auto expired = FileSystemRpcCodec::decodeStatusResponse(
        transport.sent,
        transport.sentSize
    );
    assert(expired);
    assert(expired.value().requestId == 74U);
    assert(expired.value().status == FileSystemRpcStatus::BUSY);
    assert(service.persistenceJobs().depth() == 0U);
    assert(service.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assert(filesystem.stat("/midi-studio/tmp/rpc-conditional.journal"));
    uint8_t current[3] = {};
    const auto read = filesystem.read(
        "/midi-studio/library/step-presets/deadline.mssp",
        0U,
        current,
        sizeof(current)
    );
    assert(read && read.value() == sizeof(current));
    assert(std::memcmp(current, "old", sizeof(current)) == 0);

    endpoint.end();
    std::cout << "[PASS] conditional deadline preserves durable recovery evidence\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "FileSystemRpc tests\n";
    std::cout << "==============================================\n\n";

    test_job_codec_matches_bridge_golden_vectors();
    test_job_codec_start_subset_and_request_validation();
    test_job_codec_response_semantics_and_retention();
    test_endpoint_job_control_is_immediate_bounded_and_io_free();
    test_endpoint_job_malformed_frames_fail_before_admission();
    test_endpoint_job_completion_is_polled_retained_and_not_unsolicited();
    test_endpoint_job_safe_cancel_deadline_and_media_are_typed();
    test_endpoint_job_terminal_cache_has_exact_bounded_capacity();
    test_endpoint_job_write_commit_retains_late_completion_and_identity();
    test_endpoint_job_conditional_journal_retains_late_completion();
    test_endpoint_job_rename_and_conditional_delete_complete();
    test_endpoint_job_safe_cancel_unwinds_reversible_conditional_plan();
    test_endpoint_job_recursive_delete_cancel_too_late_continues();
    test_endpoint_job_deadline_after_hide_retains_completion();
    test_stat_and_read_roundtrip();
    test_capabilities_roundtrip();
    test_list_is_paginated_and_bounded();
    test_write_session_commits_atomically();
    test_write_session_commits_empty_file();
    test_write_session_aborts_on_short_append();
    test_write_commit_rejects_same_size_backend_corruption_before_mapping();
    test_write_commit_propagates_final_stat_error();
    test_write_commit_requires_recovery_when_promotion_fails();
    test_write_recovery_retains_backup_when_restore_fails();
    test_write_session_abort_and_timeout_cleanup();
    test_invalid_path_maps_to_error_status();
    test_read_error_response_is_decodable();
    test_file_management_operations();
    test_storage_gate_maps_busy_exhausted_absent_and_io_failures();
    test_conditional_replace_is_cas_and_idempotent();
    test_conditional_replace_rejects_source_and_staging_mismatch();
    test_conditional_replace_rejects_case_alias_of_same_fat_path();
    test_conditional_delete_is_cas_and_idempotent();
    test_conditional_replace_requires_full_recovery_authority();
    test_conditional_delete_requires_full_recovery_authority();
    test_conditional_journal_promotion_failure_is_non_mutating();
    test_orphan_conditional_journal_staging_is_cleaned_on_recovery();
    test_truncated_conditional_journal_is_quarantined_once();
    test_bad_crc_conditional_journal_is_quarantined_once();
    test_conditional_replace_rejects_fat_short_name_alias_syntax();
    test_protocol_transaction_paths_are_reserved();
    test_endpoint_answers_only_filesystem_requests();
    test_endpoint_recursive_delete_is_hide_first_and_one_node_per_turn();
    test_endpoint_recursive_delete_deadline_preserves_recovery_marker();
    test_endpoint_advance_expires_abandoned_write_session();
    test_endpoint_retains_one_legacy_frame_and_rejects_excess();
    test_endpoint_playback_rejects_without_filesystem_work();
    test_endpoint_total_upload_deadline_is_not_refreshed_by_chunks();
    test_aged_autosave_aborts_upload_before_promotion();
    test_upload_size_limit_is_inclusive();
    test_endpoint_commit_yields_between_bounded_durable_phases();
    test_endpoint_conditional_replace_is_cooperative_and_playback_safe();
    test_endpoint_reaps_conditional_continuation_after_media_invalidation();
    test_endpoint_deadline_cancels_durable_conditional_into_recovery();

    resetTestRoot();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>
#include <oc/interface/ITransport.hpp>

#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/protocol/filesystem/FileSystemRpc.hpp"

namespace {

using core::persistence::ProductFileService;
using core::protocol::filesystem::FileSystemRpcCodec;
using core::protocol::filesystem::FILESYSTEM_RPC_FEATURE_CAPABILITIES;
using core::protocol::filesystem::FILESYSTEM_RPC_FEATURE_FILE_MANAGEMENT;
using core::protocol::filesystem::FILESYSTEM_RPC_FEATURE_WRITE_SESSIONS;
using core::protocol::filesystem::FILESYSTEM_RPC_MAX_CHUNK_SIZE;
using core::protocol::filesystem::FILESYSTEM_RPC_MAX_LIST_ENTRIES;
using core::protocol::filesystem::FILESYSTEM_RPC_RESPONSE_BUFFER_SIZE;
using core::protocol::filesystem::FILESYSTEM_RPC_SCHEMA;
using core::protocol::filesystem::FileSystemRpcFileType;
using core::protocol::filesystem::FileSystemRpcMessageId;
using core::protocol::filesystem::FileSystemRpcEndpoint;
using core::protocol::filesystem::FileSystemRpcHandler;
using core::protocol::filesystem::FileSystemRpcStatus;

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
    FileSystemRpcHandler handler;
    uint8_t request[1024] = {};
    uint8_t response[1024] = {};

    Harness()
        : filesystem(testRoot().string().c_str()),
          service(filesystem),
          handler(service, FileSystemRpcHandler::Config{100}) {
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

struct FaultInjectingFileSystem : oc::interface::IFileSystem {
    explicit FaultInjectingFileSystem(const char* rootPath)
        : delegate(rootPath) {}

    oc::type::Result<void> init() override { return delegate.init(); }
    bool available() const override { return delegate.available(); }
    oc::type::Result<oc::interface::FileInfo> stat(const char* path) override {
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
        return delegate.list(path, visitor, context);
    }
    oc::type::Result<void> createDirectory(const char* path) override {
        return delegate.createDirectory(path);
    }
    oc::type::Result<void> remove(
        const char* path,
        oc::interface::RemoveMode mode = oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
    ) override {
        return delegate.remove(path, mode);
    }
    oc::type::Result<void> rename(const char* fromPath, const char* toPath) override {
        return delegate.rename(fromPath, toPath);
    }
    oc::type::Result<size_t> read(
        const char* path,
        uint32_t offset,
        uint8_t* buffer,
        size_t size
    ) override {
        return delegate.read(path, offset, buffer, size);
    }
    oc::type::Result<size_t> write(
        const char* path,
        uint32_t offset,
        const uint8_t* data,
        size_t size
    ) override {
        return delegate.write(path, offset, data, size);
    }
    oc::type::Result<void> flush(const char* path) override {
        return delegate.flush(path);
    }
    oc::type::Result<void> beginWrite(const char* path, uint32_t expectedSize) override {
        return delegate.beginWrite(path, expectedSize);
    }
    oc::type::Result<size_t> appendWrite(const uint8_t* data, size_t size) override {
        if (!shortAppend || size == 0) {
            return delegate.appendWrite(data, size);
        }
        const size_t shortSize = size - 1U;
        auto written = delegate.appendWrite(data, shortSize);
        if (!written) {
            return written;
        }
        return oc::type::Result<size_t>::ok(shortSize);
    }
    oc::type::Result<void> finishWrite() override {
        return delegate.finishWrite();
    }
    void abortWrite() override {
        delegate.abortWrite();
    }

    oc::impl::HostFileSystem delegate;
    bool shortAppend = false;
    bool failFinalStat = false;
};

bool listContains(const core::protocol::filesystem::FileSystemRpcListResponse& response,
                  const char* name) {
    for (uint8_t i = 0; i < response.entryCount; ++i) {
        if (std::strcmp(response.entries[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

void test_stat_and_read_roundtrip() {
    resetTestRoot();
    Harness h;

    const uint8_t payload[] = {'p', 'r', 'o', 'j', 'e', 'c', 't'};
    auto written = h.service.write("projects/demo.bin", 0, payload, sizeof(payload));
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

    std::cout << "[PASS] test_capabilities_roundtrip\n";
}

void test_list_is_paginated_and_bounded() {
    resetTestRoot();
    Harness h;

    const uint8_t byte = 1;
    assert(h.service.write("projects/a.bin", 0, &byte, 1));
    assert(h.service.write("projects/b.bin", 0, &byte, 1));
    assert(h.service.write("projects/c.bin", 0, &byte, 1));

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
    FileSystemRpcHandler handler(service, FileSystemRpcHandler::Config{100});
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

void test_write_commit_propagates_final_stat_error() {
    resetTestRoot();

    FaultInjectingFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());
    FileSystemRpcHandler handler(service, FileSystemRpcHandler::Config{100});
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
    assert(h.service.write("projects/rpc-folder/source.bin", 0, payload, sizeof(payload)));

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

void test_endpoint_answers_only_filesystem_requests() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());

    const uint8_t payload[] = {'o', 'k'};
    assert(service.write("projects/endpoint.bin", 0, payload, sizeof(payload)));

    FakeTransport transport;
    FileSystemRpcEndpoint endpoint(transport, service, nowMs);
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
    transport.emit(request, requestSize);
    assert(transport.sendCount == 1);

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

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "FileSystemRpc tests\n";
    std::cout << "==============================================\n\n";

    test_stat_and_read_roundtrip();
    test_capabilities_roundtrip();
    test_list_is_paginated_and_bounded();
    test_write_session_commits_atomically();
    test_write_session_commits_empty_file();
    test_write_session_aborts_on_short_append();
    test_write_commit_propagates_final_stat_error();
    test_write_session_abort_and_timeout_cleanup();
    test_invalid_path_maps_to_error_status();
    test_read_error_response_is_decodable();
    test_file_management_operations();
    test_endpoint_answers_only_filesystem_requests();

    resetTestRoot();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <vector>
#include <oc/impl/HostFileSystem.hpp>
#include "protocol/filesystem/UnifiedFileTransfer.hpp"
#include "protocol/filesystem/FileSystemRpcInternal.hpp"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace core::protocol::filesystem::unified;
using core::protocol::filesystem::internal::ByteWriter;

struct FaultFileSystem : oc::impl::HostFileSystem {
    using oc::impl::HostFileSystem::HostFileSystem;
    bool failRemove = false;
    oc::type::Result<void> remove(const char* path, oc::interface::RemoveMode mode) override {
        if (failRemove) return oc::type::Result<void>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "injected cleanup failure"});
        return oc::impl::HostFileSystem::remove(path, mode);
    }
};

struct Harness {
    std::filesystem::path root;
    FaultFileSystem filesystem;
    core::persistence::ProductFileService files;
    FileTransfer transfer;
    std::array<uint8_t, HEADER + MAX_BODY> input{}, output{}, scratch{};
    uint32_t now = 0;
    explicit Harness(const std::filesystem::path& path)
        : root(path), filesystem(root.string().c_str()), files(filesystem), transfer(files) {
        assert(!std::filesystem::exists(root));
        std::filesystem::create_directories(root);
        assert(files.init());
        auto lease = files.acquireMutation(core::persistence::ProductMutationOwner::FILESYSTEM_RPC);
        assert(lease && files.ensureLayout(lease.value()));
        assert(files.releaseMutation(lease.value()));
    }
    Frame call(Operation operation, const std::vector<uint8_t>& body = {}, uint32_t nonce = 0,
               uint32_t identity = 0, uint32_t delay = 0, bool playing = false) {
        Frame request{operation, State::Request, 7, Error::None, nonce, identity, delay, body.data(), body.size()};
        assert(files.persistenceJobs().beginTurn(++now));
        const auto size = encode(request, input.data(), input.size()); assert(size);
        const auto count = transfer.process(input.data(), size, now, playing, output.data(), output.size());
        Frame response; assert(count && decode(output.data(), count, response));
        assert(response.requestId == 7 && response.operation == operation);
        if (response.state == State::Failed) std::cerr << "RPC op=" << unsigned(operation) << " error=" << unsigned(response.error) << '\n';
        return response;
    }
    void tick() { assert(files.persistenceJobs().beginTurn(++now)); transfer.advance(now, false, scratch.data(), scratch.size()); }
};

std::vector<uint8_t> beginBody(uint16_t session, const char* path, uint32_t size) {
    std::vector<uint8_t> body(210); ByteWriter w(body.data(), body.size());
    assert(w.writeU16(session) && w.writeU32(size) && w.writeString(path, 192));
    body.resize(w.position()); return body;
}
std::vector<uint8_t> chunkBody(uint16_t session, uint32_t offset, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> body(8 + data.size()); ByteWriter w(body.data(), body.size());
    assert(w.writeU16(session) && w.writeU32(offset) && w.writeU16(uint16_t(data.size())) && w.writeBytes(data.data(), data.size()));
    return body;
}

int main(int argc, char** argv) {
    const auto root = std::filesystem::temp_directory_path() / ("ms-core-unified-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Harness h(root);
    if (argc > 1 && std::string(argv[1]) == "--server") {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
        uint32_t size;
        while (std::cin.read(reinterpret_cast<char*>(&size), 4)) {
            assert(size <= h.input.size());
            std::cin.read(reinterpret_cast<char*>(h.input.data()), size);
            h.tick();
            assert(h.files.persistenceJobs().beginTurn(++h.now));
            const auto outputSize = uint32_t(h.transfer.process(h.input.data(), size, h.now, false, h.output.data(), h.output.size()));
            std::cout.write(reinterpret_cast<const char*>(&outputSize), 4);
            std::cout.write(reinterpret_cast<const char*>(h.output.data()), outputSize); std::cout.flush();
        }
        return 0;
    }
    auto response = h.call(Operation::Capabilities); assert(response.state == State::Complete && response.bodySize == 20);
    assert(h.call(Operation::UploadBegin, beginBody(7, "projects/rpc.bin", 5), 0, 0, 0, true).error == Error::BusyPlaying);
    assert(h.call(Operation::UploadBegin, beginBody(7, "../outside", 5)).error == Error::InvalidArgument);
    assert(h.call(Operation::UploadBegin, beginBody(7, "projects/rpc.bin", 5)).state == State::Complete);
    assert(h.call(Operation::UploadChunk, chunkBody(7, 1, {1})).error == Error::InvalidArgument);
    assert(h.call(Operation::UploadChunk, chunkBody(7, 0, {1, 2})).state == State::Complete);
    assert(h.call(Operation::UploadChunk, chunkBody(7, 2, {3, 4, 5})).state == State::Complete);
    response = h.call(Operation::UploadCommit, {7, 0}, 99, 0, 10000);
    assert(response.state == State::Pending && response.operationId != 0);
    const auto identity = response.operationId;
    // Treat the admission response as lost. Identical retry must not execute twice.
    response = h.call(Operation::UploadCommit, {7, 0}, 99, 0, 10000);
    assert(response.state == State::Pending && response.replayed && response.operationId == identity);
    assert(h.call(Operation::UploadCommit, {8, 0}, 99, 0, 10000).error == Error::Conflict);
    for (size_t i = 0; i < 100; ++i) {
        h.tick(); response = h.call(Operation::Poll, {}, 99, identity);
        if (response.state != State::Pending) break;
    }
    assert(response.state == State::Complete);
    response = h.call(Operation::UploadCommit, {7, 0}, 99, 0, 10000);
    assert(response.state == State::Complete && response.replayed && response.operationId == identity);
    std::vector<uint8_t> read(210); ByteWriter writer(read.data(), read.size());
    assert(writer.writeString("projects/rpc.bin", 192) && writer.writeU32(0) && writer.writeU16(5)); read.resize(writer.position());
    response = h.call(Operation::Read, read);
    assert(response.state == State::Complete && response.bodySize == 5);
    for (size_t i = 0; i < 5; ++i) assert(response.body[i] == i + 1);
    h.now += 30001;
    assert(h.call(Operation::Poll, {}, 99, identity).error == Error::ResultExpired);
    assert(h.call(Operation::UploadBegin, beginBody(8, "projects/next.bin", 1)).error == Error::ResourceExhausted);
    {
        Harness cancelled(root.string() + "-cancel");
        assert(cancelled.call(Operation::UploadBegin, beginBody(9, "projects/cancelled.bin", 0)).state == State::Complete);
        const auto admitted = cancelled.call(Operation::UploadCommit, {9, 0}, 101, 0, 10000);
        assert(admitted.state == State::Pending);
        const auto result = cancelled.call(Operation::Cancel, {}, 101, admitted.operationId);
        assert(result.state == State::Cancelled && result.error == Error::Cancelled);
        assert(!cancelled.files.writeSessionActive());
        assert(!cancelled.files.stat("projects/cancelled.bin"));
        assert(cancelled.files.persistenceJobs().activeJobId() == 0);
    }
    {
        Harness idle(root.string() + "-idle");
        assert(idle.call(Operation::UploadBegin, beginBody(11, "projects/idle.bin", 0)).state == State::Complete);
        idle.now += 10001;
        idle.tick(); // No subsequent request is required to release the abandoned upload.
        assert(!idle.files.writeSessionActive());
        assert(idle.files.persistenceJobs().activeJobId() == 0);
        assert(idle.call(Operation::UploadBegin, beginBody(12, "projects/retry.bin", 0)).state == State::Complete);
        idle.filesystem.failRemove = true;
        assert(idle.call(Operation::UploadAbort, {12, 0}).error == Error::StorageWriteFailed);
        assert(!idle.files.writeSessionActive());
        assert(idle.files.persistenceJobs().activeJobId() == 0);
        assert(idle.call(Operation::UploadBegin, beginBody(13, "projects/refused.bin", 0)).state == State::Failed);
    }
    {
        Harness deadline(root.string() + "-deadline");
        assert(deadline.call(Operation::UploadBegin, beginBody(10, "projects/deadline.bin", 0)).state == State::Complete);
        const auto admitted = deadline.call(Operation::UploadCommit, {10, 0}, 102, 0, 1);
        assert(admitted.state == State::Pending);
        deadline.tick();
        assert(deadline.call(Operation::Poll, {}, 102, admitted.operationId).error == Error::DeadlineExceeded);
        assert(!deadline.files.writeSessionActive());
        assert(!deadline.files.stat("projects/deadline.bin"));
    }
    std::cout << "Unified transfer: upload/commit/replay/read/expiry/cancel/deadline/idle cleanup/cleanup failure passed\n";
}

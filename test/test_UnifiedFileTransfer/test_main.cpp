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
    bool shortAppend = false, failTemporaryRead = false;
    size_t removes = 0, failedReads = 0;
    oc::type::Result<void> remove(const char* path, oc::interface::RemoveMode mode) override {
        ++removes;
        if (failRemove) return oc::type::Result<void>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "injected cleanup failure"});
        return oc::impl::HostFileSystem::remove(path, mode);
    }
    oc::type::Result<size_t> appendWrite(const uint8_t* data, size_t size) override {
        return oc::impl::HostFileSystem::appendWrite(data, shortAppend && size ? size - 1 : size);
    }
    oc::type::Result<size_t> read(const char* path, uint32_t offset, uint8_t* output, size_t size) override {
        if (failTemporaryRead && std::string(path).find("rpc-write-") != std::string::npos) {
            ++failedReads;
            return oc::type::Result<size_t>::err(
                {oc::type::ErrorCode::STORAGE_READ_FAILED, "injected integrity read failure"});
        }
        return oc::impl::HostFileSystem::read(path, offset, output, size);
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
    uint32_t start(const char* path, uint32_t expected) {
        std::vector<uint8_t> body(210); ByteWriter w(body.data(), body.size());
        assert(w.writeU32(expected) && w.writeString(path, 192)); body.resize(w.position());
        const auto response = call(Operation::UploadBegin, body);
        assert(response.state == State::Complete && response.bodySize == 4);
        const auto* b = response.body;
        const uint32_t id = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
        assert(id); return id;
    }
    void tick() { assert(files.persistenceJobs().beginTurn(++now)); transfer.advance(now, false, scratch.data(), scratch.size()); }
};

std::vector<uint8_t> beginBody(const char* path, uint32_t size) {
    std::vector<uint8_t> body(210); ByteWriter w(body.data(), body.size());
    assert(w.writeU32(size) && w.writeString(path, 192));
    body.resize(w.position()); return body;
}
std::vector<uint8_t> chunkBody(uint32_t session, uint32_t offset, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> body(10 + data.size()); ByteWriter w(body.data(), body.size());
    assert(w.writeU32(session) && w.writeU32(offset) && w.writeU16(uint16_t(data.size())) && w.writeBytes(data.data(), data.size()));
    return body;
}

std::vector<uint8_t> identityBody(uint32_t id) {
    return {uint8_t(id), uint8_t(id >> 8), uint8_t(id >> 16), uint8_t(id >> 24)};
}

Frame finish(Harness& h, uint32_t nonce, uint32_t id) {
    for (size_t i = 0; i < 200; ++i) {
        h.tick();
        const auto response = h.call(Operation::Poll, {}, nonce, id);
        if (response.state != State::Pending) return response;
    }
    assert(false && "commit did not terminate"); return {};
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
    assert(h.call(Operation::UploadBegin, beginBody("projects/rpc.bin", 5), 0, 0, 0, true).error == Error::BusyPlaying);
    assert(h.call(Operation::UploadBegin, beginBody("../outside", 5)).error == Error::InvalidArgument);
    assert(h.call(Operation::UploadBegin, beginBody("", 0)).error == Error::InvalidArgument);
    auto embeddedNul = beginBody("projects/hidden.bin", 0);
    embeddedNul[5 + 9] = 0;
    assert(h.call(Operation::UploadBegin, embeddedNul).error == Error::InvalidArgument);
    assert(!h.files.writeSessionActive());
    const auto session = h.start("projects/rpc.bin", 5);
    assert(h.call(Operation::UploadChunk, chunkBody(session, 1, {1})).error == Error::InvalidArgument);
    assert(h.call(Operation::UploadChunk, chunkBody(session, 0, {1, 2})).state == State::Complete);
    assert(h.call(Operation::UploadChunk, chunkBody(session, 2, {3, 4, 5})).state == State::Complete);
    response = h.call(Operation::UploadCommit, identityBody(session), 99, 0, 10000);
    assert(response.state == State::Pending && response.operationId != 0);
    const auto identity = response.operationId;
    // Treat the admission response as lost. Identical retry must not execute twice.
    response = h.call(Operation::UploadCommit, identityBody(session), 99, 0, 10000);
    assert(response.state == State::Pending && response.replayed && response.operationId == identity);
    assert(h.call(Operation::UploadCommit, identityBody(session + 1), 99, 0, 10000).error == Error::Conflict);
    response = finish(h, 99, identity);
    assert(response.state == State::Complete);
    response = h.call(Operation::UploadCommit, identityBody(session), 99, 0, 10000);
    assert(response.state == State::Complete && response.replayed && response.operationId == identity);
    std::vector<uint8_t> read(210); ByteWriter writer(read.data(), read.size());
    assert(writer.writeString("projects/rpc.bin", 192) && writer.writeU32(0) && writer.writeU16(5)); read.resize(writer.position());
    response = h.call(Operation::Read, read);
    assert(response.state == State::Complete && response.bodySize == 5);
    for (size_t i = 0; i < 5; ++i) assert(response.body[i] == i + 1);
    h.now += 30001;
    assert(h.call(Operation::Poll, {}, 99, identity).error == Error::ResultExpired);
    const auto next = h.start("projects/next.bin", 1);
    assert(next != session);
    assert(h.call(Operation::UploadChunk, chunkBody(session, 0, {9})).error == Error::PreconditionFailed);
    assert(h.call(Operation::UploadAbort, identityBody(session)).error == Error::PreconditionFailed);
    assert(h.call(Operation::UploadCommit, identityBody(session), 99, 0, 10000).error == Error::PreconditionFailed);
    assert(h.files.writeSessionActive());
    assert(h.call(Operation::UploadAbort, identityBody(next)).state == State::Complete);
    {
        Harness cancelled(root.string() + "-cancel");
        const auto id = cancelled.start("projects/cancelled.bin", 0);
        const auto admitted = cancelled.call(Operation::UploadCommit, identityBody(id), 101, 0, 10000);
        assert(admitted.state == State::Pending);
        const auto result = cancelled.call(Operation::Cancel, {}, 101, admitted.operationId);
        assert(result.state == State::Cancelled && result.error == Error::Cancelled);
        assert(!cancelled.files.writeSessionActive());
        assert(!cancelled.files.stat("projects/cancelled.bin"));
        assert(cancelled.files.persistenceJobs().activeJobId() == 0);
    }
    {
        Harness idle(root.string() + "-idle");
        (void)idle.start("projects/idle.bin", 0);
        idle.now += 10001;
        idle.tick(); // No subsequent request is required to release the abandoned upload.
        assert(!idle.files.writeSessionActive());
        assert(idle.files.persistenceJobs().activeJobId() == 0);
        const auto id = idle.start("projects/retry.bin", 0);
        idle.filesystem.failRemove = true;
        assert(idle.call(Operation::UploadAbort, identityBody(id)).error == Error::StorageWriteFailed);
        assert(!idle.files.writeSessionActive());
        assert(idle.files.persistenceJobs().activeJobId() == 0);
        assert(idle.call(Operation::UploadBegin, beginBody("projects/refused.bin", 0)).state == State::Failed);
    }
    {
        Harness deadline(root.string() + "-deadline");
        const auto id = deadline.start("projects/deadline.bin", 0);
        const auto admitted = deadline.call(Operation::UploadCommit, identityBody(id), 102, 0, 1);
        assert(admitted.state == State::Pending);
        deadline.tick();
        assert(deadline.call(Operation::Poll, {}, 102, admitted.operationId).error == Error::DeadlineExceeded);
        assert(!deadline.files.writeSessionActive());
        assert(!deadline.files.stat("projects/deadline.bin"));
    }
    {
        Harness shortWrite(root.string() + "-short-write");
        const auto id = shortWrite.start("projects/short.bin", 2);
        shortWrite.filesystem.shortAppend = true;
        const auto removes = shortWrite.filesystem.removes;
        assert(shortWrite.call(Operation::UploadChunk, chunkBody(id, 0, {1, 2})).error == Error::StorageWriteFailed);
        assert(shortWrite.filesystem.removes == removes);
        assert(shortWrite.files.writeSessionActive());
        assert(shortWrite.call(Operation::UploadCommit, identityBody(id), 500, 0, 10000).error == Error::PreconditionFailed);
        shortWrite.tick(); // Unwind has a separate promotion quota, not the chunk's single-I/O quota.
        assert(shortWrite.filesystem.removes == removes + 1);
        assert(!shortWrite.files.writeSessionActive());
        assert(shortWrite.files.persistenceJobs().activeJobId() == 0);
        assert(!shortWrite.files.stat("projects/short.bin"));
    }
    {
        Harness readFailure(root.string() + "-integrity-read");
        const auto id = readFailure.start("projects/read-failure.bin", 1);
        assert(readFailure.call(Operation::UploadChunk, chunkBody(id, 0, {1})).state == State::Complete);
        assert(readFailure.call(Operation::UploadCommit, identityBody(id), 501, 0, 10000).state == State::Pending);
        readFailure.filesystem.failTemporaryRead = true;
        const auto removes = readFailure.filesystem.removes;
        for (size_t i = 0; i < 20 && !readFailure.filesystem.failedReads; ++i) readFailure.tick();
        assert(readFailure.filesystem.failedReads == 1 && readFailure.filesystem.removes == removes);
        readFailure.tick();
        const auto result = readFailure.call(Operation::Poll, {}, 501, id);
        assert(result.state == State::Failed && result.error == Error::StorageReadFailed);
        assert(readFailure.filesystem.removes == removes + 1);
        assert(readFailure.files.persistenceJobs().activeJobId() == 0);
        assert(!readFailure.files.stat("projects/read-failure.bin"));
    }
    {
        Harness media(root.string() + "-media");
        const auto id = media.start("projects/media.bin", 0);
        assert(media.call(Operation::UploadCommit, identityBody(id), 502, 0, 10000).state == State::Pending);
        media.files.markMediaUnavailable();
        media.tick();
        assert(media.call(Operation::Poll, {}, 502, id).error == Error::MediaChanged);
        assert(media.files.persistenceJobs().activeJobId() == 0);
        assert(!media.files.writeSessionActive());
    }
    {
        Harness rollover(root.string() + "-rollover");
        rollover.now = UINT32_MAX - 100;
        const auto id = rollover.start("projects/rollover.bin", 0);
        assert(rollover.call(Operation::UploadCommit, identityBody(id), 503, 0, 10000).state == State::Pending);
        assert(finish(rollover, 503, id).state == State::Complete);
        rollover.now += 200;
        assert(rollover.call(Operation::Poll, {}, 503, id).state == State::Complete);
        rollover.now += FileTransfer::RETENTION_MS;
        assert(rollover.call(Operation::Poll, {}, 503, id).error == Error::ResultExpired);
    }
    {
        Harness paused(root.string() + "-paused");
        const auto id = paused.start("projects/paused.bin", 0);
        assert(paused.call(Operation::UploadCommit, identityBody(id), 504, 0, 10000).state == State::Pending);
        paused.now += FileTransfer::RETENTION_MS + 1;
        assert(paused.files.persistenceJobs().beginTurn(++paused.now));
        paused.transfer.advance(paused.now, true, paused.scratch.data(), paused.scratch.size());
        assert(paused.call(Operation::Poll, {}, 504, id, 0, true).state == State::Pending);
        paused.tick();
        assert(paused.call(Operation::Poll, {}, 504, id).error == Error::DeadlineExceeded);
    }
    {
        Harness autosave(root.string() + "-autosave");
        (void)autosave.start("projects/autosave.bin", 0);
        auto admitted = autosave.files.persistenceJobs().admit({
            core::persistence::ProductPersistenceJobOwner::PROJECT_AUTOSAVE, autosave.now, 0,
            core::persistence::PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO});
        assert(admitted);
        auto token = std::move(admitted.value());
        autosave.now += core::persistence::PRODUCT_PERSISTENCE_AUTOSAVE_MAX_DEFERRAL_MS + 1;
        autosave.tick();
        assert(!autosave.files.writeSessionActive());
        assert(autosave.files.persistenceJobs().isActive(token));
        assert(autosave.files.persistenceJobs().cancel(token));
    }
    {
        Harness registry(root.string() + "-registry");
        std::array<uint32_t, FileTransfer::RETAINED_CAPACITY> ids{};
        for (uint32_t i = 0; i < ids.size(); ++i) {
            ids[i] = registry.start("projects/repeated.bin", 0);
            assert(registry.call(Operation::UploadCommit, identityBody(ids[i]), 1000 + i, 0, 10000).state == State::Pending);
            // A second nonce cannot replace the active continuation.
            assert(registry.call(Operation::UploadCommit, identityBody(ids[i]), 2000 + i, 0, 10000).error == Error::PreconditionFailed);
            assert(finish(registry, 1000 + i, ids[i]).state == State::Complete);
            assert(registry.files.persistenceJobs().activeJobId() == 0);
        }
        assert(registry.call(Operation::UploadBegin, beginBody("projects/full.bin", 0)).error == Error::ResourceExhausted);
        assert(!registry.files.writeSessionActive());
        // Saturation preserved every unexpired result.
        for (uint32_t i = 0; i < ids.size(); ++i) {
            const auto result = registry.call(Operation::UploadCommit, identityBody(ids[i]), 1000 + i, 0, 10000);
            assert(result.state == State::Complete && result.replayed && result.operationId == ids[i]);
        }
        registry.now += FileTransfer::RETENTION_MS;
        registry.tick();
        assert(registry.call(Operation::Poll, {}, 1000, ids[0]).error == Error::ResultExpired);
        const auto id = registry.start("projects/reclaimed.bin", 0);
        for (auto old : ids) assert(id != old);
        assert(registry.call(Operation::UploadCommit, identityBody(id), 3000, 0, 10000).state == State::Pending);
        assert(finish(registry, 3000, id).state == State::Complete);
    }
    std::cout << "Unified transfer: upload/commit/replay/read/expiry/cancel/deadline/cleanup/32 retained results/saturation/reclamation/stale identities passed\n";
}

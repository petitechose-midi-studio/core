#include <array>
#include <algorithm>
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
    size_t lists = 0;
    bool failList = false;
    size_t creates = 0, renames = 0;
    bool failRename = false;
    oc::type::Result<void> createDirectory(const char* path) override {
        ++creates;
        return oc::impl::HostFileSystem::createDirectory(path);
    }
    oc::type::Result<void> rename(const char* from, const char* to) override {
        ++renames;
        if (failRename) return oc::type::Result<void>::err(
            {oc::type::ErrorCode::STORAGE_WRITE_FAILED, "injected rename failure"});
        return oc::impl::HostFileSystem::rename(from, to);
    }
    oc::type::Result<void> list(const char* path, oc::interface::DirectoryEntryVisitor visitor, void* context) override {
        ++lists;
        if (failList) return oc::type::Result<void>::err(
            {oc::type::ErrorCode::STORAGE_READ_FAILED, "injected list failure"});
        return oc::impl::HostFileSystem::list(path, visitor, context);
    }
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
    core::persistence::ProductDirectoryCatalog catalog;
    FileTransfer transfer;
    std::array<uint8_t, HEADER + MAX_BODY> input{}, output{}, scratch{};
    uint32_t now = 0;
    explicit Harness(const std::filesystem::path& path)
        : root(path), filesystem(root.string().c_str()), files(filesystem), catalog(files), transfer(files, catalog) {
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

std::vector<uint8_t> listBody(const char* path, uint16_t start = 0, uint8_t limit = 8, uint32_t snapshot = 0) {
    std::vector<uint8_t> body(210); ByteWriter w(body.data(), body.size());
    assert(w.writeString(path, 192) && w.writeU16(start) && w.writeU8(limit) && w.writeU32(snapshot));
    body.resize(w.position()); return body;
}

void testListing(const std::filesystem::path& root) {
    Harness h(root);
    using Reader = core::protocol::filesystem::internal::ByteReader;
    auto lease = h.files.acquireMutation(core::persistence::ProductMutationOwner::FILESYSTEM_RPC);
    assert(lease);
    for (size_t i = 0; i < 256; ++i) {
        const auto path = "projects/entry-" + std::to_string(i);
        assert(h.files.beginWrite(lease.value(), path.c_str(), 0) && h.files.finishWrite(lease.value()));
    }
    assert(h.files.releaseMutation(lease.value()));
    uint32_t snapshot = 0; std::vector<std::string> names;
    for (uint16_t start = 0; start < 256; start += 8) {
        const auto page = h.call(Operation::List, listBody("projects", start, 8, snapshot));
        assert(page.state == State::Complete);
        Reader reader(page.body, page.bodySize);
        uint32_t id; uint16_t index; uint8_t count; bool more;
        assert(reader.readU32(id) && id && (!snapshot || id == snapshot)); snapshot = id;
        assert(reader.readU16(index) && index == start && reader.readU8(count) && count == 8);
        assert(reader.readBool(more) && more == (start + count < 256));
        for (uint8_t i = 0; i < count; ++i) {
            char name[65]; uint8_t type; uint32_t size; bool truncated;
            assert(reader.readString(name, sizeof(name), 64) && reader.readU8(type) && type == 1);
            assert(reader.readU32(size) && size == 0 && reader.readBool(truncated) && !truncated);
            assert(std::find(names.begin(), names.end(), name) == names.end()); names.emplace_back(name);
        }
        assert(!reader.remaining());
    }
    assert(names.size() == 256 && h.filesystem.lists == 1);
    assert(h.call(Operation::List, listBody("projects", 257, 8, snapshot)).error == Error::InvalidArgument);
    assert(h.call(Operation::List, listBody("projects", 1)).error == Error::InvalidArgument);
    assert(h.call(Operation::List, listBody("projects", 0, 0)).error == Error::InvalidArgument);
    assert(h.call(Operation::List, listBody("projects", 0, 9)).error == Error::InvalidArgument);
    auto malformed = listBody("projects"); malformed.push_back(0);
    assert(h.call(Operation::List, malformed).error == Error::InvalidArgument);
    assert(h.call(Operation::List, listBody("projects"), 0, 0, 0, true).error == Error::BusyPlaying);
    assert(h.filesystem.lists == 1);
    // A shared catalog displaced and then rebuilt for the same path is a different snapshot.
    const auto empty = h.call(Operation::List, listBody("tmp"));
    assert(empty.state == State::Complete && empty.bodySize == 8 && empty.body[6] == 0 && empty.body[7] == 0);
    assert(h.call(Operation::List, listBody("projects", 8, 8, snapshot)).error == Error::Conflict);
    assert(h.call(Operation::List, listBody("projects")).state == State::Complete);
    assert(h.call(Operation::List, listBody("projects", 8, 8, snapshot)).error == Error::Conflict);
    snapshot = h.catalog.rawSnapshotId("projects");
    lease = h.files.acquireMutation(core::persistence::ProductMutationOwner::FILESYSTEM_RPC);
    assert(lease && h.files.beginWrite(lease.value(), "projects/overflow", 0) && h.files.finishWrite(lease.value()));
    assert(h.files.releaseMutation(lease.value()));
    assert(h.call(Operation::List, listBody("projects", 8, 8, snapshot)).error == Error::Conflict);
    assert(h.call(Operation::List, listBody("projects")).error == Error::ResourceExhausted);
    h.filesystem.failList = true;
    assert(h.call(Operation::List, listBody("tmp")).error == Error::StorageReadFailed);
    h.filesystem.failList = false;
    assert(h.call(Operation::List, listBody("tmp")).state == State::Complete);
    std::cout << "List: 256 entries in 32 pages, one scan, invalidation/conflict, overflow, error recovery passed\n";
}

std::vector<uint8_t> mutationBody(const char* path, const char* destination = nullptr, int recursive = -1) {
    std::vector<uint8_t> body(386); ByteWriter w(body.data(), body.size());
    assert(w.writeString(path, 192));
    if (destination) assert(w.writeString(destination, 192));
    if (recursive >= 0) assert(w.writeU8(uint8_t(recursive)));
    body.resize(w.position()); return body;
}

void testMutations(const std::filesystem::path& root) {
    Harness h(root);
    const auto mkdir = mutationBody("projects/folder");
    const auto creates = h.filesystem.creates;
    auto result = h.call(Operation::Mkdir, mkdir, 601, 0, 10000);
    assert(result.state == State::Complete && result.operationId);
    const auto id = result.operationId;
    assert(h.filesystem.creates == creates + 1);
    assert(h.call(Operation::Mkdir, mkdir, 601, 0, 10000).replayed);
    assert(h.filesystem.creates == creates + 1);
    assert(h.call(Operation::Delete, mutationBody("projects/folder", nullptr, 0), 601, 0, 10000).error == Error::Conflict);
    assert(h.call(Operation::Mkdir, mutationBody("projects/other"), 601, 0, 10000).error == Error::Conflict);
    assert(h.call(Operation::Mkdir, mkdir, 601, 0, 9999).error == Error::Conflict);
    assert(h.call(Operation::Poll, {}, 601, id).state == State::Complete);
    const auto rename = mutationBody("projects/folder", "projects/renamed");
    const auto renames = h.filesystem.renames;
    assert(h.call(Operation::Rename, rename, 602, 0, 10000).state == State::Complete);
    assert(h.call(Operation::Rename, rename, 602, 0, 10000).replayed);
    assert(h.filesystem.renames == renames + 1);
    assert(!h.files.stat("projects/folder") && h.files.stat("projects/renamed"));
    const auto remove = mutationBody("projects/renamed", nullptr, 0);
    const auto removes = h.filesystem.removes;
    assert(h.call(Operation::Delete, remove, 603, 0, 10000).state == State::Complete);
    assert(h.call(Operation::Delete, remove, 603, 0, 10000).replayed);
    assert(h.filesystem.removes == removes + 1);
    assert(h.call(Operation::Delete, mutationBody("projects/x", nullptr, 2), 604, 0, 10000).error == Error::InvalidArgument);
    assert(h.call(Operation::Mkdir, mutationBody("tmp/rpc-forbidden"), 604, 0, 10000).error == Error::InvalidArgument);
    assert(h.call(Operation::Rename, mutationBody("projects/x", "../escape"), 604, 0, 10000).error == Error::InvalidArgument);
    assert(h.call(Operation::Mkdir, mkdir, 604, 0, 10000, true).error == Error::BusyPlaying);
    const auto upload = h.start("projects/staging.bin", 0);
    assert(h.call(Operation::Mkdir, mkdir, 604, 0, 10000).error == Error::ResourceExhausted);
    assert(h.call(Operation::UploadAbort, identityBody(upload)).state == State::Complete);
    assert(h.call(Operation::Mkdir, mkdir, 604, 0, 10000).state == State::Complete);
    h.filesystem.failRename = true;
    result = h.call(Operation::Rename, rename, 605, 0, 10000);
    assert(result.error == Error::StorageWriteFailed && result.operationId);
    h.filesystem.failRename = false;
    assert(h.call(Operation::Rename, rename, 605, 0, 10000).error == Error::StorageWriteFailed);
    assert(h.files.stat("projects/folder"));
    assert(h.files.persistenceJobs().activeJobId() == 0);
    std::cout << "Mutations: mkdir/rename/delete, cross-operation nonce conflict, exact replay, failure retention, upload exclusion passed\n";
}

void makeTree(Harness& h) {
    auto lease = h.files.acquireMutation(core::persistence::ProductMutationOwner::FILESYSTEM_RPC);
    assert(lease && h.files.createDirectory(lease.value(), "projects/tree/child"));
    assert(h.files.beginWrite(lease.value(), "projects/tree/child/file", 0) && h.files.finishWrite(lease.value()));
    assert(h.files.releaseMutation(lease.value()));
}

void testTreeMutation(const std::filesystem::path& root) {
    const auto body = mutationBody("projects/tree", nullptr, 1);
    Harness h(root); makeTree(h);
    auto result = h.call(Operation::Delete, body, 701, 0, 10000);
    assert(result.state == State::Pending); auto id = result.operationId;
    assert(h.call(Operation::Delete, body, 701, 0, 10000).replayed);
    assert(h.call(Operation::Cancel, {}, 701, id).state == State::Cancelled);
    assert(h.files.stat("projects/tree/child/file"));
    result = h.call(Operation::Delete, body, 702, 0, 1); id = result.operationId;
    h.tick();
    assert(h.call(Operation::Poll, {}, 702, id).error == Error::DeadlineExceeded);
    assert(h.files.stat("projects/tree/child/file"));
    result = h.call(Operation::Delete, body, 703, 0, 10000); id = result.operationId;
    assert(h.call(Operation::UploadBegin, beginBody("projects/blocked", 0)).error == Error::ResourceExhausted);
    h.tick(); h.tick();
    assert(h.call(Operation::Cancel, {}, 703, id).error == Error::CancelTooLate);
    assert(finish(h, 703, id).state == State::Complete);
    assert(!h.files.stat("projects/tree"));
    assert(h.call(Operation::Delete, body, 703, 0, 10000).replayed);
    assert(h.files.persistenceJobs().activeJobId() == 0);
    // Storage failure after the hide keeps the recovery marker and blocks writes.
    Harness failed(root.string() + "-failure"); makeTree(failed);
    result = failed.call(Operation::Delete, body, 704, 0, 10000); id = result.operationId;
    failed.filesystem.failRemove = true;
    assert(finish(failed, 704, id).error == Error::StorageWriteFailed);
    assert(failed.files.persistenceJobs().activeJobId() == 0);
    assert(failed.call(Operation::Mkdir, mutationBody("projects/nope"), 705, 0, 10000).error == Error::ResourceExhausted);
    failed.filesystem.failRemove = false;
    auto recoveryLease = failed.files.beginRecovery(); assert(recoveryLease);
    core::persistence::ProductTreeCleanupPlan recovery; recovery.beginRecovery();
    for (unsigned step = 0; step < 100 && !recovery.terminal(); ++step)
        recovery.advanceRecovery(failed.files, recoveryLease.value());
    assert(recovery.completed() && failed.files.completeRecovery(recoveryLease.value(), true));
    assert(failed.call(Operation::Mkdir, mutationBody("projects/recovered"), 706, 0, 10000).state == State::Complete);
    Harness media(root.string() + "-media"); makeTree(media);
    result = media.call(Operation::Delete, body, 707, 0, 10000); id = result.operationId;
    media.files.markMediaUnavailable(); media.tick();
    assert(media.call(Operation::Poll, {}, 707, id).error == Error::MediaChanged);
    assert(media.files.persistenceJobs().activeJobId() == 0);
    std::cout << "Recursive delete: cancellation/deadline before hide, too-late refusal, retained completion, failure/recovery/media passed\n";
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
            // Logical foreground scheduling, independent of one RPC poll per step.
            // Eight admitted turns per exchange keep large-file integrity work
            // from being artificially limited by Manager's wall-clock poll sleep.
            // This is an interoperability oracle, not a firmware timing model.
            for (unsigned turn = 0; turn < 8; ++turn) h.tick();
            assert(h.files.persistenceJobs().beginTurn(++h.now));
            const auto outputSize = uint32_t(h.transfer.process(h.input.data(), size, h.now, false, h.output.data(), h.output.size()));
            std::cout.write(reinterpret_cast<const char*>(&outputSize), 4);
            std::cout.write(reinterpret_cast<const char*>(h.output.data()), outputSize); std::cout.flush();
        }
        return 0;
    }
    testListing(root.string() + "-list");
    testMutations(root.string() + "-mutations");
    testTreeMutation(root.string() + "-tree");
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

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <oc/impl/HostFileSystem.hpp>
#include "protocol/filesystem/UnifiedFileTransfer.hpp"
#include "protocol/filesystem/RpcBody.hpp"
#include "persistence/ProductFileRecoveryPlan.hpp"
#include "persistence/ProductConditionalMutationDigest.hpp"

namespace p = core::persistence;
namespace c = p::conditional_mutation;
using namespace core::protocol::filesystem::unified;

// Freeze all fallible backend calls at a selected boundary. The after case
// models a primitive whose effect persisted but whose acknowledgement was lost.
class CutFileSystem : public oc::impl::HostFileSystem {
public:
    using HostFileSystem::HostFileSystem;
    unsigned count = 0, target = 0;
    bool after = false, cut = false;
    template<class F> auto run(F fn) -> decltype(fn()) {
        using Result = decltype(fn());
        if (cut) return Result::err({oc::type::ErrorCode::STORAGE_WRITE_FAILED, "power cut"});
        ++count;
        if (target == count && !after) { cut = true; return Result::err({oc::type::ErrorCode::STORAGE_WRITE_FAILED, "before cut"}); }
        auto result = fn();
        if (target == count && after) { cut = true; return Result::err({oc::type::ErrorCode::STORAGE_WRITE_FAILED, "after cut"}); }
        return result;
    }
    oc::type::Result<oc::interface::FileInfo> stat(const char* path) override {
        return run([&] { return HostFileSystem::stat(path); });
    }
    oc::type::Result<size_t> read(const char* path, uint32_t offset, uint8_t* data, size_t size) override {
        return run([&] { return HostFileSystem::read(path, offset, data, size); });
    }
    oc::type::Result<size_t> write(const char* path, uint32_t offset, const uint8_t* data, size_t size) override {
        return run([&] { return HostFileSystem::write(path, offset, data, size); });
    }
    oc::type::Result<void> rename(const char* from, const char* to) override {
        return run([&] { return HostFileSystem::rename(from, to); });
    }
    oc::type::Result<void> remove(const char* path, oc::interface::RemoveMode mode) override {
        return run([&] { return HostFileSystem::remove(path, mode); });
    }
    oc::type::Result<void> createDirectory(const char* path) override {
        return run([&] { return HostFileSystem::createDirectory(path); });
    }
    oc::type::Result<void> list(const char* path, oc::interface::DirectoryEntryVisitor visitor, void* context) override {
        return run([&] { return HostFileSystem::list(path, visitor, context); });
    }
    oc::type::Result<void> flush(const char* path) override {
        return run([&] { return HostFileSystem::flush(path); });
    }
    oc::type::Result<void> beginWrite(const char* path, uint32_t size) override {
        return run([&] { return HostFileSystem::beginWrite(path, size); });
    }
    oc::type::Result<size_t> appendWrite(const uint8_t* data, size_t size) override {
        return run([&] { return HostFileSystem::appendWrite(data, size); });
    }
    oc::type::Result<void> finishWrite() override {
        return run([&] { return HostFileSystem::finishWrite(); });
    }
};

void recover(p::ProductFileService& files) {
    auto acquired = files.beginRecovery(); assert(acquired);
    auto lease = std::move(acquired.value());
    p::ProductFileRecoveryPlan ordinary;
    std::array<uint8_t, 30720> scratch{};
    assert(ordinary.begin(files, lease));
    for (unsigned i = 0; ordinary.active() && i < 500; ++i)
        assert(ordinary.advance(files, lease, scratch.data(), scratch.size()));
    assert(ordinary.complete());
    c::Journal journal; bool present = false, corrupt = false;
    const auto loaded = c::readJournal(files, lease, journal, present, corrupt);
    if (loaded != c::Status::OK) {
        assert(corrupt && c::quarantineCorruptJournal(files, lease) == c::Status::OK);
        assert(c::removeIfExists(files, lease, c::JOURNAL_STAGING_PATH) == c::Status::OK);
    } else {
        assert(c::removeIfExists(files, lease, c::JOURNAL_STAGING_PATH) == c::Status::OK);
        if (present) {
            c::ConditionalMutationPlan plan;
            assert(plan.beginRecovery(files, lease, journal));
            for (unsigned i = 0; plan.active() && i < 500; ++i)
                (void)plan.advance(files, scratch.data(), scratch.size());
            assert(plan.terminal() && plan.status() == c::Status::OK);
        }
    }
    assert(files.completeRecovery(lease, true));
}

void put(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary); assert(out);
    out.write(reinterpret_cast<const char*>(data.data()), data.size()); assert(out);
}
std::vector<uint8_t> get(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary); assert(in);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

unsigned campaign(const std::filesystem::path& root, bool replacing, unsigned target, bool after) {
    std::filesystem::create_directories(root / "midi-studio/projects");
    std::filesystem::create_directories(root / "midi-studio/tmp");
    const auto current = root / "midi-studio/projects/current";
    const auto stage = root / "midi-studio/tmp/stage";
    const std::vector<uint8_t> old(777, 0x31), next(1549, 0xa7), neighbor{1, 3, 5, 7};
    put(current, old); put(stage, next); put(root / "midi-studio/projects/neighbor", neighbor);
    unsigned boundaries = 0;
    State terminal = State::Request;
    {
        CutFileSystem fs(root.string().c_str());
        p::ProductFileService files(fs); assert(files.init());
        p::ProductDirectoryCatalog catalog(files);
        FileTransfer service(files, catalog, 42);
        std::array<uint8_t, HEADER + MAX_BODY> input{}, output{}, scratch{};
        uint8_t body[450], oldHash[32], nextHash[32];
        assert(c::hashBytes(old.data(), old.size(), oldHash));
        assert(c::hashBytes(next.data(), next.size(), nextHash));
        ByteWriter writer(body, sizeof(body));
        assert(writer.writeBytes(oldHash, 32));
        if (replacing) assert(writer.writeBytes(nextHash, 32));
        assert(writer.writeString("projects/current", 192));
        if (replacing) assert(writer.writeString("tmp/stage", 192));
        Frame request{replacing ? Operation::ConditionalReplace : Operation::ConditionalDelete,
            State::Request, 1, Error::None, 99, 0, 10000, body, writer.position(), false, 42};
        fs.count = 0; fs.target = target; fs.after = after;
        const auto size = encode(request, input.data(), input.size()); assert(size);
        assert(files.persistenceJobs().beginTurn(1));
        auto length = service.process(input.data(), size, 1, false, output.data(), output.size());
        Frame result; assert(decode(output.data(), length, result));
        const auto identity = result.operationId;
        for (unsigned turn = 2; result.state == State::Pending && turn < 500; ++turn) {
            assert(files.persistenceJobs().beginTurn(turn));
            service.advance(turn, false, scratch.data(), scratch.size());
            Frame poll{Operation::Poll, State::Request, 2, Error::None, 99, identity};
            poll.lifetime = 42;
            const auto pollSize = encode(poll, input.data(), input.size()); assert(pollSize);
            length = service.process(input.data(), pollSize, turn, false, output.data(), output.size());
            assert(decode(output.data(), length, result));
        }
        assert(result.state == State::Complete || result.state == State::Failed);
        terminal = result.state; boundaries = fs.count;
        if (target) assert(fs.cut);
    }
    // A new service and backend model restart; no in-memory retained result is reused.
    oc::impl::HostFileSystem backend(root.string().c_str());
    p::ProductFileService restarted(backend); assert(restarted.init());
    recover(restarted);
    const bool exists = std::filesystem::exists(current);
    if (replacing) { assert(exists); const auto content = get(current); assert(content == old || content == next); }
    else if (exists) assert(get(current) == old);
    if (terminal == State::Complete) { if (replacing) assert(get(current) == next); else assert(!exists); }
    assert(get(root / "midi-studio/projects/neighbor") == neighbor);
    const auto first = exists ? get(current) : std::vector<uint8_t>{};
    recover(restarted);
    assert(std::filesystem::exists(current) == exists);
    if (exists) assert(get(current) == first);
    assert(!std::filesystem::exists(root / "midi-studio/tmp/rpc-conditional.journal"));
    return boundaries;
}

void corruptJournals(const std::filesystem::path& root) {
    for (unsigned mode = 0; mode < 3; ++mode) {
        const auto path = root / std::to_string(mode);
        std::filesystem::create_directories(path / "midi-studio/tmp");
        std::filesystem::create_directories(path / "midi-studio/projects");
        const auto journalPath = path / "midi-studio/tmp/rpc-conditional.journal";
        const auto quarantine = path / "midi-studio/tmp/rpc-conditional.journal.corrupt";
        const auto stage = path / "midi-studio/tmp/rpc-conditional.journal.tmp";
        const auto current = path / "midi-studio/projects/current";
        const auto backup = path / "midi-studio/tmp/rpc-conditional.backup";
        put(current, {1, 2}); put(backup, {3, 4}); put(stage, {'F', 'S', 'T', 'X'});
        oc::impl::HostFileSystem backend(path.string().c_str());
        p::ProductFileService files(backend); assert(files.init());
        std::vector<uint8_t> broken{'F', 'S', 'T', 'X'};
        if (mode == 1) {
            auto acquired = files.acquireMutation(p::ProductMutationOwner::FILESYSTEM_RPC); assert(acquired);
            c::Journal journal; journal.kind = c::Kind::DELETE; journal.operationId = 9;
            std::strcpy(journal.currentPath, "projects/current");
            assert(c::writeJournal(files, acquired.value(), journal) == c::Status::OK);
            assert(files.releaseMutation(acquired.value()));
            broken = get(journalPath); broken.back() ^= 0x80;
        }
        if (mode < 2) { put(journalPath, broken); put(quarantine, {0x99}); }
        recover(files);
        assert(!std::filesystem::exists(journalPath) && !std::filesystem::exists(stage));
        assert(get(current) == std::vector<uint8_t>({1, 2}));
        assert(get(backup) == std::vector<uint8_t>({3, 4}));
        if (mode < 2) assert(get(quarantine) == broken);
        recover(files);
        if (mode < 2) assert(get(quarantine) == broken);
    }
    std::cout << "Corrupt journals: truncated, invalid CRC, orphan staging and stable quarantine passed\n";
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / ("unified-recovery-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    corruptJournals(root / "corrupt");
    for (bool replace : {false, true}) {
        const auto name = replace ? "replace" : "delete";
        const auto count = campaign(root / (std::string(name) + "-baseline"), replace, 0, false);
        for (bool after : {false, true}) for (unsigned cut = 1; cut <= count; ++cut) {
            std::cerr << name << " cut=" << cut << " after=" << after << '\n';
            campaign(root / (std::string(name) + "-" + std::to_string(cut) + "-" + std::to_string(after)), replace, cut, after);
        }
        std::cout << name << ": " << count << " backend boundaries, before/after cuts, exact canonical content and repeat recovery passed\n";
    }
}

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <oc/impl/HostFileSystem.hpp>
#include "validation/benchmark/BenchFileSystem.hpp"
#include "persistence/ProductFileService.hpp"

int main() {
    namespace fs = std::filesystem;
    using namespace oc::interface;
    const auto root = fs::temp_directory_path() / ("ms-bench-namespace-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(!fs::exists(root));
    oc::impl::HostFileSystem backing(root.string().c_str());
    assert(backing.init());
    const uint8_t sentinel[] = {11, 22, 33};
    assert(backing.write("/user-project", 0, sentinel, sizeof(sentinel)));
    core::validation::benchmark::BenchFileSystem scoped(backing);
    core::persistence::ProductFileService files(scoped);
    assert(files.init()); // Layout and recovery must stay inside the namespace.
    assert(fs::exists(root / "ms-rpc-bench/midi-studio/tmp"));
    assert(!fs::exists(root / "midi-studio"));
    for (const char* bad : {"../user-project", "/../user-project", "/a/../../user-project",
                            "/.. /user-project", "/a/./b", "//user-project",
                            "/a\\..\\user-project", "/C:/user-project", "/a/.."}) {
        assert(!scoped.write(bad, 0, sentinel, sizeof(sentinel)));
        assert(!scoped.remove(bad, RemoveMode::RECURSIVE));
    }
    assert(!scoped.stat(std::string(193, '/').c_str()));
    assert(!scoped.remove("/", RemoveMode::RECURSIVE));
    assert(!scoped.rename("/", "/a"));
    assert(scoped.beginWrite("/midi-studio/tmp/a", sizeof(sentinel)));
    assert(scoped.appendWrite(sentinel, sizeof(sentinel)));
    assert(scoped.finishWrite());
    assert(scoped.rename("/midi-studio/tmp/a", "/midi-studio/tmp/b"));
    uint8_t readback[3]{};
    assert(scoped.read("/midi-studio/tmp/b", 0, readback, sizeof(readback)));
    assert(std::memcmp(readback, sentinel, sizeof(sentinel)) == 0);
    assert(scoped.remove("/midi-studio/tmp/b", RemoveMode::FILE_OR_EMPTY_DIRECTORY));
    assert(backing.read("/user-project", 0, readback, sizeof(readback)));
    assert(std::memcmp(readback, sentinel, sizeof(sentinel)) == 0);
    fs::remove_all(root); // This test created and owns the unique temporary root.
    std::cout << "Benchmark SD namespace: layout/recovery/stream/rename/remove isolated; traversal rejected\n";
}

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include <oc/impl/HostFileSystem.hpp>
#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "../../src/persistence/ProductFileService.hpp"

namespace {

using core::persistence::ProductFileService;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() / "midi-studio-core-product-file-service-test";
}

void resetTestRoot() {
    std::error_code ec;
    std::filesystem::remove_all(testRoot(), ec);
}

ProductFileService makeService(oc::impl::HostFileSystem& filesystem) {
    ProductFileService service(filesystem);
    auto init = service.init();
    assert(init);
    return service;
}

bool hasErrorCode(const oc::type::Result<void>& result, oc::type::ErrorCode code) {
    return !result && result.error().code == code;
}

bool hasSizeErrorCode(const oc::type::Result<size_t>& result, oc::type::ErrorCode code) {
    return !result && result.error().code == code;
}

struct RootEntries {
    bool projects = false;
    bool library = false;
    bool tmp = false;
};

bool rootEntryVisitor(const oc::interface::DirectoryEntry& entry, void* context) {
    auto* entries = static_cast<RootEntries*>(context);
    if (std::strcmp(entry.name, "projects") == 0 &&
        entry.type == oc::interface::FileType::DIRECTORY) {
        entries->projects = true;
    }
    if (std::strcmp(entry.name, "library") == 0 &&
        entry.type == oc::interface::FileType::DIRECTORY) {
        entries->library = true;
    }
    if (std::strcmp(entry.name, "tmp") == 0 &&
        entry.type == oc::interface::FileType::DIRECTORY) {
        entries->tmp = true;
    }
    return true;
}

void test_init_creates_product_layout() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto service = makeService(filesystem);

    auto rootInfo = service.stat("/");
    assert(rootInfo);
    assert(rootInfo.value().type == oc::interface::FileType::DIRECTORY);

    RootEntries entries{};
    auto list = service.list("/", rootEntryVisitor, &entries);
    assert(list);
    assert(entries.projects);
    assert(entries.library);
    assert(entries.tmp);

    assert(std::filesystem::is_directory(testRoot() / "midi-studio" / "projects"));
    assert(std::filesystem::is_directory(testRoot() / "midi-studio" / "library"));
    assert(std::filesystem::is_directory(testRoot() / "midi-studio" / "tmp"));

    std::cout << "[PASS] test_init_creates_product_layout\n";
}

void test_resolve_path_accepts_relative_and_product_rooted_paths() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto service = makeService(filesystem);

    char resolved[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    assert(service.resolvePath("projects/demo.msproj", resolved, sizeof(resolved)));
    assert(std::string(resolved) == "/midi-studio/projects/demo.msproj");

    assert(service.resolvePath("/midi-studio/projects/demo.msproj", resolved, sizeof(resolved)));
    assert(std::string(resolved) == "/midi-studio/projects/demo.msproj");

    assert(service.resolvePath("", resolved, sizeof(resolved)));
    assert(std::string(resolved) == "/midi-studio");

    assert(service.resolvePath("/", resolved, sizeof(resolved)));
    assert(std::string(resolved) == "/midi-studio");

    std::cout << "[PASS] test_resolve_path_accepts_relative_and_product_rooted_paths\n";
}

void test_file_roundtrip_rename_and_recursive_remove() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto service = makeService(filesystem);

    assert(service.createDirectory("projects/session-001"));

    const uint8_t first[] = {'m', 's', 'p', 'r', 'o', 'j'};
    auto written = service.write("projects/session-001/project.bin", 0, first, sizeof(first));
    assert(written);
    assert(written.value() == sizeof(first));

    const uint8_t tail[] = {'1'};
    written = service.write("projects/session-001/project.bin", sizeof(first), tail, sizeof(tail));
    assert(written);
    assert(written.value() == sizeof(tail));

    auto info = service.stat("projects/session-001/project.bin");
    assert(info);
    assert(info.value().type == oc::interface::FileType::FILE);
    assert(info.value().sizeBytes == sizeof(first) + sizeof(tail));

    uint8_t buffer[8] = {};
    auto read = service.read("projects/session-001/project.bin", 0, buffer, sizeof(buffer));
    assert(read);
    assert(read.value() == sizeof(first) + sizeof(tail));
    assert(std::memcmp(buffer, "msproj1", 7) == 0);

    assert(service.rename(
        "projects/session-001/project.bin",
        "projects/session-001/current.bin"
    ));
    assert(!service.stat("projects/session-001/project.bin"));
    assert(service.stat("projects/session-001/current.bin"));

    assert(service.remove("projects/session-001", oc::interface::RemoveMode::RECURSIVE));
    assert(!service.stat("projects/session-001"));

    std::cout << "[PASS] test_file_roundtrip_rename_and_recursive_remove\n";
}

void test_sandbox_rejects_escape_and_invalid_paths() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto service = makeService(filesystem);

    const uint8_t payload[] = {1, 2, 3};

    assert(hasSizeErrorCode(
        service.write("../escape.bin", 0, payload, sizeof(payload)),
        oc::type::ErrorCode::INVALID_ARGUMENT
    ));
    assert(hasSizeErrorCode(
        service.write("projects/../../escape.bin", 0, payload, sizeof(payload)),
        oc::type::ErrorCode::INVALID_ARGUMENT
    ));
    assert(hasErrorCode(
        service.createDirectory("projects\\bad"),
        oc::type::ErrorCode::INVALID_ARGUMENT
    ));
    assert(hasSizeErrorCode(
        service.write("C:/escape.bin", 0, payload, sizeof(payload)),
        oc::type::ErrorCode::INVALID_ARGUMENT
    ));
    assert(hasErrorCode(
        service.remove("/", oc::interface::RemoveMode::RECURSIVE),
        oc::type::ErrorCode::INVALID_ARGUMENT
    ));

    assert(!std::filesystem::exists(testRoot().parent_path() / "escape.bin"));
    assert(std::filesystem::is_directory(testRoot() / "midi-studio"));

    std::cout << "[PASS] test_sandbox_rejects_escape_and_invalid_paths\n";
}

void test_sequential_write_session_contract_is_enforced_by_product_service() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto service = makeService(filesystem);

    const uint8_t payload[] = {1, 2, 3, 4};
    assert(hasSizeErrorCode(
        service.appendWrite(payload, sizeof(payload)),
        oc::type::ErrorCode::INVALID_STATE
    ));
    assert(hasErrorCode(service.finishWrite(), oc::type::ErrorCode::INVALID_STATE));

    assert(service.beginWrite("tmp/session.bin", sizeof(payload)));
    assert(service.writeSessionActive());
    assert(hasErrorCode(
        service.beginWrite("tmp/other.bin", sizeof(payload)),
        oc::type::ErrorCode::INVALID_STATE
    ));
    assert(service.appendWrite(payload, 2));
    assert(service.appendWrite(payload + 2, 2));
    assert(service.finishWrite());
    assert(!service.writeSessionActive());

    uint8_t loaded[sizeof(payload)] = {};
    auto read = service.read("tmp/session.bin", 0, loaded, sizeof(loaded));
    assert(read && read.value() == sizeof(payload));
    assert(std::memcmp(loaded, payload, sizeof(payload)) == 0);

    service.abortWrite();
    assert(!service.writeSessionActive());

    std::cout << "[PASS] test_sequential_write_session_contract_is_enforced_by_product_service\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProductFileService tests\n";
    std::cout << "==============================================\n\n";

    test_init_creates_product_layout();
    test_resolve_path_accepts_relative_and_product_rooted_paths();
    test_file_roundtrip_rename_and_recursive_remove();
    test_sandbox_rejects_escape_and_invalid_paths();
    test_sequential_write_session_contract_is_enforced_by_product_service();

    resetTestRoot();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

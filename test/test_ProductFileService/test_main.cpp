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
#include "../../src/persistence/ProductPersistenceCoordinator.hpp"
#include "../../src/persistence/ProductPersistenceJobCoordinator.hpp"

namespace {

using core::persistence::ProductFileService;
using core::persistence::ProductMutationLease;
using core::persistence::ProductMutationOwner;
using core::persistence::ProductPersistenceCoordinator;
using core::persistence::ProductPersistenceCoordinatorSeed;
using core::persistence::ProductPersistenceJobOwner;
using core::persistence::ProductStorageIdentity;
using core::persistence::ProductStorageState;

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

class RetryableHostFileSystem final : public oc::impl::HostFileSystem {
public:
    explicit RetryableHostFileSystem(const char* rootPath)
        : oc::impl::HostFileSystem(rootPath) {}

    oc::type::Result<void> init() override {
        ++initAttempts_;
        if (!mediaPresent_) {
            return oc::type::Result<void>::err(
                {oc::type::ErrorCode::HARDWARE_INIT_FAILED, "test medium absent"}
            );
        }
        return oc::impl::HostFileSystem::init();
    }

    bool available() const override {
        return mediaPresent_ && oc::impl::HostFileSystem::available();
    }

    void setMediaPresent(bool present) { mediaPresent_ = present; }
    uint32_t initAttempts() const { return initAttempts_; }

private:
    uint32_t initAttempts_ = 0;
    bool mediaPresent_ = false;
};

bool hasErrorCode(const oc::type::Result<void>& result, oc::type::ErrorCode code) {
    return !result && result.error().code == code;
}

bool hasSizeErrorCode(const oc::type::Result<size_t>& result, oc::type::ErrorCode code) {
    return !result && result.error().code == code;
}

template <typename T>
bool hasResultErrorCode(const oc::type::Result<T>& result, oc::type::ErrorCode code) {
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

bool countEntryVisitor(const oc::interface::DirectoryEntry&, void* context) {
    auto* count = static_cast<uint16_t*>(context);
    ++(*count);
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
    auto leaseResult = service.acquireMutation(ProductMutationOwner::PROJECT);
    assert(leaseResult);
    auto lease = std::move(leaseResult.value());

    assert(service.createDirectory(lease, "projects/session-001"));

    const uint8_t first[] = {'m', 's', 'p', 'r', 'o', 'j'};
    auto written = service.write(
        lease,
        "projects/session-001/project.bin",
        0,
        first,
        sizeof(first)
    );
    assert(written);
    assert(written.value() == sizeof(first));

    const uint8_t tail[] = {'1'};
    written = service.write(
        lease,
        "projects/session-001/project.bin",
        sizeof(first),
        tail,
        sizeof(tail)
    );
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
        lease,
        "projects/session-001/project.bin",
        "projects/session-001/current.bin"
    ));
    assert(!service.stat("projects/session-001/project.bin"));
    assert(service.stat("projects/session-001/current.bin"));

    assert(service.remove(
        lease,
        "projects/session-001",
        oc::interface::RemoveMode::RECURSIVE
    ));
    assert(!service.stat("projects/session-001"));
    const auto beforeRelease = service.storageIdentity();
    assert(service.releaseMutation(lease));
    assert(service.storageIdentity().mediaGeneration == beforeRelease.mediaGeneration);
    assert(service.storageIdentity().storageEpoch == beforeRelease.storageEpoch + 1);

    std::cout << "[PASS] test_file_roundtrip_rename_and_recursive_remove\n";
}

void test_work_measurement_counts_exact_primitives_and_rejects_nesting() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto service = makeService(filesystem);
    auto leaseResult = service.acquireMutation(ProductMutationOwner::FILESYSTEM_RPC);
    assert(leaseResult);
    auto lease = std::move(leaseResult.value());

    core::persistence::ProductPersistenceWorkUsage usage{};
    {
        auto measuredResult = service.measurePersistenceWork(usage);
        assert(measuredResult);
        auto measured = std::move(measuredResult.value());
        core::persistence::ProductPersistenceWorkUsage nestedUsage{};
        assert(hasResultErrorCode(
            service.measurePersistenceWork(nestedUsage),
            oc::type::ErrorCode::INVALID_STATE
        ));

        assert(service.createDirectory(lease, "projects/measured"));
        const uint8_t payload[] = {1U, 2U, 3U, 4U};
        assert(service.write(
            lease,
            "projects/measured/value.bin",
            0U,
            payload,
            sizeof(payload)
        ));
        assert(service.stat("projects/measured/value.bin"));
        uint16_t visited = 0U;
        assert(service.list("projects/measured", countEntryVisitor, &visited));
        assert(visited == 1U);
        uint8_t readBuffer[sizeof(payload)] = {};
        assert(service.read(
            "projects/measured/value.bin",
            0U,
            readBuffer,
            sizeof(readBuffer)
        ));
        measured.addNodes(1U);
        measured.addAllocations(2U);
    }

    assert(usage.filesystemCalls == 6U);
    assert(usage.bytes == 8U);
    assert(usage.entries == 1U);
    assert(usage.nodes == 1U);
    assert(usage.allocations == 2U);

    core::persistence::ProductPersistenceWorkUsage secondUsage{};
    auto secondMeasurement = service.measurePersistenceWork(secondUsage);
    assert(secondMeasurement);
    assert(service.releaseMutation(lease));

    std::cout << "[PASS] exact persistence work measurement\n";
}

void test_sandbox_rejects_escape_and_invalid_paths() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto service = makeService(filesystem);
    auto leaseResult = service.acquireMutation(ProductMutationOwner::FILESYSTEM_RPC);
    assert(leaseResult);
    auto lease = std::move(leaseResult.value());

    const uint8_t payload[] = {1, 2, 3};

    assert(hasSizeErrorCode(
        service.write(lease, "../escape.bin", 0, payload, sizeof(payload)),
        oc::type::ErrorCode::INVALID_ARGUMENT
    ));
    assert(hasSizeErrorCode(
        service.write(lease, "projects/../../escape.bin", 0, payload, sizeof(payload)),
        oc::type::ErrorCode::INVALID_ARGUMENT
    ));
    assert(hasErrorCode(
        service.createDirectory(lease, "projects\\bad"),
        oc::type::ErrorCode::INVALID_ARGUMENT
    ));
    assert(hasSizeErrorCode(
        service.write(lease, "C:/escape.bin", 0, payload, sizeof(payload)),
        oc::type::ErrorCode::INVALID_ARGUMENT
    ));
    assert(hasErrorCode(
        service.remove(lease, "/", oc::interface::RemoveMode::RECURSIVE),
        oc::type::ErrorCode::INVALID_ARGUMENT
    ));

    assert(!std::filesystem::exists(testRoot().parent_path() / "escape.bin"));
    assert(std::filesystem::is_directory(testRoot() / "midi-studio"));
    const auto unchangedIdentity = service.storageIdentity();
    assert(service.releaseMutation(lease));
    assert(service.storageIdentity() == unchangedIdentity);

    std::cout << "[PASS] test_sandbox_rejects_escape_and_invalid_paths\n";
}

void test_sequential_write_session_contract_is_enforced_by_product_service() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto service = makeService(filesystem);
    auto leaseResult = service.acquireMutation(ProductMutationOwner::FILESYSTEM_RPC);
    assert(leaseResult);
    auto lease = std::move(leaseResult.value());

    const uint8_t payload[] = {1, 2, 3, 4};
    assert(hasSizeErrorCode(
        service.appendWrite(lease, payload, sizeof(payload)),
        oc::type::ErrorCode::INVALID_STATE
    ));
    assert(hasErrorCode(service.finishWrite(lease), oc::type::ErrorCode::INVALID_STATE));

    assert(service.beginWrite(lease, "tmp/session.bin", sizeof(payload)));
    assert(service.writeSessionActive());
    assert(hasErrorCode(
        service.beginWrite(lease, "tmp/other.bin", sizeof(payload)),
        oc::type::ErrorCode::INVALID_STATE
    ));
    assert(service.appendWrite(lease, payload, 2));
    assert(service.appendWrite(lease, payload + 2, 2));
    assert(service.finishWrite(lease));
    assert(!service.writeSessionActive());

    uint8_t loaded[sizeof(payload)] = {};
    auto read = service.read("tmp/session.bin", 0, loaded, sizeof(loaded));
    assert(read && read.value() == sizeof(payload));
    assert(std::memcmp(loaded, payload, sizeof(payload)) == 0);

    assert(service.abortWrite(lease));
    assert(!service.writeSessionActive());
    assert(service.releaseMutation(lease));

    std::cout << "[PASS] test_sequential_write_session_contract_is_enforced_by_product_service\n";
}

void test_coordinator_grants_one_exact_owner_and_advances_epoch_once() {
    ProductPersistenceCoordinator coordinator;
    assert(coordinator.identity() == ProductStorageIdentity{});

    auto acquired = coordinator.acquireMutation(ProductMutationOwner::PROJECT);
    assert(acquired);
    auto lease = std::move(acquired.value());
    assert(coordinator.owns(lease, ProductMutationOwner::PROJECT));
    assert(!coordinator.owns(lease, ProductMutationOwner::ASSET));

    auto competing = coordinator.acquireMutation(ProductMutationOwner::ASSET);
    assert(hasResultErrorCode(competing, oc::type::ErrorCode::HARDWARE_BUSY));

    assert(coordinator.noteMutation(lease));
    assert(coordinator.noteMutation(lease));
    assert(coordinator.releaseMutation(lease));
    assert(!lease.valid());
    assert((coordinator.identity() == ProductStorageIdentity{1, 1}));

    auto readOnlyLeaseResult = coordinator.acquireMutation(ProductMutationOwner::ASSET);
    assert(readOnlyLeaseResult);
    auto readOnlyLease = std::move(readOnlyLeaseResult.value());
    assert(coordinator.releaseMutation(readOnlyLease));
    assert((coordinator.identity() == ProductStorageIdentity{1, 1}));

    std::cout << "[PASS] test_coordinator_grants_one_exact_owner_and_advances_epoch_once\n";
}

void test_coordinator_media_change_invalidates_stale_lease_and_retries_recovery() {
    ProductPersistenceCoordinator coordinator;
    auto acquired = coordinator.acquireMutation(ProductMutationOwner::FILESYSTEM_RPC);
    assert(acquired);
    auto stale = std::move(acquired.value());
    assert(coordinator.noteMutation(stale));

    coordinator.markMediaUnavailable();
    assert(coordinator.storageState() == ProductStorageState::ABSENT);
    assert((coordinator.identity() == ProductStorageIdentity{1, 0}));
    assert(!coordinator.owns(stale));
    assert(hasErrorCode(
        coordinator.releaseMutation(stale),
        oc::type::ErrorCode::INVALID_STATE
    ));

    auto recovery = coordinator.beginRecovery();
    assert(recovery);
    auto recoveryLease = std::move(recovery.value());
    assert(coordinator.storageState() == ProductStorageState::RECOVERING);
    assert((coordinator.identity() == ProductStorageIdentity{2, 0}));
    assert(coordinator.noteMutation(recoveryLease));
    assert(coordinator.completeRecovery(
        recoveryLease,
        false,
        oc::type::ErrorCode::STORAGE_WRITE_FAILED
    ));
    assert(coordinator.storageState() == ProductStorageState::DEGRADED);
    assert((coordinator.identity() == ProductStorageIdentity{2, 1}));

    auto blocked = coordinator.acquireMutation(ProductMutationOwner::PROJECT);
    assert(hasResultErrorCode(blocked, oc::type::ErrorCode::HARDWARE_BUSY));

    auto retry = coordinator.beginRecovery();
    assert(retry);
    auto retryLease = std::move(retry.value());
    assert((coordinator.identity() == ProductStorageIdentity{2, 1}));
    assert(coordinator.completeRecovery(retryLease, true));
    assert(coordinator.storageState() == ProductStorageState::READY);

    std::cout << "[PASS] test_coordinator_media_change_invalidates_stale_lease_and_retries_recovery\n";
}

void test_service_removal_invalidates_prepare_and_open_stream_leases() {
    resetTestRoot();
    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);
    assert(service.init());

    auto prepareResult = service.acquireMutation(ProductMutationOwner::PROJECT);
    assert(prepareResult);
    auto prepareLease = std::move(prepareResult.value());
    assert(service.owns(prepareLease, ProductMutationOwner::PROJECT));

    auto jobResult = service.persistenceJobs().admit({
        .owner = ProductPersistenceJobOwner::PROJECT_CATALOG,
        .nowMs = 10U,
        .quota = core::persistence::PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG,
    });
    assert(jobResult);
    auto job = std::move(jobResult.value());
    assert(service.persistenceJobs().owns(job));

    service.markMediaUnavailable();
    assert(service.storageState() == ProductStorageState::ABSENT);
    assert(!service.owns(prepareLease));
    assert(service.persistenceJobs().depth() == 0U);
    assert(!service.persistenceJobs().owns(job));
    assert(hasErrorCode(
        service.persistenceJobs().cancel(job),
        oc::type::ErrorCode::INVALID_STATE
    ));
    assert(hasErrorCode(
        service.createDirectory(prepareLease, "projects/prepared"),
        oc::type::ErrorCode::INVALID_STATE
    ));
    assert(hasErrorCode(
        service.releaseMutation(prepareLease),
        oc::type::ErrorCode::INVALID_STATE
    ));

    auto firstRecoveryResult = service.beginRecovery();
    assert(firstRecoveryResult);
    auto firstRecovery = std::move(firstRecoveryResult.value());
    assert((service.storageIdentity() == ProductStorageIdentity{2, 0}));
    assert(service.ensureLayout(firstRecovery));
    assert(service.completeRecovery(firstRecovery, true));

    auto streamResult = service.acquireMutation(ProductMutationOwner::FILESYSTEM_RPC);
    assert(streamResult);
    auto streamLease = std::move(streamResult.value());
    const uint8_t bytes[] = {1, 2, 3, 4};
    assert(service.beginWrite(streamLease, "tmp/removal.bin", sizeof(bytes)));
    assert(service.appendWrite(streamLease, bytes, 2));
    assert(service.writeSessionActive());

    service.markMediaUnavailable();
    assert(service.storageState() == ProductStorageState::ABSENT);
    assert(!service.writeSessionActive());
    assert(!service.owns(streamLease));
    assert(hasSizeErrorCode(
        service.appendWrite(streamLease, bytes + 2, 2),
        oc::type::ErrorCode::INVALID_STATE
    ));
    assert(hasErrorCode(
        service.finishWrite(streamLease),
        oc::type::ErrorCode::INVALID_STATE
    ));
    assert(hasErrorCode(
        service.releaseMutation(streamLease),
        oc::type::ErrorCode::INVALID_STATE
    ));

    auto secondRecoveryResult = service.beginRecovery();
    assert(secondRecoveryResult);
    auto secondRecovery = std::move(secondRecoveryResult.value());
    assert((service.storageIdentity() == ProductStorageIdentity{3, 0}));
    assert(service.completeRecovery(secondRecovery, true));
    assert(service.storageState() == ProductStorageState::READY);

    std::cout
        << "[PASS] test_service_removal_invalidates_prepare_and_open_stream_leases\n";
}

void test_service_retries_initially_absent_backend_and_admits_generation_once() {
    resetTestRoot();
    RetryableHostFileSystem filesystem(testRoot().string().c_str());
    ProductFileService service(filesystem);

    const auto unavailable = service.initForRecovery();
    assert(hasErrorCode(unavailable, oc::type::ErrorCode::HARDWARE_INIT_FAILED));
    assert(filesystem.initAttempts() == 1);
    assert(service.storageState() == ProductStorageState::ABSENT);
    assert((service.storageIdentity() == ProductStorageIdentity{1, 0}));
    assert(!service.mediaPresent());

    const auto blocked = service.beginRecovery();
    assert(hasResultErrorCode(blocked, oc::type::ErrorCode::HARDWARE_NOT_FOUND));
    assert((service.storageIdentity() == ProductStorageIdentity{1, 0}));

    filesystem.setMediaPresent(true);
    assert(service.initForRecovery());
    assert(filesystem.initAttempts() == 2);
    assert(service.mediaPresent());
    assert(!service.available());
    assert(service.storageState() == ProductStorageState::ABSENT);
    assert((service.storageIdentity() == ProductStorageIdentity{1, 0}));

    auto recoveryResult = service.beginRecovery();
    assert(recoveryResult);
    auto recovery = std::move(recoveryResult.value());
    assert(service.storageState() == ProductStorageState::RECOVERING);
    assert((service.storageIdentity() == ProductStorageIdentity{2, 0}));
    assert(service.ensureLayout(recovery));
    assert(service.completeRecovery(recovery, true));
    assert(service.storageState() == ProductStorageState::READY);
    assert(service.available());
    assert((service.storageIdentity() == ProductStorageIdentity{2, 1}));

    std::cout
        << "[PASS] test_service_retries_initially_absent_backend_and_admits_generation_once\n";
}

void test_coordinator_identity_exhaustion_is_fail_closed() {
    ProductPersistenceCoordinator finalLeaseCoordinator{
        ProductPersistenceCoordinatorSeed{
            .nextLeaseId = UINT32_MAX,
            .identity = {7, 4},
        }
    };
    auto finalLeaseResult =
        finalLeaseCoordinator.acquireMutation(ProductMutationOwner::PROJECT);
    assert(finalLeaseResult);
    auto finalLease = std::move(finalLeaseResult.value());
    assert(finalLeaseCoordinator.noteMutation(finalLease));
    assert(finalLeaseCoordinator.releaseMutation(finalLease));
    assert((finalLeaseCoordinator.identity() == ProductStorageIdentity{7, 5}));
    auto reused = finalLeaseCoordinator.acquireMutation(ProductMutationOwner::PROJECT);
    assert(hasResultErrorCode(reused, oc::type::ErrorCode::RESOURCE_EXHAUSTED));
    assert(finalLeaseCoordinator.storageState() == ProductStorageState::EXHAUSTED);

    ProductPersistenceCoordinator epochCoordinator{
        ProductPersistenceCoordinatorSeed{
            .identity = {9, UINT32_MAX},
        }
    };
    auto epochWrapped = epochCoordinator.acquireMutation(ProductMutationOwner::ASSET);
    assert(hasResultErrorCode(epochWrapped, oc::type::ErrorCode::RESOURCE_EXHAUSTED));
    assert((epochCoordinator.identity() == ProductStorageIdentity{9, UINT32_MAX}));

    ProductPersistenceCoordinator generationCoordinator{
        ProductPersistenceCoordinatorSeed{
            .identity = {UINT32_MAX, 3},
        }
    };
    generationCoordinator.markMediaUnavailable();
    auto generationWrapped = generationCoordinator.beginRecovery();
    assert(hasResultErrorCode(
        generationWrapped,
        oc::type::ErrorCode::RESOURCE_EXHAUSTED
    ));
    assert((generationCoordinator.identity() == ProductStorageIdentity{UINT32_MAX, 3}));
    assert(generationCoordinator.storageState() == ProductStorageState::EXHAUSTED);

    std::cout << "[PASS] test_coordinator_identity_exhaustion_is_fail_closed\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProductFileService tests\n";
    std::cout << "==============================================\n\n";

    test_init_creates_product_layout();
    test_resolve_path_accepts_relative_and_product_rooted_paths();
    test_file_roundtrip_rename_and_recursive_remove();
    test_work_measurement_counts_exact_primitives_and_rejects_nesting();
    test_sandbox_rejects_escape_and_invalid_paths();
    test_sequential_write_session_contract_is_enforced_by_product_service();
    test_coordinator_grants_one_exact_owner_and_advances_epoch_once();
    test_coordinator_media_change_invalidates_stale_lease_and_retries_recovery();
    test_service_removal_invalidates_prepare_and_open_stream_leases();
    test_service_retries_initially_absent_backend_and_admits_generation_once();
    test_coordinator_identity_exhaustion_is_fail_closed();

    resetTestRoot();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

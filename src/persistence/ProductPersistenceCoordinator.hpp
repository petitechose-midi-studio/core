#pragma once

#include <cstdint>
#include <utility>

#include <oc/type/Result.hpp>

namespace core::persistence {

struct ProductStorageIdentity {
    uint32_t mediaGeneration = 1;
    uint32_t storageEpoch = 0;

    constexpr bool operator==(const ProductStorageIdentity& other) const {
        return mediaGeneration == other.mediaGeneration &&
               storageEpoch == other.storageEpoch;
    }

    constexpr bool operator!=(const ProductStorageIdentity& other) const {
        return !(*this == other);
    }
};

enum class ProductMutationOwner : uint8_t {
    NONE = 0,
    PROJECT,
    ASSET,
    FILESYSTEM_RPC,
    RECOVERY,
};

enum class ProductStorageState : uint8_t {
    READY = 0,
    ABSENT,
    RECOVERY_PENDING,
    RECOVERING,
    DEGRADED,
    EXHAUSTED,
};

class ProductPersistenceCoordinator;
class ProductFileService;

/**
 * Exact, non-reusable capability for one product-storage mutation.
 *
 * The handle is deliberately only one 32-bit scalar. It is move-only so a
 * retained transaction cannot accidentally create a second release authority.
 * ProductPersistenceCoordinator validates the scalar against its sole active
 * ID; media removal invalidates that ID before a new medium can be admitted.
 */
class ProductMutationLease {
public:
    constexpr ProductMutationLease() = default;

    ProductMutationLease(const ProductMutationLease&) = delete;
    ProductMutationLease& operator=(const ProductMutationLease&) = delete;

    constexpr ProductMutationLease(ProductMutationLease&& other) noexcept
        : id_(other.id_) {
        other.id_ = 0;
    }

    constexpr ProductMutationLease& operator=(ProductMutationLease&& other) noexcept {
        if (this != &other) {
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }

    constexpr bool valid() const { return id_ != 0; }

private:
    friend class ProductPersistenceCoordinator;
    friend class ProductFileService;

    constexpr explicit ProductMutationLease(uint32_t id) : id_(id) {}
    constexpr void invalidate_() { id_ = 0; }

    uint32_t id_ = 0;
};

/** Testable seed for checked terminal-boundary behavior. Production uses it defaults. */
struct ProductPersistenceCoordinatorSeed {
    uint32_t nextLeaseId = 1;
    ProductStorageIdentity identity{};
    ProductStorageState storageState = ProductStorageState::READY;
};

/**
 * Allocation-free single authority for product-file mutation and recovery.
 *
 * A normal mutation may run only in READY. Recovery uses the same exact lease
 * while READY (boot verification) or while reconciling a missing/degraded
 * medium. Each released lease which touched media advances storageEpoch once.
 */
class ProductPersistenceCoordinator {
public:
    explicit ProductPersistenceCoordinator(ProductPersistenceCoordinatorSeed seed = {});

    oc::type::Result<ProductMutationLease> acquireMutation(ProductMutationOwner owner);
    oc::type::Result<ProductMutationLease> beginRecovery();

    bool owns(const ProductMutationLease& lease) const;
    bool owns(const ProductMutationLease& lease, ProductMutationOwner owner) const;
    oc::type::Result<void> noteMutation(const ProductMutationLease& lease);
    oc::type::Result<void> releaseMutation(ProductMutationLease& lease);
    oc::type::Result<void> completeRecovery(
        ProductMutationLease& lease,
        bool success,
        oc::type::ErrorCode error = oc::type::ErrorCode::OK
    );

    void markMediaUnavailable();
    oc::type::Result<void> requireRecovery(oc::type::ErrorCode error);

    ProductStorageIdentity identity() const { return identity_; }
    ProductStorageState storageState() const { return storage_state_; }
    ProductMutationOwner activeOwner() const { return active_owner_; }
    oc::type::ErrorCode recoveryError() const { return recovery_error_; }
    bool mutationActive() const { return active_lease_id_ != 0; }

private:
    oc::type::Result<ProductMutationLease> acquire_(ProductMutationOwner owner);
    void invalidateActiveLease_();
    oc::type::Result<ProductMutationLease> exhausted_(const char* context);

    uint32_t next_lease_id_ = 1;
    uint32_t active_lease_id_ = 0;
    ProductStorageIdentity identity_{};
    ProductMutationOwner active_owner_ = ProductMutationOwner::NONE;
    ProductStorageState storage_state_ = ProductStorageState::READY;
    bool active_mutation_touched_ = false;
    oc::type::ErrorCode recovery_error_ = oc::type::ErrorCode::OK;
};

static_assert(sizeof(ProductStorageIdentity) == 8, "storage identity ABI drift");
static_assert(alignof(ProductStorageIdentity) == 4, "storage identity alignment drift");
static_assert(sizeof(ProductMutationLease) == 4, "mutation lease ABI drift");
static_assert(alignof(ProductMutationLease) == 4, "mutation lease alignment drift");
static_assert(
    sizeof(ProductPersistenceCoordinator) == 20,
    "persistence coordinator exceeds LOCK-S"
);
static_assert(
    alignof(ProductPersistenceCoordinator) == 4,
    "persistence coordinator alignment drift"
);

}  // namespace core::persistence

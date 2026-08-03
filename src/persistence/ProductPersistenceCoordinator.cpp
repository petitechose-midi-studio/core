#include "persistence/ProductPersistenceCoordinator.hpp"

#include <config/PlatformCompat.hpp>

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

template <typename T>
FLASHMEM oc::type::Result<T> error(ErrorCode code, const char* context) {
    return oc::type::Result<T>::err({code, context});
}

}  // namespace

FLASHMEM ProductPersistenceCoordinator::ProductPersistenceCoordinator(
    ProductPersistenceCoordinatorSeed seed
) : next_lease_id_(seed.nextLeaseId)
  , identity_(seed.identity)
  , storage_state_(seed.storageState) {
    if (identity_.mediaGeneration == 0) {
        storage_state_ = ProductStorageState::EXHAUSTED;
        recovery_error_ = ErrorCode::RESOURCE_EXHAUSTED;
    }
}

FLASHMEM oc::type::Result<ProductMutationLease>
ProductPersistenceCoordinator::acquireMutation(ProductMutationOwner owner) {
    if (owner == ProductMutationOwner::NONE || owner == ProductMutationOwner::RECOVERY) {
        return error<ProductMutationLease>(
            ErrorCode::INVALID_ARGUMENT,
            "invalid product mutation owner"
        );
    }
    if (storage_state_ == ProductStorageState::EXHAUSTED) {
        return exhausted_("product storage identity exhausted");
    }
    if (storage_state_ != ProductStorageState::READY) {
        return error<ProductMutationLease>(
            ErrorCode::HARDWARE_BUSY,
            "product storage recovery pending"
        );
    }
    return acquire_(owner);
}

FLASHMEM oc::type::Result<ProductMutationLease>
ProductPersistenceCoordinator::beginRecovery() {
    if (storage_state_ == ProductStorageState::EXHAUSTED) {
        return exhausted_("product storage identity exhausted");
    }
    if (active_lease_id_ != 0 || storage_state_ == ProductStorageState::RECOVERING) {
        return error<ProductMutationLease>(
            ErrorCode::HARDWARE_BUSY,
            "product mutation already active"
        );
    }
    // A reinserted medium receives a new generation only if recovery can also
    // receive a unique lease. Keep the identity unchanged on terminal lease
    // exhaustion instead of publishing a generation which no operation owns.
    if (next_lease_id_ == 0) {
        storage_state_ = ProductStorageState::EXHAUSTED;
        recovery_error_ = ErrorCode::RESOURCE_EXHAUSTED;
        return exhausted_("product mutation identity exhausted");
    }
    if (storage_state_ == ProductStorageState::ABSENT) {
        if (identity_.mediaGeneration == UINT32_MAX) {
            storage_state_ = ProductStorageState::EXHAUSTED;
            recovery_error_ = ErrorCode::RESOURCE_EXHAUSTED;
            return exhausted_("product media generation exhausted");
        }
        ++identity_.mediaGeneration;
        identity_.storageEpoch = 0;
        storage_state_ = ProductStorageState::RECOVERY_PENDING;
    }
    if (storage_state_ != ProductStorageState::READY &&
        storage_state_ != ProductStorageState::RECOVERY_PENDING &&
        storage_state_ != ProductStorageState::DEGRADED) {
        return error<ProductMutationLease>(
            ErrorCode::INVALID_STATE,
            "product storage cannot enter recovery"
        );
    }

    auto acquired = acquire_(ProductMutationOwner::RECOVERY);
    if (acquired) {
        storage_state_ = ProductStorageState::RECOVERING;
        recovery_error_ = ErrorCode::OK;
    }
    return acquired;
}

bool ProductPersistenceCoordinator::owns(const ProductMutationLease& lease) const {
    return lease.id_ != 0 && lease.id_ == active_lease_id_;
}

bool ProductPersistenceCoordinator::owns(
    const ProductMutationLease& lease,
    ProductMutationOwner owner
) const {
    return owns(lease) && owner != ProductMutationOwner::NONE && active_owner_ == owner;
}

FLASHMEM oc::type::Result<void> ProductPersistenceCoordinator::noteMutation(
    const ProductMutationLease& lease
) {
    if (!owns(lease)) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "stale product mutation lease"}
        );
    }
    active_mutation_touched_ = true;
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProductPersistenceCoordinator::releaseMutation(
    ProductMutationLease& lease
) {
    if (!owns(lease)) {
        lease.invalidate_();
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "stale product mutation lease"}
        );
    }

    if (active_mutation_touched_) {
        // acquire_ refuses an epoch already at UINT32_MAX, so this increment
        // reaches the terminal value but can never wrap it.
        ++identity_.storageEpoch;
    }
    invalidateActiveLease_();
    lease.invalidate_();
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProductPersistenceCoordinator::completeRecovery(
    ProductMutationLease& lease,
    bool success,
    ErrorCode errorCode
) {
    if (storage_state_ != ProductStorageState::RECOVERING ||
        !owns(lease, ProductMutationOwner::RECOVERY)) {
        lease.invalidate_();
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "stale product recovery lease"}
        );
    }

    auto released = releaseMutation(lease);
    if (!released) {
        storage_state_ = ProductStorageState::DEGRADED;
        recovery_error_ = ErrorCode::INVALID_STATE;
        return released;
    }

    storage_state_ = success
        ? ProductStorageState::READY
        : ProductStorageState::DEGRADED;
    recovery_error_ = success
        ? ErrorCode::OK
        : (errorCode == ErrorCode::OK ? ErrorCode::STORAGE_WRITE_FAILED : errorCode);
    return oc::type::Result<void>::ok();
}

FLASHMEM void ProductPersistenceCoordinator::markMediaUnavailable() {
    invalidateActiveLease_();
    if (storage_state_ == ProductStorageState::EXHAUSTED) {
        return;
    }
    storage_state_ = ProductStorageState::ABSENT;
    recovery_error_ = ErrorCode::HARDWARE_NOT_FOUND;
}

FLASHMEM oc::type::Result<void> ProductPersistenceCoordinator::requireRecovery(
    ErrorCode errorCode
) {
    if (active_lease_id_ != 0) {
        return oc::type::Result<void>::err(
            {ErrorCode::HARDWARE_BUSY, "product mutation already active"}
        );
    }
    if (storage_state_ == ProductStorageState::ABSENT ||
        storage_state_ == ProductStorageState::EXHAUSTED) {
        return oc::type::Result<void>::err(
            {storage_state_ == ProductStorageState::ABSENT
                 ? ErrorCode::HARDWARE_NOT_FOUND
                 : ErrorCode::RESOURCE_EXHAUSTED,
             "product storage is not recoverable yet"}
        );
    }
    storage_state_ = errorCode == ErrorCode::OK
        ? ProductStorageState::RECOVERY_PENDING
        : ProductStorageState::DEGRADED;
    recovery_error_ = errorCode;
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<ProductMutationLease>
ProductPersistenceCoordinator::acquire_(ProductMutationOwner owner) {
    if (active_lease_id_ != 0) {
        return error<ProductMutationLease>(
            ErrorCode::HARDWARE_BUSY,
            "product mutation already active"
        );
    }
    if (next_lease_id_ == 0 || identity_.storageEpoch == UINT32_MAX) {
        storage_state_ = ProductStorageState::EXHAUSTED;
        recovery_error_ = ErrorCode::RESOURCE_EXHAUSTED;
        return exhausted_("product mutation identity exhausted");
    }

    active_lease_id_ = next_lease_id_;
    next_lease_id_ = next_lease_id_ == UINT32_MAX ? 0 : next_lease_id_ + 1;
    active_owner_ = owner;
    active_mutation_touched_ = false;
    return oc::type::Result<ProductMutationLease>::ok(
        ProductMutationLease{active_lease_id_}
    );
}

FLASHMEM void ProductPersistenceCoordinator::invalidateActiveLease_() {
    active_lease_id_ = 0;
    active_owner_ = ProductMutationOwner::NONE;
    active_mutation_touched_ = false;
}

FLASHMEM oc::type::Result<ProductMutationLease>
ProductPersistenceCoordinator::exhausted_(const char* context) {
    return error<ProductMutationLease>(ErrorCode::RESOURCE_EXHAUSTED, context);
}

}  // namespace core::persistence

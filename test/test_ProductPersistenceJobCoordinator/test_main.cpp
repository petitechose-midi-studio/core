#include <cassert>
#include <cstdint>
#include <iostream>
#include <utility>

#include <oc/type/Result.hpp>

#include "persistence/ProductPersistenceJobCoordinator.hpp"

namespace {

using core::persistence::PRODUCT_PERSISTENCE_AUTOSAVE_MAX_DEFERRAL_MS;
using core::persistence::PRODUCT_PERSISTENCE_QUOTA_ASSET_METADATA;
using core::persistence::PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO;
using core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE;
using core::persistence::PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG;
using core::persistence::PRODUCT_PERSISTENCE_SOFT_ADVANCE_WALL_MICROS;
using core::persistence::ProductPersistenceJobAdmission;
using core::persistence::ProductPersistenceJobCoordinator;
using core::persistence::ProductPersistenceJobCoordinatorSeed;
using core::persistence::ProductPersistenceJobOwner;
using core::persistence::ProductPersistenceJobPriority;
using core::persistence::ProductPersistenceJobSnapshot;
using core::persistence::ProductPersistenceJobState;
using core::persistence::ProductPersistenceJobToken;
using core::persistence::ProductPersistenceWorkQuota;
using core::persistence::ProductPersistenceWorkUsage;
using oc::type::ErrorCode;

template <typename T>
bool hasErrorCode(const oc::type::Result<T>& result, ErrorCode code) {
    return !result && result.error().code == code;
}

ProductPersistenceJobAdmission admission(
    ProductPersistenceJobOwner owner,
    uint32_t nowMs,
    ProductPersistenceWorkQuota quota = PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO,
    uint32_t deadlineAfterMs = 0U
) {
    return {
        .owner = owner,
        .nowMs = nowMs,
        .deadlineAfterMs = deadlineAfterMs,
        .quota = quota,
    };
}

ProductPersistenceJobToken admit(
    ProductPersistenceJobCoordinator& coordinator,
    const ProductPersistenceJobAdmission& request
) {
    auto result = coordinator.admit(request);
    assert(result);
    return std::move(result.value());
}

ProductPersistenceJobSnapshot inspect(
    const ProductPersistenceJobCoordinator& coordinator,
    const ProductPersistenceJobToken& token
) {
    ProductPersistenceJobSnapshot snapshot{};
    assert(coordinator.inspect(token, snapshot));
    return snapshot;
}

void test_frozen_quota_constants_and_compact_layout() {
    static_assert(sizeof(ProductPersistenceWorkQuota) == 8U);
    static_assert(sizeof(ProductPersistenceWorkUsage) == 16U);
    static_assert(sizeof(ProductPersistenceJobToken) == 4U);
    static_assert(sizeof(ProductPersistenceJobCoordinator) == 128U);

    assert(PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG.maxBytes() == 19456U);
    assert(PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG.maxFilesystemCalls() == 1U);
    assert(PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG.maxEntries() == 257U);
    assert(PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG.maxAllocations() == 0U);
    assert(PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG.maxNodes() == 0U);
    assert(PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG.valid());

    ProductPersistenceJobCoordinator coordinator;
    auto invalidQuota = ProductPersistenceWorkQuota::limited(1U, 16U, 0U, 0U, 0U);
    assert(!invalidQuota.valid());
    auto rejected = coordinator.admit(admission(
        ProductPersistenceJobOwner::FILESYSTEM_RPC,
        0U,
        invalidQuota
    ));
    assert(hasErrorCode(rejected, ErrorCode::INVALID_ARGUMENT));
    assert(coordinator.depth() == 0U);

    auto valid = admit(
        coordinator,
        admission(ProductPersistenceJobOwner::FILESYSTEM_RPC, 0U)
    );
    assert(valid.id() == 1U);

    std::cout << "[PASS] frozen quota constants and compact layout\n";
}

void test_capacity_priority_stable_ids_and_high_water() {
    ProductPersistenceJobCoordinator coordinator({.nextJobId = 41U});

    auto catalog = admit(
        coordinator,
        admission(
            ProductPersistenceJobOwner::PROJECT_CATALOG,
            100U,
            PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG
        )
    );
    assert(catalog.id() == 41U);
    assert(coordinator.activeJobId() == catalog.id());

    auto explicitJob = admit(
        coordinator,
        admission(ProductPersistenceJobOwner::FILESYSTEM_RPC, 101U)
    );
    assert(explicitJob.id() == 42U);
    assert(coordinator.beginTurn(101U));
    assert(coordinator.activeJobId() == explicitJob.id());
    assert(coordinator.deferredJobId() == catalog.id());
    assert(coordinator.isActive(explicitJob));
    auto explicitSnapshot = inspect(coordinator, explicitJob);
    auto catalogSnapshot = inspect(coordinator, catalog);
    assert(explicitSnapshot.priority == ProductPersistenceJobPriority::EXPLICIT);
    assert(explicitSnapshot.state == ProductPersistenceJobState::ACTIVE);
    assert(catalogSnapshot.priority == ProductPersistenceJobPriority::CATALOG);
    assert(catalogSnapshot.state == ProductPersistenceJobState::DEFERRED);
    assert(coordinator.depth() == 2U);
    assert(coordinator.highWater() == 2U);

    auto third = coordinator.admit(admission(
        ProductPersistenceJobOwner::PROJECT_AUTOSAVE,
        102U
    ));
    assert(hasErrorCode(third, ErrorCode::HARDWARE_BUSY));
    assert(coordinator.cancel(catalog));

    auto autosave = admit(
        coordinator,
        admission(ProductPersistenceJobOwner::PROJECT_AUTOSAVE, 103U)
    );
    assert(autosave.id() == 43U);
    assert(coordinator.activeJobId() == explicitJob.id());
    assert(coordinator.deferredJobId() == autosave.id());
    assert(coordinator.highWater() == 2U);

    std::cout << "[PASS] capacity, priority, stable ids and high-water\n";
}

void test_priority_changes_only_at_safe_yield() {
    ProductPersistenceJobCoordinator coordinator;
    auto explicitJob = admit(
        coordinator,
        admission(ProductPersistenceJobOwner::FILESYSTEM_RPC, 0U)
    );

    assert(coordinator.beginTurn(0U));
    assert(coordinator.claimAdvance(explicitJob, 0U));

    auto recovery = admit(
        coordinator,
        admission(ProductPersistenceJobOwner::STORAGE_RECOVERY, 1U)
    );
    assert(coordinator.activeJobId() == explicitJob.id());
    assert(coordinator.deferredJobId() == recovery.id());

    ProductPersistenceWorkUsage usage{
        .bytes = 128U,
        .wallMicros = 100U,
        .filesystemCalls = 1U,
    };
    assert(coordinator.finishAdvance(explicitJob, usage, false));
    assert(coordinator.activeJobId() == explicitJob.id());
    assert(hasErrorCode(coordinator.complete(explicitJob), ErrorCode::INVALID_STATE));
    assert(hasErrorCode(coordinator.cancel(explicitJob), ErrorCode::INVALID_STATE));
    assert(explicitJob.valid());

    assert(coordinator.beginTurn(2U));
    assert(coordinator.claimAdvance(explicitJob, 2U));
    assert(coordinator.finishAdvance(explicitJob, usage, true));
    assert(coordinator.activeJobId() == explicitJob.id());
    assert(coordinator.beginTurn(3U));
    assert(coordinator.activeJobId() == recovery.id());
    assert(coordinator.deferredJobId() == explicitJob.id());
    assert(coordinator.complete(recovery));
    assert(coordinator.activeJobId() == explicitJob.id());

    std::cout << "[PASS] priority changes only at safe yield\n";
}

void test_autosave_aging_is_rollover_safe_and_does_not_preempt_recovery() {
    constexpr uint32_t autosaveAdmittedAt = UINT32_MAX - 999U;
    constexpr uint32_t beforeAge = 999U;
    constexpr uint32_t atAge = 1000U;
    static_assert(
        static_cast<uint32_t>(atAge - autosaveAdmittedAt) ==
        PRODUCT_PERSISTENCE_AUTOSAVE_MAX_DEFERRAL_MS
    );

    ProductPersistenceJobCoordinator coordinator;
    auto explicitJob = admit(
        coordinator,
        admission(ProductPersistenceJobOwner::FILESYSTEM_RPC, autosaveAdmittedAt - 1U)
    );
    auto autosave = admit(
        coordinator,
        admission(ProductPersistenceJobOwner::PROJECT_AUTOSAVE, autosaveAdmittedAt)
    );

    assert(!coordinator.deferredAutosaveAged(beforeAge));
    assert(coordinator.beginTurn(beforeAge));
    assert(coordinator.activeJobId() == explicitJob.id());
    assert(coordinator.deferredAutosaveAged(atAge));
    assert(coordinator.beginTurn(atAge));
    assert(coordinator.activeJobId() == autosave.id());

    ProductPersistenceJobCoordinator recoveryCoordinator;
    auto recovery = admit(
        recoveryCoordinator,
        admission(ProductPersistenceJobOwner::STORAGE_RECOVERY, autosaveAdmittedAt - 1U)
    );
    auto deferredAutosave = admit(
        recoveryCoordinator,
        admission(ProductPersistenceJobOwner::PROJECT_AUTOSAVE, autosaveAdmittedAt)
    );
    assert(recoveryCoordinator.beginTurn(atAge));
    assert(recoveryCoordinator.activeJobId() == recovery.id());
    assert(recoveryCoordinator.deferredJobId() == deferredAutosave.id());

    std::cout << "[PASS] rollover-safe autosave aging and recovery priority\n";
}

void test_one_claim_per_turn_and_exact_metrics() {
    ProductPersistenceJobCoordinator coordinator;
    auto job = admit(
        coordinator,
        admission(ProductPersistenceJobOwner::FILESYSTEM_RPC, 0U)
    );

    assert(hasErrorCode(
        coordinator.prepareAdvance(job, PRODUCT_PERSISTENCE_QUOTA_ASSET_METADATA),
        ErrorCode::INVALID_STATE
    ));
    assert(coordinator.beginTurn(0U));
    assert(coordinator.prepareAdvance(
        job,
        PRODUCT_PERSISTENCE_QUOTA_ASSET_METADATA
    ));
    assert(
        inspect(coordinator, job).quota.maxBytes() ==
        PRODUCT_PERSISTENCE_QUOTA_ASSET_METADATA.maxBytes()
    );
    assert(coordinator.claimAdvance(job, 0U));
    assert(hasErrorCode(
        coordinator.claimAdvance(job, 0U),
        ErrorCode::INVALID_STATE
    ));
    assert(hasErrorCode(coordinator.beginTurn(1U), ErrorCode::INVALID_STATE));

    ProductPersistenceWorkUsage first{
        .bytes = 100U,
        .wallMicros = PRODUCT_PERSISTENCE_SOFT_ADVANCE_WALL_MICROS + 1U,
        .filesystemCalls = 1U,
    };
    assert(coordinator.finishAdvance(job, first, true));

    auto firstSnapshot = inspect(coordinator, job);
    assert(firstSnapshot.lastUsage.bytes == 100U);
    assert(firstSnapshot.metrics.cumulativeBytes == 100U);
    assert(firstSnapshot.metrics.cumulativeWallMicros == 501U);
    assert(firstSnapshot.metrics.filesystemCalls == 1U);
    assert(firstSnapshot.metrics.advances == 1U);
    assert(firstSnapshot.wallOverruns == 1U);
    assert(firstSnapshot.safeYield);

    assert(coordinator.beginTurn(2U));
    assert(coordinator.prepareAdvance(job, PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO));
    assert(coordinator.claimAdvance(job, 2U));
    ProductPersistenceWorkUsage second{
        .bytes = 200U,
        .wallMicros = 499U,
        .filesystemCalls = 1U,
    };
    assert(coordinator.finishAdvance(job, second, true));

    auto secondSnapshot = inspect(coordinator, job);
    assert(secondSnapshot.metrics.cumulativeBytes == 300U);
    assert(secondSnapshot.metrics.cumulativeWallMicros == 1000U);
    assert(secondSnapshot.metrics.filesystemCalls == 2U);
    assert(secondSnapshot.metrics.advances == 2U);
    assert(secondSnapshot.wallOverruns == 1U);

    assert(coordinator.beginTurn(3U));
    assert(coordinator.prepareAdvance(job, PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO));
    assert(coordinator.claimAdvance(job, 3U));
    ProductPersistenceWorkUsage exceeded{
        .bytes = PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO.maxBytes() + 1U,
        .wallMicros = 1U,
        .filesystemCalls = 1U,
    };
    assert(hasErrorCode(
        coordinator.finishAdvance(job, exceeded, true),
        ErrorCode::RESOURCE_EXHAUSTED
    ));
    auto exceededSnapshot = inspect(coordinator, job);
    assert(exceededSnapshot.quotaExceeded);
    assert(exceededSnapshot.metrics.advances == 3U);

    assert(coordinator.beginTurn(4U));
    assert(hasErrorCode(
        coordinator.prepareAdvance(job, PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE),
        ErrorCode::RESOURCE_EXHAUSTED
    ));
    assert(hasErrorCode(
        coordinator.claimAdvance(job, 4U),
        ErrorCode::RESOURCE_EXHAUSTED
    ));
    assert(coordinator.cancel(job));

    std::cout << "[PASS] one claim per turn, metrics and hard quota\n";
}

void test_deadline_and_id_exhaustion_are_rollover_safe() {
    ProductPersistenceJobCoordinator coordinator({.nextJobId = UINT32_MAX});
    constexpr uint32_t admittedAt = UINT32_MAX - 5U;
    auto lastJob = admit(
        coordinator,
        admission(
            ProductPersistenceJobOwner::FILESYSTEM_RPC,
            admittedAt,
            PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO,
            10U
        )
    );
    assert(lastJob.id() == UINT32_MAX);

    auto snapshot = inspect(coordinator, lastJob);
    assert(snapshot.hasDeadline);
    assert(snapshot.deadlineAtMs == 4U);
    assert(!coordinator.deadlineExpired(lastJob, 3U));
    assert(coordinator.deadlineExpired(lastJob, 4U));

    assert(coordinator.beginTurn(3U));
    assert(coordinator.claimAdvance(lastJob, 3U));
    assert(coordinator.finishAdvance(lastJob, {}, true));
    assert(coordinator.complete(lastJob));

    auto exhausted = coordinator.admit(admission(
        ProductPersistenceJobOwner::PROJECT_CATALOG,
        5U,
        PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG
    ));
    assert(hasErrorCode(exhausted, ErrorCode::RESOURCE_EXHAUSTED));

    ProductPersistenceJobCoordinator deadlineCoordinator;
    auto expired = admit(
        deadlineCoordinator,
        admission(ProductPersistenceJobOwner::FILESYSTEM_RPC, 10U,
                  PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO, 5U)
    );
    assert(deadlineCoordinator.beginTurn(15U));
    assert(hasErrorCode(
        deadlineCoordinator.claimAdvance(expired, 15U),
        ErrorCode::HARDWARE_TIMEOUT
    ));
    assert(deadlineCoordinator.expire(expired, 15U));

    ProductPersistenceJobCoordinator unsafeExpiryCoordinator;
    auto unsafeExpired = admit(
        unsafeExpiryCoordinator,
        admission(ProductPersistenceJobOwner::FILESYSTEM_RPC, 20U,
                  PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO, 5U)
    );
    assert(unsafeExpiryCoordinator.beginTurn(20U));
    assert(unsafeExpiryCoordinator.claimAdvance(unsafeExpired, 20U));
    assert(unsafeExpiryCoordinator.finishAdvance(unsafeExpired, {}, false));
    assert(unsafeExpiryCoordinator.beginTurn(25U));
    assert(unsafeExpiryCoordinator.claimAdvance(unsafeExpired, 25U));
    assert(unsafeExpiryCoordinator.finishAdvance(unsafeExpired, {}, true));
    assert(unsafeExpiryCoordinator.expire(unsafeExpired, 25U));

    std::cout << "[PASS] rollover-safe deadline and terminal job ids\n";
}

void test_exact_release_and_media_invalidation() {
    ProductPersistenceJobCoordinator coordinator;
    auto explicitJob = admit(
        coordinator,
        admission(ProductPersistenceJobOwner::FILESYSTEM_RPC, 0U)
    );
    auto catalog = admit(
        coordinator,
        admission(
            ProductPersistenceJobOwner::PROJECT_CATALOG,
            0U,
            PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG
        )
    );
    assert(coordinator.highWater() == 2U);
    assert(coordinator.owns(explicitJob));
    assert(coordinator.owns(catalog));

    coordinator.invalidateAll();
    assert(coordinator.depth() == 0U);
    assert(coordinator.highWater() == 2U);
    assert(!coordinator.owns(explicitJob));
    assert(!coordinator.owns(catalog));
    assert(hasErrorCode(coordinator.cancel(explicitJob), ErrorCode::INVALID_STATE));
    assert(!explicitJob.valid());

    auto next = admit(
        coordinator,
        admission(ProductPersistenceJobOwner::FILESYSTEM_RPC, 1U)
    );
    assert(next.id() == 3U);
    assert(coordinator.complete(next));
    assert(!next.valid());
    assert(hasErrorCode(coordinator.complete(next), ErrorCode::INVALID_STATE));

    std::cout << "[PASS] exact release and media invalidation\n";
}

void test_unsafe_external_unwind_has_one_explicit_release_path() {
    ProductPersistenceJobCoordinator coordinator;
    auto job = admit(
        coordinator,
        admission(ProductPersistenceJobOwner::FILESYSTEM_RPC, 0U)
    );

    assert(coordinator.beginTurn(0U));
    assert(coordinator.claimAdvance(job, 0U));
    assert(hasErrorCode(
        coordinator.cancelAfterUnwind(job),
        ErrorCode::INVALID_STATE
    ));
    assert(job.valid());
    assert(coordinator.finishAdvance(job, {}, false));
    assert(hasErrorCode(coordinator.cancel(job), ErrorCode::INVALID_STATE));
    assert(coordinator.cancelAfterUnwind(job));
    assert(!job.valid());
    assert(coordinator.depth() == 0U);

    std::cout << "[PASS] explicit unsafe unwind release\n";
}

}  // namespace

int main() {
    test_frozen_quota_constants_and_compact_layout();
    test_capacity_priority_stable_ids_and_high_water();
    test_priority_changes_only_at_safe_yield();
    test_autosave_aging_is_rollover_safe_and_does_not_preempt_recovery();
    test_one_claim_per_turn_and_exact_metrics();
    test_deadline_and_id_exhaustion_are_rollover_safe();
    test_exact_release_and_media_invalidation();
    test_unsafe_external_unwind_has_one_explicit_release_path();
    return 0;
}

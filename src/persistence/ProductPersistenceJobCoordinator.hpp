#pragma once

#include <cstdint>

#include <oc/type/Result.hpp>

namespace core::persistence {

inline constexpr uint32_t PRODUCT_PERSISTENCE_SOFT_ADVANCE_WALL_MICROS = 500U;
inline constexpr uint32_t PRODUCT_PERSISTENCE_AUTOSAVE_MAX_DEFERRAL_MS = 2000U;
inline constexpr uint32_t PRODUCT_PERSISTENCE_MAX_DEADLINE_DURATION_MS =
    0x7FFFFFFFU;

enum class ProductPersistenceJobOwner : uint8_t {
    NONE = 0,
    STORAGE_RECOVERY,
    HIDDEN_TREE_CLEANUP,
    FILESYSTEM_RPC,
    PROJECT_EXPLICIT,
    ASSET_EXPLICIT,
    PROJECT_AUTOSAVE,
    PROJECT_CATALOG,
    STEP_PRESET_CATALOG,
    CHORD_PRESET_CATALOG,
};

enum class ProductPersistenceJobPriority : uint8_t {
    NONE = 0,
    CATALOG,
    AUTOSAVE,
    EXPLICIT,
    RECOVERY,
};

enum class ProductPersistenceJobState : uint8_t {
    EMPTY = 0,
    ACTIVE,
    DEFERRED,
};

constexpr ProductPersistenceJobPriority productPersistenceJobPriority(
    ProductPersistenceJobOwner owner
) {
    switch (owner) {
        case ProductPersistenceJobOwner::STORAGE_RECOVERY:
        case ProductPersistenceJobOwner::HIDDEN_TREE_CLEANUP:
            return ProductPersistenceJobPriority::RECOVERY;
        case ProductPersistenceJobOwner::FILESYSTEM_RPC:
        case ProductPersistenceJobOwner::PROJECT_EXPLICIT:
        case ProductPersistenceJobOwner::ASSET_EXPLICIT:
            return ProductPersistenceJobPriority::EXPLICIT;
        case ProductPersistenceJobOwner::PROJECT_AUTOSAVE:
            return ProductPersistenceJobPriority::AUTOSAVE;
        case ProductPersistenceJobOwner::PROJECT_CATALOG:
        case ProductPersistenceJobOwner::STEP_PRESET_CATALOG:
        case ProductPersistenceJobOwner::CHORD_PRESET_CATALOG:
            return ProductPersistenceJobPriority::CATALOG;
        case ProductPersistenceJobOwner::NONE:
        default:
            return ProductPersistenceJobPriority::NONE;
    }
}

struct ProductPersistenceWorkUsage {
    uint32_t bytes = 0;
    uint32_t wallMicros = 0;
    uint16_t entries = 0;
    uint8_t filesystemCalls = 0;
    uint8_t allocations = 0;
    uint8_t nodes = 0;
};

/**
 * Hard per-advance resource ceiling.
 *
 * Filesystem calls and nodes are packed into one byte because every frozen
 * L-R05-04 class is <=8 calls and <=1 node. The factory retains an invalid bit
 * when calls/nodes exceed 15 or allocations exceed 127; admission then fails
 * instead of silently truncating a quota.
 */
class ProductPersistenceWorkQuota {
public:
    static constexpr ProductPersistenceWorkQuota limited(
        uint32_t maxBytes,
        uint8_t maxFilesystemCalls,
        uint8_t maxAllocations,
        uint16_t maxEntries,
        uint8_t maxNodes
    ) {
        return ProductPersistenceWorkQuota(
            maxBytes,
            maxFilesystemCalls,
            maxAllocations,
            maxEntries,
            maxNodes
        );
    }

    constexpr uint32_t maxBytes() const { return max_bytes_; }
    constexpr uint16_t maxEntries() const { return max_entries_; }
    constexpr uint8_t maxFilesystemCalls() const {
        return filesystem_calls_and_nodes_ & 0x0FU;
    }
    constexpr uint8_t maxAllocations() const {
        return max_allocations_and_validity_ & 0x7FU;
    }
    constexpr uint8_t maxNodes() const {
        return (filesystem_calls_and_nodes_ >> 4U) & 0x0FU;
    }
    constexpr bool valid() const {
        return (max_allocations_and_validity_ & 0x80U) == 0U;
    }

private:
    constexpr ProductPersistenceWorkQuota() = default;

    constexpr ProductPersistenceWorkQuota(
        uint32_t maxBytes,
        uint8_t maxFilesystemCalls,
        uint8_t maxAllocations,
        uint16_t maxEntries,
        uint8_t maxNodes
    ) : max_bytes_(maxBytes)
      , max_entries_(maxEntries)
      , max_allocations_and_validity_(static_cast<uint8_t>(
            (maxAllocations & 0x7FU) |
            ((maxFilesystemCalls > 15U || maxAllocations > 127U || maxNodes > 15U)
                 ? 0x80U
                 : 0U)
        ))
      , filesystem_calls_and_nodes_(static_cast<uint8_t>(
            (maxFilesystemCalls & 0x0FU) | ((maxNodes & 0x0FU) << 4U)
        )) {}

    friend class ProductPersistenceJobCoordinator;

    uint32_t max_bytes_ = 0;
    uint16_t max_entries_ = 0;
    uint8_t max_allocations_and_validity_ = 0;
    uint8_t filesystem_calls_and_nodes_ = 0;
};

inline constexpr ProductPersistenceWorkQuota PRODUCT_PERSISTENCE_QUOTA_ENDPOINT_FRAME =
    ProductPersistenceWorkQuota::limited(32512U, 0U, 0U, 0U, 0U);
inline constexpr ProductPersistenceWorkQuota PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO =
    ProductPersistenceWorkQuota::limited(30720U, 1U, 0U, 0U, 0U);
inline constexpr ProductPersistenceWorkQuota
    PRODUCT_PERSISTENCE_QUOTA_AUTOSAVE_AUTOMATION =
        ProductPersistenceWorkQuota::limited(4096U, 0U, 0U, 0U, 0U);
inline constexpr ProductPersistenceWorkQuota
    PRODUCT_PERSISTENCE_QUOTA_AUTOSAVE_SEQUENCER =
        ProductPersistenceWorkQuota::limited(16384U, 0U, 0U, 0U, 0U);
inline constexpr ProductPersistenceWorkQuota PRODUCT_PERSISTENCE_QUOTA_PROJECT_ENCODE =
    ProductPersistenceWorkQuota::limited(524288U, 0U, 0U, 0U, 0U);
inline constexpr ProductPersistenceWorkQuota PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE =
    ProductPersistenceWorkQuota::limited(607U, 8U, 0U, 0U, 1U);
inline constexpr ProductPersistenceWorkQuota PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG =
    ProductPersistenceWorkQuota::limited(19456U, 1U, 0U, 257U, 0U);
inline constexpr ProductPersistenceWorkQuota PRODUCT_PERSISTENCE_QUOTA_ASSET_METADATA =
    ProductPersistenceWorkQuota::limited(112U, 1U, 0U, 1U, 0U);
inline constexpr ProductPersistenceWorkQuota PRODUCT_PERSISTENCE_QUOTA_TREE_CLEANUP =
    ProductPersistenceWorkQuota::limited(193U, 2U, 0U, 1U, 1U);

struct ProductPersistenceWorkMetrics {
    uint32_t cumulativeBytes = 0;
    uint32_t cumulativeWallMicros = 0;
    uint16_t filesystemCalls = 0;
    uint16_t entries = 0;
    uint16_t advances = 0;
    uint8_t allocations = 0;
    uint8_t nodes = 0;
};

class ProductPersistenceJobToken {
public:
    constexpr ProductPersistenceJobToken() = default;

    ProductPersistenceJobToken(const ProductPersistenceJobToken&) = delete;
    ProductPersistenceJobToken& operator=(const ProductPersistenceJobToken&) = delete;

    constexpr ProductPersistenceJobToken(ProductPersistenceJobToken&& other) noexcept
        : id_(other.id_) {
        other.id_ = 0;
    }

    constexpr ProductPersistenceJobToken& operator=(
        ProductPersistenceJobToken&& other
    ) noexcept {
        if (this != &other) {
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }

    constexpr bool valid() const { return id_ != 0; }
    constexpr uint32_t id() const { return id_; }

private:
    friend class ProductPersistenceJobCoordinator;

    constexpr explicit ProductPersistenceJobToken(uint32_t id) : id_(id) {}
    constexpr void invalidate_() { id_ = 0; }

    uint32_t id_ = 0;
};

struct ProductPersistenceJobAdmission {
    ProductPersistenceJobOwner owner = ProductPersistenceJobOwner::NONE;
    uint32_t nowMs = 0;
    uint32_t deadlineAfterMs = 0;
    ProductPersistenceWorkQuota quota =
        ProductPersistenceWorkQuota::limited(0U, 0U, 0U, 0U, 0U);
};

struct ProductPersistenceJobSnapshot {
    uint32_t id = 0;
    uint32_t admittedAtMs = 0;
    uint32_t deadlineAtMs = 0;
    ProductPersistenceWorkQuota quota =
        ProductPersistenceWorkQuota::limited(0U, 0U, 0U, 0U, 0U);
    ProductPersistenceWorkUsage lastUsage{};
    ProductPersistenceWorkMetrics metrics{};
    uint16_t wallOverruns = 0;
    ProductPersistenceJobOwner owner = ProductPersistenceJobOwner::NONE;
    ProductPersistenceJobPriority priority = ProductPersistenceJobPriority::NONE;
    ProductPersistenceJobState state = ProductPersistenceJobState::EMPTY;
    bool hasDeadline = false;
    bool safeYield = false;
    bool quotaExceeded = false;
};

struct ProductPersistenceJobCoordinatorSeed {
    uint32_t nextJobId = 1;
};

/**
 * Allocation-free scheduler for exactly one active and one deferred job.
 *
 * The coordinator owns scheduling facts only. Payloads remain in preadmitted
 * PSRAM owners and the existing ProductPersistenceCoordinator remains the sole
 * authority for durable mutation. A foreground turn can claim at most one
 * advance, and a running job can be reordered only at a declared safe yield.
 */
class ProductPersistenceJobCoordinator {
public:
    explicit ProductPersistenceJobCoordinator(
        ProductPersistenceJobCoordinatorSeed seed = {}
    );

    oc::type::Result<ProductPersistenceJobToken> admit(
        const ProductPersistenceJobAdmission& admission
    );

    oc::type::Result<void> beginTurn(uint32_t nowMs);
    oc::type::Result<void> prepareAdvance(
        const ProductPersistenceJobToken& token,
        ProductPersistenceWorkQuota quota
    );
    oc::type::Result<void> claimAdvance(
        const ProductPersistenceJobToken& token,
        uint32_t nowMs
    );
    oc::type::Result<void> finishAdvance(
        const ProductPersistenceJobToken& token,
        const ProductPersistenceWorkUsage& usage,
        bool safeYield
    );

    oc::type::Result<void> complete(ProductPersistenceJobToken& token);
    oc::type::Result<void> cancel(ProductPersistenceJobToken& token);
    /** Release an unsafe record only after its owner has synchronously unwound
     * every external resource. A currently claimed advance cannot be bypassed. */
    oc::type::Result<void> cancelAfterUnwind(ProductPersistenceJobToken& token);
    oc::type::Result<void> expire(ProductPersistenceJobToken& token, uint32_t nowMs);
    void invalidateAll();

    bool owns(const ProductPersistenceJobToken& token) const;
    bool isActive(const ProductPersistenceJobToken& token) const;
    bool deadlineExpired(
        const ProductPersistenceJobToken& token,
        uint32_t nowMs
    ) const;
    bool deferredAutosaveAged(uint32_t nowMs) const;
    bool inspect(
        const ProductPersistenceJobToken& token,
        ProductPersistenceJobSnapshot& snapshot
    ) const;

    uint8_t depth() const { return depth_; }
    uint8_t highWater() const { return high_water_; }
    uint32_t activeJobId() const;
    uint32_t deferredJobId() const;

private:
    enum class TurnState : uint8_t {
        CLOSED = 0,
        OPEN,
        CLAIMED,
        RECORDED,
    };

    struct Record {
        ProductPersistenceWorkQuota quota =
            ProductPersistenceWorkQuota::limited(0U, 0U, 0U, 0U, 0U);
        ProductPersistenceWorkUsage lastUsage{};
        ProductPersistenceWorkMetrics metrics{};
        uint32_t id = 0;
        uint32_t admittedAtMs = 0;
        uint32_t deadlineAtMs = 0;
        uint16_t wallOverruns = 0;
        ProductPersistenceJobOwner owner = ProductPersistenceJobOwner::NONE;
        ProductPersistenceJobPriority priority = ProductPersistenceJobPriority::NONE;
        ProductPersistenceJobState state = ProductPersistenceJobState::EMPTY;
        uint8_t flags = 0;
    };

    static constexpr uint8_t INVALID_SLOT = 0xFFU;
    static constexpr uint8_t FLAG_HAS_DEADLINE = 1U << 0U;
    static constexpr uint8_t FLAG_SAFE_YIELD = 1U << 1U;
    static constexpr uint8_t FLAG_QUOTA_EXCEEDED = 1U << 2U;

    Record* recordFor_(const ProductPersistenceJobToken& token);
    const Record* recordFor_(const ProductPersistenceJobToken& token) const;
    Record* activeRecord_();
    const Record* activeRecord_() const;
    Record* deferredRecord_();
    const Record* deferredRecord_() const;
    uint8_t slotFor_(const Record* record) const;
    uint8_t emptySlot_() const;
    bool safeYield_(const Record& record) const;
    bool hasDeadline_(const Record& record) const;
    bool quotaExceeded_(const Record& record) const;
    bool usageWithinQuota_(
        const ProductPersistenceWorkUsage& usage,
        const ProductPersistenceWorkQuota& quota
    ) const;
    bool preferDeferred_() const;
    void rebalance_();
    void updateMetrics_(Record& record, const ProductPersistenceWorkUsage& usage);
    void releaseRecord_(Record& record);

    Record records_[2]{};
    uint32_t next_job_id_ = 1;
    uint8_t active_slot_ = INVALID_SLOT;
    uint8_t depth_ = 0;
    uint8_t high_water_ = 0;
    TurnState turn_state_ = TurnState::CLOSED;
};

static_assert(sizeof(ProductPersistenceWorkUsage) == 16U, "job usage ABI drift");
static_assert(sizeof(ProductPersistenceWorkQuota) == 8U, "job quota ABI drift");
static_assert(sizeof(ProductPersistenceWorkMetrics) == 16U, "job metrics ABI drift");
static_assert(sizeof(ProductPersistenceJobToken) == 4U, "job token ABI drift");
static_assert(
    sizeof(ProductPersistenceJobCoordinator) <= 128U,
    "persistence job coordinator exceeds LOCK-S"
);
static_assert(
    alignof(ProductPersistenceJobCoordinator) == 4U,
    "persistence job coordinator alignment drift"
);

}  // namespace core::persistence

#include "persistence/ProductPersistenceJobCoordinator.hpp"

#include <limits>

#include <config/PlatformCompat.hpp>

#include "diagnostics/StorageQualificationProbe.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

const char kInvalidOwner[] PROGMEM = "invalid persistence job owner";
const char kDeadlineRangeExceeded[] PROGMEM =
    "persistence job deadline exceeds rollover-safe range";
const char kQuotaRangeExceeded[] PROGMEM =
    "persistence job quota exceeds compact range";
const char kQueueFull[] PROGMEM = "persistence job queue full";
const char kIdentityExhausted[] PROGMEM = "persistence job identity exhausted";
const char kQueueStateCorrupt[] PROGMEM = "persistence job queue state corrupt";
const char kAdvanceUsageMissing[] PROGMEM =
    "persistence advance has no usage record";
const char kTurnAlreadyConsumed[] PROGMEM = "persistence turn already consumed";
const char kAdvancePreparationClosed[] PROGMEM =
    "persistence advance preparation is closed";
const char kAdvancePreparationInvalid[] PROGMEM =
    "persistence advance quota is invalid";
const char kJobNotActive[] PROGMEM = "persistence job is not active";
const char kQuotaAlreadyExceeded[] PROGMEM =
    "persistence job quota already exceeded";
const char kDeadlineExpired[] PROGMEM = "persistence job deadline expired";
const char kAdvanceNotClaimed[] PROGMEM = "persistence advance was not claimed";
const char kAdvanceQuotaExceeded[] PROGMEM =
    "persistence advance exceeded hard quota";
const char kStaleToken[] PROGMEM = "stale persistence job token";
const char kNotTerminalSafe[] PROGMEM = "persistence job is not terminal-safe";
const char kNotCancelSafe[] PROGMEM = "persistence job is not cancel-safe";
const char kDeadlineNotExpired[] PROGMEM =
    "persistence job deadline has not expired";

template <typename T>
FLASHMEM oc::type::Result<T> error(ErrorCode code, const char* context) {
    return oc::type::Result<T>::err({code, context});
}

template <typename T, typename U>
T saturatingAdd(T current, U increment) {
    const auto maximum = std::numeric_limits<T>::max();
    const auto room = static_cast<uint64_t>(maximum) - static_cast<uint64_t>(current);
    if (static_cast<uint64_t>(increment) > room) {
        return maximum;
    }
    return static_cast<T>(current + static_cast<T>(increment));
}

bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
    return static_cast<uint32_t>(nowMs - deadlineMs) <=
           PRODUCT_PERSISTENCE_MAX_DEADLINE_DURATION_MS;
}

bool elapsedAtLeast(uint32_t nowMs, uint32_t startedAtMs, uint32_t durationMs) {
    return static_cast<uint32_t>(nowMs - startedAtMs) >= durationMs;
}

}  // namespace

FLASHMEM ProductPersistenceJobCoordinator::ProductPersistenceJobCoordinator(
    ProductPersistenceJobCoordinatorSeed seed
) : next_job_id_(seed.nextJobId) {}

FLASHMEM oc::type::Result<ProductPersistenceJobToken>
ProductPersistenceJobCoordinator::admit(
    const ProductPersistenceJobAdmission& admission
) {
    const auto priority = productPersistenceJobPriority(admission.owner);
    if (admission.owner == ProductPersistenceJobOwner::NONE ||
        priority == ProductPersistenceJobPriority::NONE) {
        return error<ProductPersistenceJobToken>(
            ErrorCode::INVALID_ARGUMENT,
            kInvalidOwner
        );
    }
    if (admission.deadlineAfterMs > PRODUCT_PERSISTENCE_MAX_DEADLINE_DURATION_MS) {
        return error<ProductPersistenceJobToken>(
            ErrorCode::INVALID_ARGUMENT,
            kDeadlineRangeExceeded
        );
    }
    if (!admission.quota.valid()) {
        return error<ProductPersistenceJobToken>(
            ErrorCode::INVALID_ARGUMENT,
            kQuotaRangeExceeded
        );
    }
    if (depth_ >= 2U) {
        return error<ProductPersistenceJobToken>(
            ErrorCode::HARDWARE_BUSY,
            kQueueFull
        );
    }
    if (next_job_id_ == 0U) {
        return error<ProductPersistenceJobToken>(
            ErrorCode::RESOURCE_EXHAUSTED,
            kIdentityExhausted
        );
    }

    const uint8_t slot = emptySlot_();
    if (slot == INVALID_SLOT) {
        return error<ProductPersistenceJobToken>(
            ErrorCode::INVALID_STATE,
            kQueueStateCorrupt
        );
    }

    Record& record = records_[slot];
    record = {};
    record.quota = admission.quota;
    record.id = next_job_id_;
    record.admittedAtMs = admission.nowMs;
    record.owner = admission.owner;
    record.priority = priority;
    record.flags = FLAG_SAFE_YIELD;
    if (admission.deadlineAfterMs != 0U) {
        record.deadlineAtMs = admission.nowMs + admission.deadlineAfterMs;
        record.flags |= FLAG_HAS_DEADLINE;
    }

    const uint32_t admittedId = next_job_id_;
    next_job_id_ = next_job_id_ == UINT32_MAX ? 0U : next_job_id_ + 1U;

    if (depth_ == 0U) {
        record.state = ProductPersistenceJobState::ACTIVE;
        active_slot_ = slot;
    } else {
        record.state = ProductPersistenceJobState::DEFERRED;
    }
    ++depth_;
    if (depth_ > high_water_) {
        high_water_ = depth_;
    }

    core::diagnostics::storage_qualification::recordJobAdmission(
        admittedId,
        static_cast<uint8_t>(record.owner),
        depth_,
        high_water_
    );

    return oc::type::Result<ProductPersistenceJobToken>::ok(
        ProductPersistenceJobToken{admittedId}
    );
}

oc::type::Result<void> ProductPersistenceJobCoordinator::beginTurn(uint32_t nowMs) {
    if (turn_state_ == TurnState::CLAIMED) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kAdvanceUsageMissing}
        );
    }
    (void)nowMs;
    turn_state_ = TurnState::OPEN;
    rebalance_();
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProductPersistenceJobCoordinator::prepareAdvance(
    const ProductPersistenceJobToken& token,
    ProductPersistenceWorkQuota quota
) {
    if (turn_state_ != TurnState::OPEN) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kAdvancePreparationClosed}
        );
    }
    if (!quota.valid()) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_ARGUMENT, kAdvancePreparationInvalid}
        );
    }

    Record* record = recordFor_(token);
    if (!record || record->state != ProductPersistenceJobState::ACTIVE) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kJobNotActive}
        );
    }
    if (quotaExceeded_(*record)) {
        return oc::type::Result<void>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, kQuotaAlreadyExceeded}
        );
    }

    record->quota = quota;
    return oc::type::Result<void>::ok();
}

oc::type::Result<void> ProductPersistenceJobCoordinator::claimAdvance(
    const ProductPersistenceJobToken& token,
    uint32_t nowMs
) {
    if (turn_state_ != TurnState::OPEN) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kTurnAlreadyConsumed}
        );
    }

    Record* record = recordFor_(token);
    if (!record || record->state != ProductPersistenceJobState::ACTIVE) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kJobNotActive}
        );
    }
    if (quotaExceeded_(*record)) {
        return oc::type::Result<void>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, kQuotaAlreadyExceeded}
        );
    }
    if (hasDeadline_(*record) && deadlineReached(nowMs, record->deadlineAtMs) &&
        safeYield_(*record)) {
        return oc::type::Result<void>::err(
            {ErrorCode::HARDWARE_TIMEOUT, kDeadlineExpired}
        );
    }

    record->flags &= static_cast<uint8_t>(~FLAG_SAFE_YIELD);
    turn_state_ = TurnState::CLAIMED;
    core::diagnostics::storage_qualification::recordJobClaim(
        record->id,
        static_cast<uint8_t>(record->owner)
    );
    return oc::type::Result<void>::ok();
}

oc::type::Result<void> ProductPersistenceJobCoordinator::finishAdvance(
    const ProductPersistenceJobToken& token,
    const ProductPersistenceWorkUsage& usage,
    bool safeYield
) {
    Record* record = recordFor_(token);
    if (turn_state_ != TurnState::CLAIMED || !record ||
        record->state != ProductPersistenceJobState::ACTIVE) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kAdvanceNotClaimed}
        );
    }

    record->lastUsage = usage;
    updateMetrics_(*record, usage);
    const bool withinQuota = usageWithinQuota_(usage, record->quota);
    if (!withinQuota) {
        record->flags |= FLAG_QUOTA_EXCEEDED;
    }
    if (safeYield) {
        record->flags |= FLAG_SAFE_YIELD;
    } else {
        record->flags &= static_cast<uint8_t>(~FLAG_SAFE_YIELD);
    }

    turn_state_ = TurnState::RECORDED;
    core::diagnostics::storage_qualification::recordJobAdvance(
        record->id,
        static_cast<uint8_t>(record->owner),
        usage.bytes,
        usage.wallMicros,
        usage.entries,
        usage.filesystemCalls,
        usage.allocations,
        usage.nodes,
        high_water_,
        safeYield,
        !withinQuota
    );
    if (!withinQuota) {
        return oc::type::Result<void>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, kAdvanceQuotaExceeded}
        );
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProductPersistenceJobCoordinator::complete(
    ProductPersistenceJobToken& token
) {
    Record* record = recordFor_(token);
    if (!record) {
        token.invalidate_();
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kStaleToken}
        );
    }
    if (record->state != ProductPersistenceJobState::ACTIVE ||
        turn_state_ == TurnState::CLAIMED || !safeYield_(*record)) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kNotTerminalSafe}
        );
    }

    core::diagnostics::storage_qualification::recordJobTerminal(
        record->id,
        static_cast<uint8_t>(record->owner),
        core::diagnostics::storage_qualification::PhaseKind::Complete,
        static_cast<uint8_t>(ErrorCode::OK)
    );
    releaseRecord_(*record);
    token.invalidate_();
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProductPersistenceJobCoordinator::cancel(
    ProductPersistenceJobToken& token
) {
    Record* record = recordFor_(token);
    if (!record) {
        token.invalidate_();
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kStaleToken}
        );
    }
    if (record->state == ProductPersistenceJobState::ACTIVE &&
        (turn_state_ == TurnState::CLAIMED || !safeYield_(*record))) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kNotCancelSafe}
        );
    }

    core::diagnostics::storage_qualification::recordJobTerminal(
        record->id,
        static_cast<uint8_t>(record->owner),
        core::diagnostics::storage_qualification::PhaseKind::Cancel,
        static_cast<uint8_t>(ErrorCode::OK)
    );
    releaseRecord_(*record);
    token.invalidate_();
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProductPersistenceJobCoordinator::cancelAfterUnwind(
    ProductPersistenceJobToken& token
) {
    Record* record = recordFor_(token);
    if (!record) {
        token.invalidate_();
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kStaleToken}
        );
    }
    if (record->state == ProductPersistenceJobState::ACTIVE &&
        turn_state_ == TurnState::CLAIMED) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kNotCancelSafe}
        );
    }

    core::diagnostics::storage_qualification::recordJobTerminal(
        record->id,
        static_cast<uint8_t>(record->owner),
        core::diagnostics::storage_qualification::PhaseKind::Cancel,
        static_cast<uint8_t>(ErrorCode::OK)
    );
    releaseRecord_(*record);
    token.invalidate_();
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProductPersistenceJobCoordinator::expire(
    ProductPersistenceJobToken& token,
    uint32_t nowMs
) {
    Record* record = recordFor_(token);
    if (!record) {
        token.invalidate_();
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kStaleToken}
        );
    }
    if (!hasDeadline_(*record) || !deadlineReached(nowMs, record->deadlineAtMs)) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kDeadlineNotExpired}
        );
    }
#if defined(MS_STORAGE_QUALIFICATION) && OC_ENABLE_STATS
    if (record->state == ProductPersistenceJobState::ACTIVE &&
        (turn_state_ == TurnState::CLAIMED || !safeYield_(*record))) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, kNotCancelSafe}
        );
    }
    core::diagnostics::storage_qualification::recordJobTerminal(
        record->id,
        static_cast<uint8_t>(record->owner),
        core::diagnostics::storage_qualification::PhaseKind::Expire,
        static_cast<uint8_t>(ErrorCode::HARDWARE_TIMEOUT)
    );
    releaseRecord_(*record);
    token.invalidate_();
    return oc::type::Result<void>::ok();
#else
    return cancel(token);
#endif
}

FLASHMEM void ProductPersistenceJobCoordinator::invalidateAll() {
    for (const auto& record : records_) {
        if (record.state == ProductPersistenceJobState::EMPTY) continue;
        core::diagnostics::storage_qualification::recordJobTerminal(
            record.id,
            static_cast<uint8_t>(record.owner),
            core::diagnostics::storage_qualification::PhaseKind::Invalidate,
            static_cast<uint8_t>(ErrorCode::HARDWARE_NOT_FOUND)
        );
    }
    records_[0] = {};
    records_[1] = {};
    active_slot_ = INVALID_SLOT;
    depth_ = 0;
    turn_state_ = TurnState::CLOSED;
}

bool ProductPersistenceJobCoordinator::owns(
    const ProductPersistenceJobToken& token
) const {
    return recordFor_(token) != nullptr;
}

bool ProductPersistenceJobCoordinator::isActive(
    const ProductPersistenceJobToken& token
) const {
    const Record* record = recordFor_(token);
    return record && record->state == ProductPersistenceJobState::ACTIVE;
}

bool ProductPersistenceJobCoordinator::deadlineExpired(
    const ProductPersistenceJobToken& token,
    uint32_t nowMs
) const {
    const Record* record = recordFor_(token);
    return record && hasDeadline_(*record) &&
           deadlineReached(nowMs, record->deadlineAtMs);
}

bool ProductPersistenceJobCoordinator::deferredAutosaveAged(uint32_t nowMs) const {
    const Record* deferred = deferredRecord_();
    return deferred && deferred->priority == ProductPersistenceJobPriority::AUTOSAVE &&
           elapsedAtLeast(
               nowMs,
               deferred->admittedAtMs,
               PRODUCT_PERSISTENCE_AUTOSAVE_MAX_DEFERRAL_MS
           );
}

FLASHMEM bool ProductPersistenceJobCoordinator::inspect(
    const ProductPersistenceJobToken& token,
    ProductPersistenceJobSnapshot& snapshot
) const {
    const Record* record = recordFor_(token);
    if (!record) {
        snapshot = {};
        return false;
    }

    snapshot.id = record->id;
    snapshot.admittedAtMs = record->admittedAtMs;
    snapshot.deadlineAtMs = record->deadlineAtMs;
    snapshot.quota = record->quota;
    snapshot.lastUsage = record->lastUsage;
    snapshot.metrics = record->metrics;
    snapshot.wallOverruns = record->wallOverruns;
    snapshot.owner = record->owner;
    snapshot.priority = record->priority;
    snapshot.state = record->state;
    snapshot.hasDeadline = hasDeadline_(*record);
    snapshot.safeYield = safeYield_(*record);
    snapshot.quotaExceeded = quotaExceeded_(*record);
    return true;
}

uint32_t ProductPersistenceJobCoordinator::activeJobId() const {
    const Record* record = activeRecord_();
    return record ? record->id : 0U;
}

uint32_t ProductPersistenceJobCoordinator::deferredJobId() const {
    const Record* record = deferredRecord_();
    return record ? record->id : 0U;
}

ProductPersistenceJobCoordinator::Record*
ProductPersistenceJobCoordinator::recordFor_(const ProductPersistenceJobToken& token) {
    if (!token.valid()) return nullptr;
    for (auto& record : records_) {
        if (record.id == token.id_) return &record;
    }
    return nullptr;
}

const ProductPersistenceJobCoordinator::Record*
ProductPersistenceJobCoordinator::recordFor_(
    const ProductPersistenceJobToken& token
) const {
    if (!token.valid()) return nullptr;
    for (const auto& record : records_) {
        if (record.id == token.id_) return &record;
    }
    return nullptr;
}

ProductPersistenceJobCoordinator::Record*
ProductPersistenceJobCoordinator::activeRecord_() {
    return active_slot_ < 2U ? &records_[active_slot_] : nullptr;
}

const ProductPersistenceJobCoordinator::Record*
ProductPersistenceJobCoordinator::activeRecord_() const {
    return active_slot_ < 2U ? &records_[active_slot_] : nullptr;
}

ProductPersistenceJobCoordinator::Record*
ProductPersistenceJobCoordinator::deferredRecord_() {
    for (auto& record : records_) {
        if (record.state == ProductPersistenceJobState::DEFERRED) return &record;
    }
    return nullptr;
}

const ProductPersistenceJobCoordinator::Record*
ProductPersistenceJobCoordinator::deferredRecord_() const {
    for (const auto& record : records_) {
        if (record.state == ProductPersistenceJobState::DEFERRED) return &record;
    }
    return nullptr;
}

uint8_t ProductPersistenceJobCoordinator::slotFor_(const Record* record) const {
    if (!record) return INVALID_SLOT;
    for (uint8_t slot = 0; slot < 2U; ++slot) {
        if (&records_[slot] == record) return slot;
    }
    return INVALID_SLOT;
}

uint8_t ProductPersistenceJobCoordinator::emptySlot_() const {
    for (uint8_t slot = 0; slot < 2U; ++slot) {
        if (records_[slot].state == ProductPersistenceJobState::EMPTY) return slot;
    }
    return INVALID_SLOT;
}

bool ProductPersistenceJobCoordinator::safeYield_(const Record& record) const {
    return (record.flags & FLAG_SAFE_YIELD) != 0U;
}

bool ProductPersistenceJobCoordinator::hasDeadline_(const Record& record) const {
    return (record.flags & FLAG_HAS_DEADLINE) != 0U;
}

bool ProductPersistenceJobCoordinator::quotaExceeded_(const Record& record) const {
    return (record.flags & FLAG_QUOTA_EXCEEDED) != 0U;
}

bool ProductPersistenceJobCoordinator::usageWithinQuota_(
    const ProductPersistenceWorkUsage& usage,
    const ProductPersistenceWorkQuota& quota
) const {
    return usage.bytes <= quota.maxBytes() &&
           usage.filesystemCalls <= quota.maxFilesystemCalls() &&
           usage.allocations <= quota.maxAllocations() &&
           usage.entries <= quota.maxEntries() && usage.nodes <= quota.maxNodes();
}

bool ProductPersistenceJobCoordinator::preferDeferred_() const {
    const Record* active = activeRecord_();
    const Record* deferred = deferredRecord_();
    if (!active || !deferred || !safeYield_(*active)) return false;

    // Autosave age is a signal to the active explicit owner, not scheduler
    // authority to swap records behind that owner's back. In particular, an
    // upload must first abort its open stream at a safe boundary; releasing
    // its record then promotes the already-admitted autosave normally.
    return static_cast<uint8_t>(deferred->priority) >
           static_cast<uint8_t>(active->priority);
}

void ProductPersistenceJobCoordinator::rebalance_() {
    if (!preferDeferred_()) return;

    Record* active = activeRecord_();
    Record* deferred = deferredRecord_();
    const uint8_t deferredSlot = slotFor_(deferred);
    if (!active || !deferred || deferredSlot == INVALID_SLOT) return;

    active->state = ProductPersistenceJobState::DEFERRED;
    deferred->state = ProductPersistenceJobState::ACTIVE;
    active_slot_ = deferredSlot;
}

void ProductPersistenceJobCoordinator::updateMetrics_(
    Record& record,
    const ProductPersistenceWorkUsage& usage
) {
    record.metrics.cumulativeBytes = saturatingAdd(
        record.metrics.cumulativeBytes,
        usage.bytes
    );
    record.metrics.cumulativeWallMicros = saturatingAdd(
        record.metrics.cumulativeWallMicros,
        usage.wallMicros
    );
    record.metrics.filesystemCalls = saturatingAdd(
        record.metrics.filesystemCalls,
        usage.filesystemCalls
    );
    record.metrics.entries = saturatingAdd(record.metrics.entries, usage.entries);
    record.metrics.advances = saturatingAdd(record.metrics.advances, uint16_t{1});
    record.metrics.allocations = saturatingAdd(
        record.metrics.allocations,
        usage.allocations
    );
    record.metrics.nodes = saturatingAdd(record.metrics.nodes, usage.nodes);
    if (usage.wallMicros > PRODUCT_PERSISTENCE_SOFT_ADVANCE_WALL_MICROS) {
        record.wallOverruns = saturatingAdd(record.wallOverruns, uint16_t{1});
    }
}

FLASHMEM void ProductPersistenceJobCoordinator::releaseRecord_(Record& record) {
    const uint8_t slot = slotFor_(&record);
    if (slot == INVALID_SLOT || record.state == ProductPersistenceJobState::EMPTY) {
        return;
    }

    const bool releasedActive = slot == active_slot_;
    record = {};
    if (depth_ > 0U) {
        --depth_;
    }

    if (!releasedActive) return;

    active_slot_ = INVALID_SLOT;
    for (uint8_t candidate = 0; candidate < 2U; ++candidate) {
        if (records_[candidate].state == ProductPersistenceJobState::DEFERRED) {
            records_[candidate].state = ProductPersistenceJobState::ACTIVE;
            active_slot_ = candidate;
            break;
        }
    }
}

}  // namespace core::persistence

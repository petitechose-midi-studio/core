#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/ProductConditionalMutationDigest.hpp"
#include "persistence/ProductFileCommitPlan.hpp"

namespace core::persistence::conditional_mutation {

enum class ConditionalPlanWorkClass : uint8_t {
    METADATA = 0,
    ORDINARY_IO,
    PROMOTION,
};

/** Preadmitted, allocation-free continuation for one conditional mutation. */
class ConditionalMutationPlan final {
public:
    oc::type::Result<void> begin(
        ProductFileService& files,
        ProductMutationLease&& lease,
        const Journal& journal
    );
    /** Borrow the sole RECOVERY lease; terminal completion never releases it. */
    oc::type::Result<void> beginRecovery(
        ProductFileService& files,
        const ProductMutationLease& lease,
        const Journal& journal
    );
    bool advance(ProductFileService& files, uint8_t* scratch, size_t scratchSize);
    void cancel(ProductFileService& files);

    bool active() const;
    bool terminal() const;
    bool recoveryRequired() const { return recovery_required_; }
    bool irreversible() const { return journal_started_ || promotion_.mapped(); }
    ConditionalPlanWorkClass nextWorkClass() const;
    Kind kind() const { return journal_.kind; }
    Status status() const { return status_; }
    Outcome outcome() const { return outcome_; }
    Subject subject() const { return subject_; }
    uint32_t operationId() const { return journal_.operationId; }
    const uint8_t* observedDigest() const {
        return has_observed_digest_ ? observed_digest_ : nullptr;
    }

private:
    enum class Step : uint8_t {
        IDLE = 0,
        DIGEST_CURRENT,
        CHECK_ALREADY_APPLIED_BACKUP,
        CLEAN_ALREADY_APPLIED_STAGING,
        DIGEST_STAGING,
        CHECK_BACKUP,
        WRITE_JOURNAL,
        REVALIDATE_CURRENT,
        REVALIDATE_STAGING,
        ADVANCE_PROMOTION,
        VERIFY_REPLACEMENT,
        CLEAN_MAPPED_STAGING,
        CLEAN_MAPPED_BACKUP,
        DELETE_MOVE_CURRENT,
        DIGEST_DELETE_BACKUP,
        DELETE_REMOVE_BACKUP,
        CLEAN_JOURNAL_STAGING,
        CLEAN_JOURNAL,
        RECOVERY_DIGEST_REPLACE_BACKUP,
        RECOVERY_RESTORE_BACKUP,
        COMPLETE,
        FAILED,
    };

    bool advanceDigestCurrent_(
        ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool advanceDigestStaging_(
        ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool advanceRevalidatedCurrent_(
        ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool advanceRevalidatedStaging_(
        ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool advanceVerifiedReplacement_(
        ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool advanceDeleteBackupDigest_(
        ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool finish_(
        ProductFileService& files,
        Status status,
        Outcome outcome,
        Subject subject,
        const uint8_t* observed,
        bool recoveryRequired
    );
    void release_(ProductFileService& files);
    const ProductMutationLease& leaseRef_() const;

    ProductFileCommitPlan promotion_{};
    Journal journal_{};
    DigestReadPlan digest_{};
    ProductMutationLease owned_lease_{};
    const ProductMutationLease* active_lease_ = nullptr;
    uint8_t observed_digest_[SHA256_SIZE] = {};
    uint32_t staging_size_ = 0U;
    Status status_ = Status::STORAGE_ERROR;
    Outcome outcome_ = Outcome::NONE;
    Subject subject_ = Subject::NONE;
    Step step_ = Step::IDLE;
    bool journal_started_ = false;
    bool recovery_required_ = false;
    bool has_observed_digest_ = false;
    bool recovery_mode_ = false;
    bool recovery_backup_source_ = false;
    Status cleanup_terminal_status_ = Status::OK;
};

static_assert(
    sizeof(ConditionalMutationPlan) <= 4'096U,
    "conditional mutation continuation exceeds PSRAM control ceiling"
);

}  // namespace core::persistence::conditional_mutation

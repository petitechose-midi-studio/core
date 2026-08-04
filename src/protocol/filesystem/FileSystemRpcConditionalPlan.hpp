#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/ProductFileCommitPlan.hpp"
#include "protocol/filesystem/FileSystemRpcConditionalTransaction.hpp"
#include "protocol/filesystem/FileSystemRpcDigest.hpp"

namespace core::protocol::filesystem::conditional_mutation {

enum class ConditionalPlanWorkClass : uint8_t {
    METADATA = 0,
    ORDINARY_IO,
    PROMOTION,
};

/**
 * Preadmitted, allocation-free continuation for one schema-1 conditional
 * replace/delete request. The object is placement-constructed in the retained
 * RPC request slot, which is owned in PSRAM on target.
 */
class ConditionalMutationPlan final {
public:
    oc::type::Result<void> begin(
        core::persistence::ProductFileService& files,
        core::persistence::ProductMutationLease&& lease,
        const Journal& journal
    );
    /** Borrow the sole RECOVERY lease; terminal completion never releases it. */
    oc::type::Result<void> beginRecovery(
        core::persistence::ProductFileService& files,
        const core::persistence::ProductMutationLease& lease,
        const Journal& journal
    );
    bool advance(
        core::persistence::ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    void cancel(core::persistence::ProductFileService& files);

    bool active() const;
    bool terminal() const;
    bool recoveryRequired() const { return recovery_required_; }
    bool irreversible() const { return journal_started_ || promotion_.mapped(); }
    ConditionalPlanWorkClass nextWorkClass() const;
    FileSystemRpcMessageId responseMessageId() const;
    FileSystemRpcStatus status() const { return status_; }
    FileSystemRpcMutationOutcome outcome() const { return outcome_; }
    FileSystemRpcMutationSubject subject() const { return subject_; }
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
        core::persistence::ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool advanceDigestStaging_(
        core::persistence::ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool advanceRevalidatedCurrent_(
        core::persistence::ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool advanceRevalidatedStaging_(
        core::persistence::ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool advanceVerifiedReplacement_(
        core::persistence::ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool advanceDeleteBackupDigest_(
        core::persistence::ProductFileService& files,
        uint8_t* scratch,
        size_t scratchSize
    );
    bool finish_(
        core::persistence::ProductFileService& files,
        FileSystemRpcStatus status,
        FileSystemRpcMutationOutcome outcome,
        FileSystemRpcMutationSubject subject,
        const uint8_t* observed,
        bool recoveryRequired
    );
    void release_(core::persistence::ProductFileService& files);
    const core::persistence::ProductMutationLease& leaseRef_() const;

    core::persistence::ProductFileCommitPlan promotion_{};
    Journal journal_{};
    DigestReadPlan digest_{};
    core::persistence::ProductMutationLease owned_lease_{};
    const core::persistence::ProductMutationLease* active_lease_ = nullptr;
    uint8_t observed_digest_[FILESYSTEM_RPC_SHA256_SIZE] = {};
    uint32_t staging_size_ = 0U;
    FileSystemRpcStatus status_ = FileSystemRpcStatus::STORAGE_ERROR;
    FileSystemRpcMutationOutcome outcome_ = FileSystemRpcMutationOutcome::NONE;
    FileSystemRpcMutationSubject subject_ = FileSystemRpcMutationSubject::NONE;
    Step step_ = Step::IDLE;
    bool journal_started_ = false;
    bool recovery_required_ = false;
    bool has_observed_digest_ = false;
    bool recovery_mode_ = false;
    bool recovery_backup_source_ = false;
    FileSystemRpcStatus cleanup_terminal_status_ = FileSystemRpcStatus::OK;
};

static_assert(
    sizeof(ConditionalMutationPlan) <= 4'096U,
    "conditional mutation continuation exceeds PSRAM control ceiling"
);

}  // namespace core::protocol::filesystem::conditional_mutation

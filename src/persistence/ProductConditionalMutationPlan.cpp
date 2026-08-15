#include "persistence/ProductConditionalMutationPlan.hpp"

#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

namespace core::persistence::conditional_mutation {

using oc::type::ErrorCode;

namespace {

const char kConditionalPlanActive[] PROGMEM =
    "conditional mutation continuation already active";
const char kConditionalPlanLease[] PROGMEM =
    "conditional mutation continuation lease invalid";
const char kConditionalPlanKind[] PROGMEM =
    "conditional mutation continuation kind invalid";

oc::type::Error planError(ErrorCode code, const char* context) {
    return {code, context};
}

bool backupIsUnexpected(
    const oc::type::Result<oc::interface::FileInfo>& backup
) {
    return backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND;
}

}  // namespace

FLASHMEM oc::type::Result<void> ConditionalMutationPlan::begin(
    ProductFileService& files,
    ProductMutationLease&& lease,
    const Journal& journal
) {
    if (active()) {
        return oc::type::Result<void>::err(
            planError(ErrorCode::INVALID_STATE, kConditionalPlanActive)
        );
    }
    if (!files.owns(lease)) {
        return oc::type::Result<void>::err(
            planError(ErrorCode::INVALID_STATE, kConditionalPlanLease)
        );
    }
    if (journal.kind != Kind::REPLACE && journal.kind != Kind::DELETE) {
        return oc::type::Result<void>::err(
            planError(ErrorCode::INVALID_ARGUMENT, kConditionalPlanKind)
        );
    }

    promotion_.reset();
    journal_ = journal;
    digest_.begin();
    owned_lease_ = std::move(lease);
    active_lease_ = &owned_lease_;
    std::memset(observed_digest_, 0, sizeof(observed_digest_));
    staging_size_ = 0U;
    status_ = Status::STORAGE_ERROR;
    outcome_ = Outcome::NONE;
    subject_ = Subject::NONE;
    step_ = Step::DIGEST_CURRENT;
    journal_started_ = false;
    recovery_required_ = false;
    has_observed_digest_ = false;
    recovery_mode_ = false;
    recovery_backup_source_ = false;
    cleanup_terminal_status_ = Status::OK;
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ConditionalMutationPlan::beginRecovery(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const Journal& journal
) {
    if (active()) {
        return oc::type::Result<void>::err(
            planError(ErrorCode::INVALID_STATE, kConditionalPlanActive)
        );
    }
    if (!files.owns(lease, ProductMutationOwner::RECOVERY)) {
        return oc::type::Result<void>::err(
            planError(ErrorCode::INVALID_STATE, kConditionalPlanLease)
        );
    }
    if (journal.kind != Kind::REPLACE && journal.kind != Kind::DELETE) {
        return oc::type::Result<void>::err(
            planError(ErrorCode::INVALID_ARGUMENT, kConditionalPlanKind)
        );
    }

    promotion_.reset();
    journal_ = journal;
    digest_.begin();
    owned_lease_ = ProductMutationLease{};
    active_lease_ = &lease;
    std::memset(observed_digest_, 0, sizeof(observed_digest_));
    staging_size_ = 0U;
    status_ = Status::STORAGE_ERROR;
    outcome_ = Outcome::NONE;
    subject_ = Subject::NONE;
    step_ = Step::DIGEST_CURRENT;
    journal_started_ = true;
    recovery_required_ = false;
    has_observed_digest_ = false;
    recovery_mode_ = true;
    recovery_backup_source_ = false;
    cleanup_terminal_status_ = Status::OK;
    return oc::type::Result<void>::ok();
}

FLASHMEM bool ConditionalMutationPlan::advance(
    ProductFileService& files,
    uint8_t* scratch,
    size_t scratchSize
) {
    if (!active() || active_lease_ == nullptr ||
        !files.owns(*active_lease_)) {
        return finish_(
            files,
            Status::STORAGE_ERROR,
            Outcome::NONE,
            Subject::NONE,
            nullptr,
            journal_started_
        );
    }

    switch (step_) {
        case Step::DIGEST_CURRENT:
            return advanceDigestCurrent_(files, scratch, scratchSize);
        case Step::CHECK_ALREADY_APPLIED_BACKUP: {
            if (recovery_mode_) {
                digest_.begin();
                step_ = journal_.kind == Kind::DELETE
                    ? Step::DIGEST_DELETE_BACKUP
                    : Step::CLEAN_MAPPED_STAGING;
                return false;
            }
            auto backup = files.stat(leaseRef_(), BACKUP_PATH);
            if (backupIsUnexpected(backup)) {
                return finish_(
                    files,
                    Status::INVALID_STATE,
                    Outcome::NONE,
                    Subject::NONE,
                    nullptr,
                    false
                );
            }
            if (journal_.kind == Kind::DELETE) {
                return finish_(
                    files,
                    Status::OK,
                    Outcome::ALREADY_APPLIED,
                    Subject::NONE,
                    nullptr,
                    false
                );
            }
            step_ = Step::CLEAN_ALREADY_APPLIED_STAGING;
            return false;
        }
        case Step::CLEAN_ALREADY_APPLIED_STAGING: {
            const auto status = removeIfExists(
                files,
                leaseRef_(),
                journal_.stagingPath
            );
            return finish_(
                files,
                status,
                status == Status::OK
                    ? Outcome::ALREADY_APPLIED
                    : Outcome::NONE,
                Subject::NONE,
                nullptr,
                false
            );
        }
        case Step::DIGEST_STAGING:
            return advanceDigestStaging_(files, scratch, scratchSize);
        case Step::CHECK_BACKUP: {
            auto backup = files.stat(leaseRef_(), BACKUP_PATH);
            if (backupIsUnexpected(backup)) {
                return finish_(
                    files,
                    Status::INVALID_STATE,
                    Outcome::NONE,
                    Subject::NONE,
                    nullptr,
                    false
                );
            }
            if (recovery_mode_) {
                if (journal_.kind == Kind::DELETE) {
                    step_ = Step::DELETE_MOVE_CURRENT;
                } else {
                    digest_.begin();
                    step_ = Step::REVALIDATE_CURRENT;
                }
            } else {
                step_ = Step::WRITE_JOURNAL;
            }
            return false;
        }
        case Step::WRITE_JOURNAL: {
            // Any failure after entering durable journal creation is
            // recovery-significant because a failed rename can be ambiguous.
            journal_started_ = true;
            const auto status = writeJournal(files, leaseRef_(), journal_);
            if (status != Status::OK) {
                return finish_(
                    files,
                    status,
                    Outcome::NONE,
                    Subject::NONE,
                    nullptr,
                    true
                );
            }
            digest_.begin();
            step_ = Step::REVALIDATE_CURRENT;
            return false;
        }
        case Step::REVALIDATE_CURRENT:
            return advanceRevalidatedCurrent_(files, scratch, scratchSize);
        case Step::REVALIDATE_STAGING:
            return advanceRevalidatedStaging_(files, scratch, scratchSize);
        case Step::ADVANCE_PROMOTION: {
            auto advanced = promotion_.advance(
                files,
                leaseRef_(),
                scratch,
                scratchSize
            );
            if (!advanced) {
                return finish_(
                    files,
                    statusFromError(advanced.error()),
                    Outcome::NONE,
                    Subject::NONE,
                    nullptr,
                    true
                );
            }
            if (!advanced.value()) return false;
            digest_.begin();
            step_ = Step::VERIFY_REPLACEMENT;
            return false;
        }
        case Step::VERIFY_REPLACEMENT:
            return advanceVerifiedReplacement_(files, scratch, scratchSize);
        case Step::CLEAN_MAPPED_STAGING: {
            const auto status = removeIfExists(
                files,
                leaseRef_(),
                journal_.stagingPath
            );
            if (status != Status::OK) {
                return finish_(
                    files,
                    status,
                    Outcome::NONE,
                    Subject::NONE,
                    nullptr,
                    true
                );
            }
            step_ = Step::CLEAN_MAPPED_BACKUP;
            return false;
        }
        case Step::CLEAN_MAPPED_BACKUP: {
            const auto status = removeIfExists(files, leaseRef_(), BACKUP_PATH);
            if (status != Status::OK) {
                return finish_(
                    files,
                    status,
                    Outcome::NONE,
                    Subject::NONE,
                    nullptr,
                    true
                );
            }
            step_ = Step::CLEAN_JOURNAL_STAGING;
            return false;
        }
        case Step::DELETE_MOVE_CURRENT: {
            auto moved = files.rename(
                leaseRef_(),
                journal_.currentPath,
                BACKUP_PATH
            );
            if (!moved) {
                return finish_(
                    files,
                    statusFromError(moved.error()),
                    Outcome::NONE,
                    Subject::NONE,
                    nullptr,
                    true
                );
            }
            digest_.begin();
            step_ = Step::DIGEST_DELETE_BACKUP;
            return false;
        }
        case Step::DIGEST_DELETE_BACKUP:
            return advanceDeleteBackupDigest_(files, scratch, scratchSize);
        case Step::DELETE_REMOVE_BACKUP: {
            auto removed = files.remove(leaseRef_(), BACKUP_PATH);
            if (!removed) {
                return finish_(
                    files,
                    statusFromError(removed.error()),
                    Outcome::NONE,
                    Subject::NONE,
                    nullptr,
                    true
                );
            }
            step_ = Step::CLEAN_JOURNAL_STAGING;
            return false;
        }
        case Step::CLEAN_JOURNAL_STAGING: {
            const auto status = removeIfExists(
                files,
                leaseRef_(),
                JOURNAL_STAGING_PATH
            );
            if (status != Status::OK) {
                return finish_(
                    files,
                    status,
                    Outcome::NONE,
                    Subject::NONE,
                    nullptr,
                    true
                );
            }
            step_ = Step::CLEAN_JOURNAL;
            return false;
        }
        case Step::CLEAN_JOURNAL: {
            const auto status = removeIfExists(files, leaseRef_(), JOURNAL_PATH);
            const auto terminalStatus = status == Status::OK
                ? cleanup_terminal_status_
                : status;
            return finish_(
                files,
                terminalStatus,
                terminalStatus == Status::OK
                    ? Outcome::APPLIED
                    : Outcome::NONE,
                Subject::NONE,
                nullptr,
                terminalStatus != Status::OK
            );
        }
        case Step::RECOVERY_DIGEST_REPLACE_BACKUP: {
            if (!digest_.advance(
                    files,
                    leaseRef_(),
                    BACKUP_PATH,
                    scratch,
                    scratchSize
                )) {
                return false;
            }
            const auto status = digest_.status();
            if (status != Status::OK ||
                !digestEquals(
                    digest_.digest(),
                    journal_.expectedSourceSha256
                )) {
                return finish_(
                    files,
                    status == Status::OK
                        ? Status::PRECONDITION_FAILED
                        : status,
                    Outcome::NONE,
                    Subject::NONE,
                    nullptr,
                    true
                );
            }
            recovery_backup_source_ = true;
            digest_.begin();
            step_ = Step::DIGEST_STAGING;
            return false;
        }
        case Step::RECOVERY_RESTORE_BACKUP: {
            auto restored = files.rename(
                leaseRef_(),
                BACKUP_PATH,
                journal_.currentPath
            );
            if (!restored) {
                return finish_(
                    files,
                    statusFromError(restored.error()),
                    Outcome::NONE,
                    Subject::NONE,
                    nullptr,
                    true
                );
            }
            step_ = Step::CLEAN_JOURNAL_STAGING;
            return false;
        }
        case Step::COMPLETE:
        case Step::FAILED:
            return true;
        case Step::IDLE:
        default:
            return finish_(
                files,
                Status::INVALID_STATE,
                Outcome::NONE,
                Subject::NONE,
                nullptr,
                journal_started_
            );
    }
}

FLASHMEM void ConditionalMutationPlan::cancel(
    ProductFileService& files
) {
    if (!active()) return;
    (void)finish_(
        files,
        Status::BUSY,
        Outcome::NONE,
        Subject::NONE,
        nullptr,
        journal_started_ || promotion_.mapped()
    );
}

bool ConditionalMutationPlan::active() const {
    return step_ != Step::IDLE && step_ != Step::COMPLETE &&
           step_ != Step::FAILED;
}

bool ConditionalMutationPlan::terminal() const {
    return step_ == Step::COMPLETE || step_ == Step::FAILED;
}

ConditionalPlanWorkClass ConditionalMutationPlan::nextWorkClass() const {
    switch (step_) {
        case Step::DIGEST_CURRENT:
        case Step::DIGEST_STAGING:
        case Step::REVALIDATE_CURRENT:
        case Step::REVALIDATE_STAGING:
        case Step::VERIFY_REPLACEMENT:
        case Step::DIGEST_DELETE_BACKUP:
        case Step::RECOVERY_DIGEST_REPLACE_BACKUP:
            return digest_.nextAdvanceReadsData()
                ? ConditionalPlanWorkClass::ORDINARY_IO
                : ConditionalPlanWorkClass::METADATA;
        case Step::CHECK_ALREADY_APPLIED_BACKUP:
        case Step::CHECK_BACKUP:
            return ConditionalPlanWorkClass::METADATA;
        case Step::ADVANCE_PROMOTION:
            return promotion_.nextAdvanceReadsData()
                ? ConditionalPlanWorkClass::ORDINARY_IO
                : ConditionalPlanWorkClass::PROMOTION;
        case Step::IDLE:
        case Step::COMPLETE:
        case Step::FAILED:
        case Step::CLEAN_ALREADY_APPLIED_STAGING:
        case Step::WRITE_JOURNAL:
        case Step::CLEAN_MAPPED_STAGING:
        case Step::CLEAN_MAPPED_BACKUP:
        case Step::DELETE_MOVE_CURRENT:
        case Step::DELETE_REMOVE_BACKUP:
        case Step::CLEAN_JOURNAL_STAGING:
        case Step::CLEAN_JOURNAL:
        case Step::RECOVERY_RESTORE_BACKUP:
        default:
            return ConditionalPlanWorkClass::PROMOTION;
    }
}


FLASHMEM bool ConditionalMutationPlan::advanceDigestCurrent_(
    ProductFileService& files,
    uint8_t* scratch,
    size_t scratchSize
) {
    if (!digest_.advance(
            files,
            leaseRef_(),
            journal_.currentPath,
            scratch,
            scratchSize
        )) {
        return false;
    }

    const auto status = digest_.status();
    if (journal_.kind == Kind::REPLACE) {
        if (status == Status::OK &&
            digestEquals(digest_.digest(), journal_.replacementSha256)) {
            step_ = recovery_mode_
                ? Step::CLEAN_MAPPED_STAGING
                : Step::CHECK_ALREADY_APPLIED_BACKUP;
            return false;
        }
        if (recovery_mode_ && status == Status::NOT_FOUND) {
            digest_.begin();
            step_ = Step::RECOVERY_DIGEST_REPLACE_BACKUP;
            return false;
        }
        if (status != Status::OK ||
            !digestEquals(digest_.digest(), journal_.expectedSourceSha256)) {
            return finish_(
                files,
                status == Status::OK
                    ? Status::PRECONDITION_FAILED
                    : status,
                Outcome::NONE,
                Subject::SOURCE,
                status == Status::OK ? digest_.digest() : nullptr,
                false
            );
        }
        digest_.begin();
        step_ = Step::DIGEST_STAGING;
        return false;
    }

    if (status == Status::NOT_FOUND) {
        if (recovery_mode_) {
            digest_.begin();
            step_ = Step::DIGEST_DELETE_BACKUP;
        } else {
            step_ = Step::CHECK_ALREADY_APPLIED_BACKUP;
        }
        return false;
    }
    if (status != Status::OK ||
        !digestEquals(digest_.digest(), journal_.expectedSourceSha256)) {
        return finish_(
            files,
            status == Status::OK
                ? Status::PRECONDITION_FAILED
                : status,
            Outcome::NONE,
            Subject::SOURCE,
            status == Status::OK ? digest_.digest() : nullptr,
            false
        );
    }
    step_ = Step::CHECK_BACKUP;
    return false;
}

FLASHMEM bool ConditionalMutationPlan::advanceDigestStaging_(
    ProductFileService& files,
    uint8_t* scratch,
    size_t scratchSize
) {
    if (!digest_.advance(
            files,
            leaseRef_(),
            journal_.stagingPath,
            scratch,
            scratchSize
        )) {
        return false;
    }
    const auto status = digest_.status();
    if (status != Status::OK ||
        !digestEquals(digest_.digest(), journal_.replacementSha256)) {
        if (recovery_mode_ && recovery_backup_source_) {
            cleanup_terminal_status_ = Status::STORAGE_ERROR;
            step_ = Step::RECOVERY_RESTORE_BACKUP;
            return false;
        }
        return finish_(
            files,
            status == Status::OK
                ? Status::PRECONDITION_FAILED
                : status,
            Outcome::NONE,
            Subject::STAGING,
            status == Status::OK ? digest_.digest() : nullptr,
            false
        );
    }
    staging_size_ = digest_.fileSize();
    if (recovery_mode_) {
        digest_.begin();
        step_ = Step::REVALIDATE_CURRENT;
    } else {
        step_ = Step::CHECK_BACKUP;
    }
    return false;
}

FLASHMEM bool ConditionalMutationPlan::advanceRevalidatedCurrent_(
    ProductFileService& files,
    uint8_t* scratch,
    size_t scratchSize
) {
    if (!digest_.advance(
            files,
            leaseRef_(),
            journal_.currentPath,
            scratch,
            scratchSize
        )) {
        return false;
    }
    const auto status = digest_.status();
    if (journal_.kind == Kind::REPLACE) {
        if (status == Status::OK &&
            digestEquals(digest_.digest(), journal_.replacementSha256)) {
            step_ = Step::CLEAN_MAPPED_STAGING;
            return false;
        }
        if (recovery_mode_ && recovery_backup_source_ &&
            status == Status::NOT_FOUND) {
            digest_.begin();
            step_ = Step::REVALIDATE_STAGING;
            return false;
        }
        if (status != Status::OK ||
            !digestEquals(digest_.digest(), journal_.expectedSourceSha256)) {
            return finish_(
                files,
                status == Status::OK
                    ? Status::PRECONDITION_FAILED
                    : status,
                Outcome::NONE,
                Subject::NONE,
                nullptr,
                true
            );
        }
        digest_.begin();
        step_ = Step::REVALIDATE_STAGING;
        return false;
    }

    if (status == Status::NOT_FOUND) {
        digest_.begin();
        step_ = Step::DIGEST_DELETE_BACKUP;
        return false;
    }
    if (status != Status::OK ||
        !digestEquals(digest_.digest(), journal_.expectedSourceSha256)) {
        return finish_(
            files,
            status == Status::OK
                ? Status::PRECONDITION_FAILED
                : status,
            Outcome::NONE,
            Subject::NONE,
            nullptr,
            true
        );
    }
    step_ = Step::DELETE_MOVE_CURRENT;
    return false;
}

FLASHMEM bool ConditionalMutationPlan::advanceRevalidatedStaging_(
    ProductFileService& files,
    uint8_t* scratch,
    size_t scratchSize
) {
    if (!digest_.advance(
            files,
            leaseRef_(),
            journal_.stagingPath,
            scratch,
            scratchSize
        )) {
        return false;
    }
    if (digest_.status() != Status::OK ||
        !digestEquals(digest_.digest(), journal_.replacementSha256)) {
        return finish_(
            files,
            digest_.status() == Status::OK
                ? Status::PRECONDITION_FAILED
                : digest_.status(),
            Outcome::NONE,
            Subject::NONE,
            nullptr,
            true
        );
    }
    auto begun = promotion_.begin(
        files,
        leaseRef_(),
        journal_.currentPath,
        BACKUP_PATH,
        journal_.stagingPath,
        staging_size_,
        digest_.crc32()
    );
    if (!begun) {
        return finish_(
            files,
            statusFromError(begun.error()),
            Outcome::NONE,
            Subject::NONE,
            nullptr,
            true
        );
    }
    step_ = Step::ADVANCE_PROMOTION;
    return false;
}

FLASHMEM bool ConditionalMutationPlan::advanceVerifiedReplacement_(
    ProductFileService& files,
    uint8_t* scratch,
    size_t scratchSize
) {
    if (!digest_.advance(
            files,
            leaseRef_(),
            journal_.currentPath,
            scratch,
            scratchSize
        )) {
        return false;
    }
    if (digest_.status() != Status::OK ||
        !digestEquals(digest_.digest(), journal_.replacementSha256)) {
        return finish_(
            files,
            Status::STORAGE_ERROR,
            Outcome::NONE,
            Subject::NONE,
            nullptr,
            true
        );
    }
    step_ = Step::CLEAN_JOURNAL_STAGING;
    return false;
}

FLASHMEM bool ConditionalMutationPlan::advanceDeleteBackupDigest_(
    ProductFileService& files,
    uint8_t* scratch,
    size_t scratchSize
) {
    if (!digest_.advance(
            files,
            leaseRef_(),
            BACKUP_PATH,
            scratch,
            scratchSize
        )) {
        return false;
    }
    if (digest_.status() == Status::NOT_FOUND) {
        step_ = Step::CLEAN_JOURNAL_STAGING;
        return false;
    }
    if (digest_.status() != Status::OK ||
        !digestEquals(digest_.digest(), journal_.expectedSourceSha256)) {
        return finish_(
            files,
            digest_.status() == Status::OK
                ? Status::PRECONDITION_FAILED
                : digest_.status(),
            Outcome::NONE,
            Subject::NONE,
            nullptr,
            true
        );
    }
    step_ = Step::DELETE_REMOVE_BACKUP;
    return false;
}

FLASHMEM bool ConditionalMutationPlan::finish_(
    ProductFileService& files,
    Status status,
    Outcome outcome,
    Subject subject,
    const uint8_t* observed,
    bool recoveryRequired
) {
    status_ = status;
    outcome_ = outcome;
    subject_ = subject;
    recovery_required_ = recovery_required_ || recoveryRequired;
    has_observed_digest_ = observed != nullptr;
    if (observed) copyDigest(observed_digest_, observed);
    release_(files);
    step_ = status_ == Status::OK ? Step::COMPLETE : Step::FAILED;
    return true;
}

FLASHMEM void ConditionalMutationPlan::release_(
    ProductFileService& files
) {
    if (active_lease_ != nullptr && active_lease_->valid() &&
        files.owns(*active_lease_)) {
        if (recovery_mode_) {
            active_lease_ = nullptr;
            return;
        }
        if (recovery_required_) {
            (void)files.requireRecovery(
                owned_lease_,
                recoveryError(status_)
            );
        }
        auto released = files.releaseMutation(owned_lease_);
        active_lease_ = nullptr;
        if (!released && status_ == Status::OK) {
            status_ = statusFromError(released.error());
            outcome_ = Outcome::NONE;
            recovery_required_ = true;
        }
    } else {
        owned_lease_ = ProductMutationLease{};
        active_lease_ = nullptr;
        if (status_ == Status::OK) {
            status_ = Status::STORAGE_ERROR;
            outcome_ = Outcome::NONE;
            recovery_required_ = true;
        }
    }
}

const ProductMutationLease&
ConditionalMutationPlan::leaseRef_() const {
    return *active_lease_;
}

}  // namespace core::persistence::conditional_mutation

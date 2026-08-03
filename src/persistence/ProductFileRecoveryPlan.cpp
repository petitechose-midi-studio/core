#include "persistence/ProductFileRecoveryPlan.hpp"

#include <config/PlatformCompat.hpp>

namespace core::persistence {

namespace transaction = product_file_transaction;
using oc::type::ErrorCode;

namespace {

const char kRecoveryPlanActive[] PROGMEM =
    "ordinary recovery continuation already active";
const char kRecoveryPlanLease[] PROGMEM =
    "exact product recovery lease required";
const char kRecoveryPlanState[] PROGMEM =
    "ordinary recovery continuation is not active";
const char kRolledBackMissing[] PROGMEM =
    "rolled-back product file is missing";
const char kUnexpectedRolledBack[] PROGMEM =
    "unexpected rolled-back product file";
const char kAmbiguousTemporary[] PROGMEM =
    "ambiguous product file and temporary";
const char kInvalidTemporary[] PROGMEM =
    "invalid product file temporary";
const char kPromotedSize[] PROGMEM =
    "promoted product file size mismatch";
const char kLostTopology[] PROGMEM =
    "product file transaction lost old and new";

oc::type::Error recoveryError(ErrorCode code, const char* context) {
    return {code, context};
}

}  // namespace

FLASHMEM oc::type::Result<void> ProductFileRecoveryPlan::begin(
    ProductFileService& files,
    const ProductMutationLease& recoveryLease
) {
    if (active()) {
        return oc::type::Result<void>::err(
            recoveryError(ErrorCode::INVALID_STATE, kRecoveryPlanActive)
        );
    }
    reset();
    if (!files.owns(recoveryLease, ProductMutationOwner::RECOVERY)) {
        return oc::type::Result<void>::err(
            recoveryError(ErrorCode::INVALID_STATE, kRecoveryPlanLease)
        );
    }
    step_ = Step::SELECT_JOURNAL;
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<bool> ProductFileRecoveryPlan::advance(
    ProductFileService& files,
    const ProductMutationLease& recoveryLease
) {
    if (!active() ||
        !files.owns(recoveryLease, ProductMutationOwner::RECOVERY)) {
        return fail_(recoveryError(ErrorCode::INVALID_STATE, kRecoveryPlanState));
    }

    switch (step_) {
        case Step::SELECT_JOURNAL: {
            auto selected = transaction::selectLatest(
                files,
                recoveryLease,
                workspace_
            );
            if (!selected) return fail_(selected.error());
            if (!selected.value().present) {
                step_ = Step::COMPLETE;
                return oc::type::Result<bool>::ok(true);
            }
            step_ = Step::INSPECT_FINAL;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::INSPECT_FINAL: {
            auto value = transaction::inspectFile(
                files,
                recoveryLease,
                workspace_.path(transaction::FINAL_PATH)
            );
            if (!value) return fail_(value.error());
            final_ = value.value();
            step_ = Step::INSPECT_TMP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::INSPECT_TMP: {
            auto value = transaction::inspectFile(
                files,
                recoveryLease,
                workspace_.path(transaction::TMP_PATH)
            );
            if (!value) return fail_(value.error());
            tmp_ = value.value();
            step_ = Step::INSPECT_BACKUP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::INSPECT_BACKUP: {
            auto value = transaction::inspectFile(
                files,
                recoveryLease,
                workspace_.path(transaction::BACKUP_PATH)
            );
            if (!value) return fail_(value.error());
            backup_ = value.value();
            return decide_();
        }
        case Step::REMOVE_CURRENT: {
            auto removed = files.remove(
                recoveryLease,
                workspace_.path(transaction::FINAL_PATH)
            );
            if (!removed) return fail_(removed.error());
            step_ = Step::RESTORE_BACKUP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::RESTORE_BACKUP: {
            auto restored = files.rename(
                recoveryLease,
                workspace_.path(transaction::BACKUP_PATH),
                workspace_.path(transaction::FINAL_PATH)
            );
            if (!restored) return fail_(restored.error());
            step_ = Step::FLUSH_RESTORED;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::FLUSH_RESTORED: {
            auto flushed = files.flush(
                recoveryLease,
                workspace_.path(transaction::FINAL_PATH)
            );
            if (!flushed) return fail_(flushed.error());
            return finish_(ProductFileTransactionPhase::ROLLED_BACK);
        }
        case Step::BACK_UP_CURRENT: {
            auto renamed = files.rename(
                recoveryLease,
                workspace_.path(transaction::FINAL_PATH),
                workspace_.path(transaction::BACKUP_PATH)
            );
            if (!renamed) return fail_(renamed.error());
            step_ = Step::FLUSH_BACKUP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::FLUSH_BACKUP: {
            auto flushed = files.flush(
                recoveryLease,
                workspace_.path(transaction::BACKUP_PATH)
            );
            if (!flushed) return fail_(flushed.error());
            step_ = Step::PERSIST_BACKED_UP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::PERSIST_BACKED_UP: {
            auto persisted = transaction::persistPhase(
                files,
                recoveryLease,
                workspace_,
                ProductFileTransactionPhase::BACKED_UP
            );
            if (!persisted) return fail_(persisted.error());
            return promoteTemporary_();
        }
        case Step::PROMOTE_TMP: {
            auto promoted = files.rename(
                recoveryLease,
                workspace_.path(transaction::TMP_PATH),
                workspace_.path(transaction::FINAL_PATH)
            );
            if (!promoted) return fail_(promoted.error());
            step_ = Step::FLUSH_PROMOTED;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::FLUSH_PROMOTED: {
            auto flushed = files.flush(
                recoveryLease,
                workspace_.path(transaction::FINAL_PATH)
            );
            if (!flushed) return fail_(flushed.error());
            step_ = Step::PERSIST_PROMOTED;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::PERSIST_PROMOTED: {
            auto persisted = transaction::persistPhase(
                files,
                recoveryLease,
                workspace_,
                ProductFileTransactionPhase::PROMOTED
            );
            if (!persisted) return fail_(persisted.error());
            return finish_(ProductFileTransactionPhase::COMMITTED);
        }
        case Step::CLEAN_TMP: {
            auto removed = transaction::cleanupMappedPath(
                files,
                recoveryLease,
                workspace_.path(transaction::TMP_PATH)
            );
            if (!removed) return fail_(removed.error());
            step_ = Step::CLEAN_BACKUP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::CLEAN_BACKUP: {
            auto removed = transaction::cleanupMappedPath(
                files,
                recoveryLease,
                workspace_.path(transaction::BACKUP_PATH)
            );
            if (!removed) return fail_(removed.error());
            if (workspace_.phase == terminal_phase_) {
                step_ = Step::COMPLETE;
                return oc::type::Result<bool>::ok(true);
            }
            step_ = Step::PERSIST_TERMINAL;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::PERSIST_TERMINAL: {
            auto persisted = transaction::persistPhase(
                files,
                recoveryLease,
                workspace_,
                terminal_phase_
            );
            if (!persisted) return fail_(persisted.error());
            step_ = Step::COMPLETE;
            return oc::type::Result<bool>::ok(true);
        }
        case Step::COMPLETE:
            return oc::type::Result<bool>::ok(true);
        case Step::IDLE:
        case Step::FAILED:
        default:
            return fail_(recoveryError(ErrorCode::INVALID_STATE, kRecoveryPlanState));
    }
}

FLASHMEM void ProductFileRecoveryPlan::reset() {
    workspace_ = {};
    final_ = {};
    tmp_ = {};
    backup_ = {};
    terminal_phase_ = ProductFileTransactionPhase::NONE;
    step_ = Step::IDLE;
}

FLASHMEM bool ProductFileRecoveryPlan::active() const {
    return step_ != Step::IDLE && step_ != Step::COMPLETE &&
           step_ != Step::FAILED;
}

FLASHMEM bool ProductFileRecoveryPlan::complete() const {
    return step_ == Step::COMPLETE;
}

FLASHMEM oc::type::Result<bool> ProductFileRecoveryPlan::decide_() {
    const bool finalExact = final_.exists && final_.size == workspace_.expectedSize;
    const bool tmpExact = tmp_.exists && tmp_.size == workspace_.expectedSize;

    if (workspace_.phase == ProductFileTransactionPhase::ROLLED_BACK) {
        if (workspace_.hadCurrent) {
            if (final_.exists) return finish_(ProductFileTransactionPhase::ROLLED_BACK);
            if (backup_.exists) return restoreBackup_(false);
            return fail_(recoveryError(ErrorCode::STORAGE_CORRUPT, kRolledBackMissing));
        }
        if (!final_.exists) return finish_(ProductFileTransactionPhase::ROLLED_BACK);
        if (finalExact) return finish_(ProductFileTransactionPhase::COMMITTED);
        return fail_(recoveryError(ErrorCode::STORAGE_CORRUPT, kUnexpectedRolledBack));
    }

    if (workspace_.phase == ProductFileTransactionPhase::COMMITTED && finalExact) {
        return finish_(ProductFileTransactionPhase::COMMITTED);
    }

    if (final_.exists && tmp_.exists) {
        if (workspace_.phase == ProductFileTransactionPhase::PREPARED &&
            workspace_.hadCurrent && !backup_.exists && tmpExact) {
            step_ = Step::BACK_UP_CURRENT;
            return oc::type::Result<bool>::ok(false);
        }
        if (transaction::phaseTerminal(workspace_.phase)) {
            return workspace_.phase == ProductFileTransactionPhase::COMMITTED &&
                           finalExact
                ? finish_(ProductFileTransactionPhase::COMMITTED)
                : finish_(ProductFileTransactionPhase::ROLLED_BACK);
        }
        return fail_(recoveryError(ErrorCode::STORAGE_CORRUPT, kAmbiguousTemporary));
    }

    if (!final_.exists && tmp_.exists) {
        if (!tmpExact) {
            if (backup_.exists) return restoreBackup_(false);
            if (!workspace_.hadCurrent) {
                return finish_(ProductFileTransactionPhase::ROLLED_BACK);
            }
            return fail_(recoveryError(ErrorCode::STORAGE_CORRUPT, kInvalidTemporary));
        }
        return promoteTemporary_();
    }

    if (final_.exists && !tmp_.exists) {
        if (backup_.exists) {
            return finalExact
                ? finish_(ProductFileTransactionPhase::COMMITTED)
                : restoreBackup_(true);
        }
        if (workspace_.phase == ProductFileTransactionPhase::PREPARED &&
            workspace_.hadCurrent) {
            return finish_(ProductFileTransactionPhase::ROLLED_BACK);
        }
        if (finalExact) return finish_(ProductFileTransactionPhase::COMMITTED);
        return fail_(recoveryError(ErrorCode::STORAGE_CORRUPT, kPromotedSize));
    }

    if (backup_.exists) return restoreBackup_(false);
    if (!workspace_.hadCurrent) {
        return finish_(ProductFileTransactionPhase::ROLLED_BACK);
    }
    return fail_(recoveryError(ErrorCode::STORAGE_CORRUPT, kLostTopology));
}

FLASHMEM oc::type::Result<bool> ProductFileRecoveryPlan::fail_(
    oc::type::Error error
) {
    step_ = Step::FAILED;
    return oc::type::Result<bool>::err(error);
}

FLASHMEM oc::type::Result<bool> ProductFileRecoveryPlan::finish_(
    ProductFileTransactionPhase phase
) {
    terminal_phase_ = phase;
    step_ = Step::CLEAN_TMP;
    return oc::type::Result<bool>::ok(false);
}

FLASHMEM oc::type::Result<bool> ProductFileRecoveryPlan::restoreBackup_(
    bool removeCurrent
) {
    terminal_phase_ = ProductFileTransactionPhase::ROLLED_BACK;
    step_ = removeCurrent ? Step::REMOVE_CURRENT : Step::RESTORE_BACKUP;
    return oc::type::Result<bool>::ok(false);
}

FLASHMEM oc::type::Result<bool> ProductFileRecoveryPlan::promoteTemporary_() {
    terminal_phase_ = ProductFileTransactionPhase::COMMITTED;
    step_ = Step::PROMOTE_TMP;
    return oc::type::Result<bool>::ok(false);
}

}  // namespace core::persistence

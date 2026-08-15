#include "persistence/ProductFileRecoveryPlan.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceChecksum.hpp"

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
const char kLostTopology[] PROGMEM =
    "product file transaction lost old and new";
const char kRecoveryIntegrityScratch[] PROGMEM =
    "product file recovery integrity scratch unavailable";
const char kRecoveryIntegrityShortRead[] PROGMEM =
    "short product file recovery integrity read";

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
    const ProductMutationLease& recoveryLease,
    uint8_t* scratch,
    size_t scratchSize
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
            return beginNextIntegrityCheck_();
        }
        case Step::VERIFY_FINAL: {
            auto verified = advanceIntegrityCheck_(
                files,
                recoveryLease,
                workspace_.path(transaction::FINAL_PATH),
                scratch,
                scratchSize
            );
            if (!verified) return fail_(verified.error());
            if (!verified.value()) return oc::type::Result<bool>::ok(false);
            final_valid_ =
                checksum::crc32Finish(integrity_crc_state_) ==
                workspace_.expectedCrc32;
            if (tmp_.exists && tmp_.size == workspace_.expectedSize) {
                beginIntegrityCheck_();
                step_ = Step::VERIFY_TMP;
                return oc::type::Result<bool>::ok(false);
            }
            return decide_();
        }
        case Step::VERIFY_TMP: {
            auto verified = advanceIntegrityCheck_(
                files,
                recoveryLease,
                workspace_.path(transaction::TMP_PATH),
                scratch,
                scratchSize
            );
            if (!verified) return fail_(verified.error());
            if (!verified.value()) return oc::type::Result<bool>::ok(false);
            tmp_valid_ =
                checksum::crc32Finish(integrity_crc_state_) ==
                workspace_.expectedCrc32;
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
        case Step::REMOVE_UNVERIFIED_CURRENT: {
            auto removed = files.remove(
                recoveryLease,
                workspace_.path(transaction::FINAL_PATH)
            );
            if (!removed) return fail_(removed.error());
            return finish_(ProductFileTransactionPhase::ROLLED_BACK);
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
            backup_ = final_;
            final_ = {};
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
            final_ = tmp_;
            tmp_ = {};
            step_ = Step::FLUSH_PROMOTED;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::FLUSH_PROMOTED: {
            auto flushed = files.flush(
                recoveryLease,
                workspace_.path(transaction::FINAL_PATH)
            );
            if (!flushed) return fail_(flushed.error());
            beginIntegrityCheck_();
            step_ = Step::VERIFY_PROMOTED;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::VERIFY_PROMOTED: {
            auto verified = advanceIntegrityCheck_(
                files,
                recoveryLease,
                workspace_.path(transaction::FINAL_PATH),
                scratch,
                scratchSize
            );
            if (!verified) return fail_(verified.error());
            if (!verified.value()) return oc::type::Result<bool>::ok(false);
            if (checksum::crc32Finish(integrity_crc_state_) !=
                workspace_.expectedCrc32) {
                if (backup_.exists) return restoreBackup_(true);
                if (!workspace_.hadCurrent) {
                    terminal_phase_ = ProductFileTransactionPhase::ROLLED_BACK;
                    step_ = Step::REMOVE_UNVERIFIED_CURRENT;
                    return oc::type::Result<bool>::ok(false);
                }
                return fail_(
                    recoveryError(ErrorCode::STORAGE_CORRUPT, kLostTopology)
                );
            }
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
    integrity_crc_state_ = 0U;
    integrity_offset_ = 0U;
    step_ = Step::IDLE;
    final_valid_ = false;
    tmp_valid_ = false;
}

FLASHMEM bool ProductFileRecoveryPlan::active() const {
    return step_ != Step::IDLE && step_ != Step::COMPLETE &&
           step_ != Step::FAILED;
}

FLASHMEM bool ProductFileRecoveryPlan::complete() const {
    return step_ == Step::COMPLETE;
}

FLASHMEM oc::type::Result<bool> ProductFileRecoveryPlan::decide_() {
    switch (transaction::decideRecovery(
        workspace_,
        final_,
        final_valid_,
        tmp_,
        tmp_valid_,
        backup_
    )) {
        case transaction::RecoveryAction::FINISH_COMMITTED:
            return finish_(ProductFileTransactionPhase::COMMITTED);
        case transaction::RecoveryAction::FINISH_ROLLED_BACK:
            return finish_(ProductFileTransactionPhase::ROLLED_BACK);
        case transaction::RecoveryAction::RESTORE_BACKUP:
            return restoreBackup_(false);
        case transaction::RecoveryAction::REMOVE_CURRENT_AND_RESTORE_BACKUP:
            return restoreBackup_(true);
        case transaction::RecoveryAction::BACK_UP_CURRENT_AND_PROMOTE_TMP:
            step_ = Step::BACK_UP_CURRENT;
            return oc::type::Result<bool>::ok(false);
        case transaction::RecoveryAction::PROMOTE_TMP:
            return promoteTemporary_();
        case transaction::RecoveryAction::REMOVE_CURRENT_AND_ROLL_BACK:
            terminal_phase_ = ProductFileTransactionPhase::ROLLED_BACK;
            step_ = Step::REMOVE_UNVERIFIED_CURRENT;
            return oc::type::Result<bool>::ok(false);
        case transaction::RecoveryAction::FAIL_CORRUPT:
        default:
            return fail_(recoveryError(ErrorCode::STORAGE_CORRUPT, kLostTopology));
    }
}

FLASHMEM void ProductFileRecoveryPlan::beginIntegrityCheck_() {
    integrity_crc_state_ = checksum::CRC32_INITIAL_STATE;
    integrity_offset_ = 0U;
}

FLASHMEM oc::type::Result<bool> ProductFileRecoveryPlan::advanceIntegrityCheck_(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path,
    uint8_t* scratch,
    size_t scratchSize
) {
    if (integrity_offset_ == workspace_.expectedSize) {
        return oc::type::Result<bool>::ok(true);
    }
    if (scratch == nullptr || scratchSize == 0U) {
        return oc::type::Result<bool>::err(
            recoveryError(ErrorCode::INVALID_ARGUMENT, kRecoveryIntegrityScratch)
        );
    }
    const size_t remaining = workspace_.expectedSize - integrity_offset_;
    const size_t requested = std::min(
        remaining,
        std::min(scratchSize, PRODUCT_FILE_INTEGRITY_CHUNK_SIZE)
    );
    auto read = files.read(
        lease,
        path,
        integrity_offset_,
        scratch,
        requested
    );
    if (!read) return oc::type::Result<bool>::err(read.error());
    if (read.value() == 0U || read.value() > requested) {
        return oc::type::Result<bool>::err(
            recoveryError(ErrorCode::STORAGE_READ_FAILED,
                          kRecoveryIntegrityShortRead)
        );
    }
    integrity_crc_state_ = checksum::crc32Update(
        integrity_crc_state_,
        scratch,
        read.value()
    );
    integrity_offset_ += static_cast<uint32_t>(read.value());
    return oc::type::Result<bool>::ok(
        integrity_offset_ == workspace_.expectedSize
    );
}

FLASHMEM oc::type::Result<bool>
ProductFileRecoveryPlan::beginNextIntegrityCheck_() {
    if (final_.exists && final_.size == workspace_.expectedSize) {
        beginIntegrityCheck_();
        step_ = Step::VERIFY_FINAL;
        return oc::type::Result<bool>::ok(false);
    }
    if (tmp_.exists && tmp_.size == workspace_.expectedSize) {
        beginIntegrityCheck_();
        step_ = Step::VERIFY_TMP;
        return oc::type::Result<bool>::ok(false);
    }
    return decide_();
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

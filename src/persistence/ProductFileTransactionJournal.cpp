#include "persistence/AtomicProductFile.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceChecksum.hpp"
#include "persistence/ProductFileTransactionJournalInternal.hpp"

namespace core::persistence {

using oc::type::ErrorCode;
using product_file_transaction::BACKUP_PATH;
using product_file_transaction::cleanupMappedPath;
using product_file_transaction::FileState;
using product_file_transaction::FINAL_PATH;
using product_file_transaction::inspectFile;
using product_file_transaction::JournalWorkspace;
using product_file_transaction::normalizePaths;
using product_file_transaction::persistPhase;
using product_file_transaction::phaseTerminal;
using product_file_transaction::selectLatest;
using product_file_transaction::TMP_PATH;

namespace {

// Synchronous asset and legacy boot recovery share the global mutation lease,
// so one cold PSRAM scratch is sufficient and avoids a 512-byte RAM1 stack
// spike. Cooperative Project/RPC/recovery paths retain and supply their own
// PSRAM scratch instead.
EXTMEM uint8_t synchronousIntegrityScratch[PRODUCT_FILE_INTEGRITY_CHUNK_SIZE] = {};
static_assert(sizeof(synchronousIntegrityScratch) == 512U);

FLASHMEM oc::type::Result<bool> payloadMatches(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path,
    uint32_t expectedSize,
    uint32_t expectedCrc32
) {
    uint32_t state = checksum::CRC32_INITIAL_STATE;
    uint32_t offset = 0U;
    while (offset < expectedSize) {
        const size_t requested = std::min<size_t>(
            expectedSize - offset,
            sizeof(synchronousIntegrityScratch)
        );
        auto read = files.read(
            lease,
            path,
            offset,
            synchronousIntegrityScratch,
            requested
        );
        if (!read) return oc::type::Result<bool>::err(read.error());
        if (read.value() == 0U || read.value() > requested) {
            return oc::type::Result<bool>::err(
                {ErrorCode::STORAGE_READ_FAILED,
                 "short product file integrity read"}
            );
        }
        state = checksum::crc32Update(
            state,
            synchronousIntegrityScratch,
            read.value()
        );
        offset += static_cast<uint32_t>(read.value());
    }
    return oc::type::Result<bool>::ok(
        checksum::crc32Finish(state) == expectedCrc32
    );
}

FLASHMEM oc::type::Result<void> finishCommitted(
    ProductFileService& files,
    const ProductMutationLease& lease,
    JournalWorkspace& workspace
) {
    auto tmpCleanup = cleanupMappedPath(files, lease, workspace.path(TMP_PATH));
    if (!tmpCleanup) {
        return oc::type::Result<void>::err(
            tmpCleanup.error()
        );
    }
    auto backupCleanup = cleanupMappedPath(files, lease, workspace.path(BACKUP_PATH));
    if (!backupCleanup) {
        return oc::type::Result<void>::err(
            backupCleanup.error()
        );
    }
    if (workspace.phase != ProductFileTransactionPhase::COMMITTED) {
        auto persisted = persistPhase(
            files,
            lease,
            workspace,
            ProductFileTransactionPhase::COMMITTED
        );
        if (!persisted) {
            return oc::type::Result<void>::err(
                persisted.error()
            );
        }
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> finishRolledBack(
    ProductFileService& files,
    const ProductMutationLease& lease,
    JournalWorkspace& workspace
) {
    auto tmpCleanup = cleanupMappedPath(files, lease, workspace.path(TMP_PATH));
    if (!tmpCleanup) {
        return oc::type::Result<void>::err(
            tmpCleanup.error()
        );
    }
    auto backupCleanup = cleanupMappedPath(files, lease, workspace.path(BACKUP_PATH));
    if (!backupCleanup) {
        return oc::type::Result<void>::err(
            backupCleanup.error()
        );
    }
    if (workspace.phase != ProductFileTransactionPhase::ROLLED_BACK) {
        auto persisted = persistPhase(
            files,
            lease,
            workspace,
            ProductFileTransactionPhase::ROLLED_BACK
        );
        if (!persisted) {
            return oc::type::Result<void>::err(
                persisted.error()
            );
        }
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> restoreBackup(
    ProductFileService& files,
    const ProductMutationLease& lease,
    JournalWorkspace& workspace,
    bool removeCurrent
) {
    if (removeCurrent) {
        auto removed = files.remove(lease, workspace.path(FINAL_PATH));
        if (!removed) {
            return oc::type::Result<void>::err(
                removed.error()
            );
        }
    }
    auto restored = files.rename(
        lease,
        workspace.path(BACKUP_PATH),
        workspace.path(FINAL_PATH)
    );
    if (!restored) {
        return oc::type::Result<void>::err(
            restored.error()
        );
    }
    auto flushed = files.flush(lease, workspace.path(FINAL_PATH));
    if (!flushed) {
        return oc::type::Result<void>::err(
            flushed.error()
        );
    }
    return finishRolledBack(files, lease, workspace);
}

FLASHMEM oc::type::Result<void> promoteTemporary(
    ProductFileService& files,
    const ProductMutationLease& lease,
    JournalWorkspace& workspace
) {
    auto promoted = files.rename(
        lease,
        workspace.path(TMP_PATH),
        workspace.path(FINAL_PATH)
    );
    if (!promoted) {
        return oc::type::Result<void>::err(
            promoted.error()
        );
    }
    auto flushed = files.flush(lease, workspace.path(FINAL_PATH));
    if (!flushed) {
        return oc::type::Result<void>::err(
            flushed.error()
        );
    }
    if (workspace.hasExpectedCrc32) {
        auto valid = payloadMatches(
            files,
            lease,
            workspace.path(FINAL_PATH),
            workspace.expectedSize,
            workspace.expectedCrc32
        );
        if (!valid) return oc::type::Result<void>::err(valid.error());
        if (!valid.value()) {
            if (workspace.hadCurrent) {
                return restoreBackup(files, lease, workspace, true);
            }
            auto removed = files.remove(lease, workspace.path(FINAL_PATH));
            if (!removed) return removed;
            return finishRolledBack(files, lease, workspace);
        }
    }
    auto phase = persistPhase(
        files,
        lease,
        workspace,
        ProductFileTransactionPhase::PROMOTED
    );
    if (!phase) {
        return oc::type::Result<void>::err(
            phase.error()
        );
    }
    return finishCommitted(files, lease, workspace);
}

FLASHMEM oc::type::Result<void> recoverSelected(
    ProductFileService& files,
    const ProductMutationLease& lease,
    JournalWorkspace& workspace
) {
    auto finalResult = inspectFile(files, lease, workspace.path(FINAL_PATH));
    if (!finalResult) {
        return oc::type::Result<void>::err(finalResult.error());
    }
    auto tmpResult = inspectFile(files, lease, workspace.path(TMP_PATH));
    if (!tmpResult) {
        return oc::type::Result<void>::err(tmpResult.error());
    }
    auto backupResult = inspectFile(files, lease, workspace.path(BACKUP_PATH));
    if (!backupResult) {
        return oc::type::Result<void>::err(backupResult.error());
    }
    const FileState final = finalResult.value();
    const FileState tmp = tmpResult.value();
    const FileState backup = backupResult.value();
    bool finalValid = false;
    bool tmpValid = false;
    if (workspace.hasExpectedCrc32 && final.exists &&
        final.size == workspace.expectedSize) {
        auto valid = payloadMatches(
            files,
            lease,
            workspace.path(FINAL_PATH),
            workspace.expectedSize,
            workspace.expectedCrc32
        );
        if (!valid) return oc::type::Result<void>::err(valid.error());
        finalValid = valid.value();
    }
    if (workspace.hasExpectedCrc32 && tmp.exists &&
        tmp.size == workspace.expectedSize) {
        auto valid = payloadMatches(
            files,
            lease,
            workspace.path(TMP_PATH),
            workspace.expectedSize,
            workspace.expectedCrc32
        );
        if (!valid) return oc::type::Result<void>::err(valid.error());
        tmpValid = valid.value();
    }

    switch (product_file_transaction::decideRecovery(
        workspace,
        final,
        finalValid,
        tmp,
        tmpValid,
        backup
    )) {
        case product_file_transaction::RecoveryAction::FINISH_COMMITTED:
            return finishCommitted(files, lease, workspace);
        case product_file_transaction::RecoveryAction::FINISH_ROLLED_BACK:
            return finishRolledBack(files, lease, workspace);
        case product_file_transaction::RecoveryAction::RESTORE_BACKUP:
            return restoreBackup(files, lease, workspace, false);
        case product_file_transaction::RecoveryAction::REMOVE_CURRENT_AND_RESTORE_BACKUP:
            return restoreBackup(files, lease, workspace, true);
        case product_file_transaction::RecoveryAction::BACK_UP_CURRENT_AND_PROMOTE_TMP: {
            auto backedUp = files.rename(
                lease,
                workspace.path(FINAL_PATH),
                workspace.path(BACKUP_PATH)
            );
            if (!backedUp) {
                return oc::type::Result<void>::err(backedUp.error());
            }
            auto flushed = files.flush(lease, workspace.path(BACKUP_PATH));
            if (!flushed) {
                return oc::type::Result<void>::err(flushed.error());
            }
            auto phase = persistPhase(
                files,
                lease,
                workspace,
                ProductFileTransactionPhase::BACKED_UP
            );
            if (!phase) {
                return oc::type::Result<void>::err(phase.error());
            }
            return promoteTemporary(files, lease, workspace);
        }
        case product_file_transaction::RecoveryAction::PROMOTE_TMP:
            return promoteTemporary(files, lease, workspace);
        case product_file_transaction::RecoveryAction::REMOVE_CURRENT_AND_ROLL_BACK: {
            auto removed = files.remove(lease, workspace.path(FINAL_PATH));
            if (!removed) return removed;
            return finishRolledBack(files, lease, workspace);
        }
        case product_file_transaction::RecoveryAction::FAIL_CORRUPT:
        default:
            return oc::type::Result<void>::err(
                {ErrorCode::STORAGE_CORRUPT,
                 "product file transaction has no valid recovery candidate"}
            );
    }
}

FLASHMEM oc::type::Result<void> executeCommit(
    ProductFileService& files,
    const ProductMutationLease& lease,
    JournalWorkspace& workspace
) {
    auto prepared = persistPhase(
        files,
        lease,
        workspace,
        ProductFileTransactionPhase::PREPARED
    );
    if (!prepared) return prepared;

    if (workspace.hadCurrent) {
        auto current = inspectFile(files, lease, workspace.path(FINAL_PATH));
        if (!current) return oc::type::Result<void>::err(current.error());
        auto backup = inspectFile(files, lease, workspace.path(BACKUP_PATH));
        if (!backup) return oc::type::Result<void>::err(backup.error());
        if (current.value().exists && !backup.value().exists) {
            auto renamed = files.rename(
                lease,
                workspace.path(FINAL_PATH),
                workspace.path(BACKUP_PATH)
            );
            if (!renamed) return renamed;
        } else if (current.value().exists || !backup.value().exists) {
            return oc::type::Result<void>::err(
                {ErrorCode::STORAGE_CORRUPT, "invalid product file backup topology"}
            );
        }
        auto backupFlushed = files.flush(lease, workspace.path(BACKUP_PATH));
        if (!backupFlushed) return backupFlushed;
        auto backedUp = persistPhase(
            files,
            lease,
            workspace,
            ProductFileTransactionPhase::BACKED_UP
        );
        if (!backedUp) return backedUp;
    }

    auto promoted = files.rename(
        lease,
        workspace.path(TMP_PATH),
        workspace.path(FINAL_PATH)
    );
    if (!promoted) return promoted;
    auto finalFlushed = files.flush(lease, workspace.path(FINAL_PATH));
    if (!finalFlushed) return finalFlushed;
    auto finalValid = payloadMatches(
        files,
        lease,
        workspace.path(FINAL_PATH),
        workspace.expectedSize,
        workspace.expectedCrc32
    );
    if (!finalValid) return oc::type::Result<void>::err(finalValid.error());
    if (!finalValid.value()) {
        return oc::type::Result<void>::err(
            {ErrorCode::STORAGE_CORRUPT,
             "promoted product file checksum mismatch"}
        );
    }
    auto promotedPhase = persistPhase(
        files,
        lease,
        workspace,
        ProductFileTransactionPhase::PROMOTED
    );
    if (!promotedPhase) return promotedPhase;

    if (workspace.hadCurrent) {
        auto backupCleanup = cleanupMappedPath(
            files,
            lease,
            workspace.path(BACKUP_PATH)
        );
        if (!backupCleanup) return backupCleanup;
    }
    return persistPhase(
        files,
        lease,
        workspace,
        ProductFileTransactionPhase::COMMITTED
    );
}

FLASHMEM void requireMappedRecovery(
    ProductFileService& files,
    const ProductMutationLease& lease,
    ErrorCode error
) {
    if (files.owns(lease)) (void)files.requireRecovery(lease, error);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
static FLASHMEM oc::type::Result<void> commitWithWorkspace(
    ProductFileService& files,
    const ProductMutationLease& lease,
    JournalWorkspace& workspace,
    const char* current,
    const char* backup,
    const char* tmp,
    uint32_t expectedSize,
    uint32_t expectedCrc32
) {
    auto selected = selectLatest(files, lease, workspace);
    if (!selected) {
        requireMappedRecovery(files, lease, selected.error().code);
        return oc::type::Result<void>::err(selected.error());
    }
    if (selected.value().present) {
        if (!phaseTerminal(workspace.phase)) {
            requireMappedRecovery(files, lease, ErrorCode::STORAGE_WRITE_FAILED);
            return oc::type::Result<void>::err(
                {ErrorCode::HARDWARE_BUSY, "product file recovery pending"}
            );
        }
    }
    if (workspace.sequence > UINT64_MAX - 4U) {
        return oc::type::Result<void>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "product file journal sequence exhausted"}
        );
    }

    auto normalized = normalizePaths(files, workspace, current, tmp, backup);
    if (!normalized) return normalized;
    workspace.expectedSize = expectedSize;
    workspace.expectedCrc32 = expectedCrc32;
    workspace.hasExpectedCrc32 = true;

    auto tmpInfo = inspectFile(files, lease, workspace.path(TMP_PATH));
    if (!tmpInfo) return oc::type::Result<void>::err(tmpInfo.error());
    if (!tmpInfo.value().exists || tmpInfo.value().size != expectedSize) {
        return oc::type::Result<void>::err(
            {ErrorCode::STORAGE_WRITE_FAILED, "product file temporary size mismatch"}
        );
    }
    auto tmpFlushed = files.flush(lease, workspace.path(TMP_PATH));
    if (!tmpFlushed) return tmpFlushed;
    auto tmpValid = payloadMatches(
        files,
        lease,
        workspace.path(TMP_PATH),
        expectedSize,
        expectedCrc32
    );
    if (!tmpValid) return oc::type::Result<void>::err(tmpValid.error());
    if (!tmpValid.value()) {
        return oc::type::Result<void>::err(
            {ErrorCode::STORAGE_CORRUPT,
             "product file temporary checksum mismatch"}
        );
    }

    auto currentInfo = inspectFile(files, lease, workspace.path(FINAL_PATH));
    if (!currentInfo) return oc::type::Result<void>::err(currentInfo.error());
    auto backupInfo = inspectFile(files, lease, workspace.path(BACKUP_PATH));
    if (!backupInfo) return oc::type::Result<void>::err(backupInfo.error());
    if (currentInfo.value().exists && backupInfo.value().exists) {
        auto staleCleanup = cleanupMappedPath(files, lease, workspace.path(BACKUP_PATH));
        if (!staleCleanup) return staleCleanup;
        backupInfo = oc::type::Result<FileState>::ok({false, 0});
    }
    workspace.hadCurrent = currentInfo.value().exists || backupInfo.value().exists;

    // From the first slot write onward, even an ambiguous backend failure owns
    // the mapped paths. Normal I/O is blocked until the recovery transaction
    // selects the last CRC-valid phase.
    auto committed = executeCommit(files, lease, workspace);
    if (!committed) {
        requireMappedRecovery(files, lease, committed.error().code);
        return committed;
    }
    return oc::type::Result<void>::ok();
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
static FLASHMEM oc::type::Result<void> recoverWithWorkspace(
    ProductFileService& files,
    const ProductMutationLease& recoveryLease,
    JournalWorkspace& workspace
) {
    auto selected = selectLatest(files, recoveryLease, workspace);
    if (!selected) {
        return oc::type::Result<void>::err(selected.error());
    }
    if (!selected.value().present) {
        return oc::type::Result<void>::ok();
    }
    return recoverSelected(files, recoveryLease, workspace);
}

}  // namespace

FLASHMEM oc::type::Result<void> commitProductFileTemp(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* current,
    const char* backup,
    const char* tmp,
    uint32_t expectedSize,
    uint32_t expectedCrc32
) {
    if (!files.owns(lease) || current == nullptr || backup == nullptr || tmp == nullptr) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid durable product file commit"}
        );
    }

    JournalWorkspace workspace{};
    return commitWithWorkspace(
        files,
        lease,
        workspace,
        current,
        backup,
        tmp,
        expectedSize,
        expectedCrc32
    );
}

FLASHMEM oc::type::Result<void> recoverPendingProductFileTransaction(
    ProductFileService& files,
    const ProductMutationLease& recoveryLease
) {
    if (!files.owns(recoveryLease, ProductMutationOwner::RECOVERY)) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "exact product recovery lease required"}
        );
    }

    JournalWorkspace workspace{};
    return recoverWithWorkspace(files, recoveryLease, workspace);
}

}  // namespace core::persistence

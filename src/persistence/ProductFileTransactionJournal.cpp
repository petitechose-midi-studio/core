#include "persistence/AtomicProductFile.hpp"

#include <config/PlatformCompat.hpp>

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
    const bool finalExact = final.exists && final.size == workspace.expectedSize;
    const bool tmpExact = tmp.exists && tmp.size == workspace.expectedSize;

    if (workspace.phase == ProductFileTransactionPhase::ROLLED_BACK) {
        if (workspace.hadCurrent) {
            if (final.exists) return finishRolledBack(files, lease, workspace);
            if (backup.exists) return restoreBackup(files, lease, workspace, false);
            return oc::type::Result<void>::err(
                {ErrorCode::STORAGE_CORRUPT, "rolled-back product file is missing"}
            );
        }
        if (!final.exists) return finishRolledBack(files, lease, workspace);
        if (finalExact) return finishCommitted(files, lease, workspace);
        return oc::type::Result<void>::err(
            {ErrorCode::STORAGE_CORRUPT, "unexpected rolled-back product file"}
        );
    }

    if (workspace.phase == ProductFileTransactionPhase::COMMITTED && finalExact) {
        return finishCommitted(files, lease, workspace);
    }

    if (final.exists && tmp.exists) {
        if (workspace.phase == ProductFileTransactionPhase::PREPARED &&
            workspace.hadCurrent && !backup.exists && tmpExact) {
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
        if (phaseTerminal(workspace.phase)) {
            return workspace.phase == ProductFileTransactionPhase::COMMITTED &&
                           finalExact
                ? finishCommitted(files, lease, workspace)
                : finishRolledBack(files, lease, workspace);
        }
        return oc::type::Result<void>::err(
            {ErrorCode::STORAGE_CORRUPT, "ambiguous product file and temporary"}
        );
    }

    if (!final.exists && tmp.exists) {
        if (!tmpExact) {
            if (backup.exists) return restoreBackup(files, lease, workspace, false);
            if (!workspace.hadCurrent) return finishRolledBack(files, lease, workspace);
            return oc::type::Result<void>::err(
                {ErrorCode::STORAGE_CORRUPT, "invalid product file temporary"}
            );
        }
        return promoteTemporary(files, lease, workspace);
    }

    if (final.exists && !tmp.exists) {
        if (backup.exists) {
            if (finalExact) return finishCommitted(files, lease, workspace);
            return restoreBackup(files, lease, workspace, true);
        }
        if (workspace.phase == ProductFileTransactionPhase::PREPARED &&
            workspace.hadCurrent) {
            return finishRolledBack(files, lease, workspace);
        }
        if (finalExact) return finishCommitted(files, lease, workspace);
        if (workspace.phase == ProductFileTransactionPhase::ROLLED_BACK &&
            workspace.hadCurrent) {
            return finishRolledBack(files, lease, workspace);
        }
        return oc::type::Result<void>::err(
            {ErrorCode::STORAGE_CORRUPT, "promoted product file size mismatch"}
        );
    }

    if (backup.exists) return restoreBackup(files, lease, workspace, false);
    if (!workspace.hadCurrent) return finishRolledBack(files, lease, workspace);
    return oc::type::Result<void>::err(
        {ErrorCode::STORAGE_CORRUPT, "product file transaction lost old and new"}
    );
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
    uint32_t expectedSize
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

    auto tmpInfo = inspectFile(files, lease, workspace.path(TMP_PATH));
    if (!tmpInfo) return oc::type::Result<void>::err(tmpInfo.error());
    if (!tmpInfo.value().exists || tmpInfo.value().size != expectedSize) {
        return oc::type::Result<void>::err(
            {ErrorCode::STORAGE_WRITE_FAILED, "product file temporary size mismatch"}
        );
    }
    auto tmpFlushed = files.flush(lease, workspace.path(TMP_PATH));
    if (!tmpFlushed) return tmpFlushed;

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
    uint32_t expectedSize
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
        expectedSize
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

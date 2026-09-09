#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/ProductFileTransactionJournalInternal.hpp"

namespace core::persistence {

/** Allocation-free continuation for the ordinary two-slot transaction journal. */
class ProductFileRecoveryPlan {
public:
    oc::type::Result<void> begin(
        ProductFileService& files,
        const ProductMutationLease& recoveryLease
    );
    oc::type::Result<bool> advance(
        ProductFileService& files,
        const ProductMutationLease& recoveryLease,
        uint8_t* scratch,
        size_t scratchSize
    );

    void reset();
    bool active() const;
    bool complete() const;

private:
    enum class Step : uint8_t {
        IDLE = 0,
        SELECT_JOURNAL,
        INSPECT_FINAL,
        INSPECT_TMP,
        INSPECT_BACKUP,
        VERIFY_FINAL,
        VERIFY_TMP,
        REMOVE_CURRENT,
        RESTORE_BACKUP,
        REMOVE_UNVERIFIED_CURRENT,
        FLUSH_RESTORED,
        BACK_UP_CURRENT,
        FLUSH_BACKUP,
        PERSIST_BACKED_UP,
        PROMOTE_TMP,
        FLUSH_PROMOTED,
        VERIFY_PROMOTED,
        PERSIST_PROMOTED,
        CLEAN_TMP,
        CLEAN_BACKUP,
        PERSIST_TERMINAL,
        COMPLETE,
        FAILED,
    };

    oc::type::Result<bool> decide_();
    void beginIntegrityCheck_();
    oc::type::Result<bool> advanceIntegrityCheck_(
        ProductFileService& files,
        const ProductMutationLease& lease,
        const char* path,
        uint8_t* scratch,
        size_t scratchSize
    );
    oc::type::Result<bool> beginNextIntegrityCheck_();
    oc::type::Result<bool> fail_(oc::type::Error error);
    oc::type::Result<bool> finish_(ProductFileTransactionPhase phase);
    oc::type::Result<bool> restoreBackup_(bool removeCurrent);
    oc::type::Result<bool> promoteTemporary_();

    product_file_transaction::JournalWorkspace workspace_{};
    product_file_transaction::FileState final_{};
    product_file_transaction::FileState tmp_{};
    product_file_transaction::FileState backup_{};
    ProductFileTransactionPhase terminal_phase_ =
        ProductFileTransactionPhase::NONE;
    uint32_t integrity_crc_state_ = 0U;
    uint32_t integrity_offset_ = 0U;
    Step step_ = Step::IDLE;
    bool final_valid_ = false;
    bool tmp_valid_ = false;
};

static_assert(
    sizeof(ProductFileRecoveryPlan) <= 768U,
    "ordinary recovery continuation exceeds PSRAM control ceiling"
);

}  // namespace core::persistence

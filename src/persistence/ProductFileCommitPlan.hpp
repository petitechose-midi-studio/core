#pragma once

#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/ProductFileTransactionJournalInternal.hpp"

namespace core::persistence {

/**
 * Preadmitted, allocation-free continuation for one ordinary product-file
 * promotion. The owner must retain this object outside scarce RAM and call
 * advance() at most once per foreground turn.
 */
class ProductFileCommitPlan {
public:
    oc::type::Result<void> begin(
        ProductFileService& files,
        const ProductMutationLease& lease,
        const char* current,
        const char* backup,
        const char* tmp,
        uint32_t expectedSize
    );
    oc::type::Result<bool> advance(
        ProductFileService& files,
        const ProductMutationLease& lease
    );

    void reset();
    bool active() const;
    bool complete() const;
    bool mapped() const { return mapped_; }
    bool requiresRecoveryOnFailure() const { return recovery_required_on_error_; }

private:
    enum class Step : uint8_t {
        IDLE = 0,
        READ_SLOT_A,
        READ_SLOT_B,
        VERIFY_SELECTED_SLOT,
        CLEAN_CORRUPT_SLOT,
        INSPECT_TMP,
        FLUSH_TMP,
        INSPECT_CURRENT,
        INSPECT_BACKUP,
        CLEAN_STALE_BACKUP,
        PERSIST_PREPARED,
        BACK_UP_CURRENT,
        PERSIST_BACKED_UP,
        PROMOTE_TMP,
        PERSIST_PROMOTED,
        CLEAN_BACKUP,
        PERSIST_COMMITTED,
        COMPLETE,
        FAILED,
    };

    oc::type::Result<bool> selectSlots_();
    oc::type::Result<bool> initializeCommitWorkspace_();
    oc::type::Result<bool> fail_(oc::type::Error error, bool recoveryRequired);
    product_file_transaction::JournalWorkspace& workspace_();
    const product_file_transaction::JournalWorkspace& workspace_() const;

    product_file_transaction::JournalWorkspace slots_[2]{};
    product_file_transaction::JournalSlotObservation observations_[2]{};
    char requested_paths_[product_file_transaction::PATH_COUNT]
                         [product_file_transaction::PATH_CAPACITY]{};
    product_file_transaction::FileState current_{};
    product_file_transaction::FileState backup_{};
    uint32_t expected_size_ = 0U;
    Step step_ = Step::IDLE;
    uint8_t active_workspace_ = 0U;
    uint8_t selected_slot_ = 0U;
    uint8_t corrupt_slot_ = 0U;
    bool selected_present_ = false;
    bool mapped_ = false;
    bool recovery_required_on_error_ = false;
};

static_assert(
    sizeof(ProductFileCommitPlan) <= 2'048U,
    "product file commit continuation exceeds PSRAM control ceiling"
);

}  // namespace core::persistence

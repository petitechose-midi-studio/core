#pragma once

#include <array>
#include <cstdint>

#include "persistence/ProductFileRecoveryPlan.hpp"
#include "persistence/ProductStorageRecoveryService.hpp"
#include "protocol/filesystem/FileSystemRpcConditionalPlan.hpp"

namespace core::persistence {

/**
 * Preadmitted hot-swap reconciliation continuation.
 *
 * Runtime owners place this object in PSRAM, call begin() before queue
 * admission, then execute at most one advance() in each stopped foreground
 * turn. BOOT may drive the same state machine synchronously before app start.
 */
class ProductStorageRecoveryPlan {
public:
    bool begin(
        ProductFileService& files,
        ProjectSessionAutosaveService& autosaveService,
        core::state::CoreState& state,
        ProductStorageRecoveryMode mode
    );
    bool advance(
        ProductFileService& files,
        ProjectSessionRestoreService& restoreService,
        ProjectSessionAutosaveService& autosaveService,
        core::state::CoreState& state
    );
    void cancel(
        ProductFileService& files,
        ProjectSessionAutosaveService& autosaveService,
        oc::type::ErrorCode error = oc::type::ErrorCode::HARDWARE_BUSY
    );

    ProductPersistenceWorkQuota nextWorkQuota(
        const ProjectSessionAutosaveService& autosaveService
    ) const;
    bool active() const;
    bool terminal() const;
    const ProductStorageRecoveryResult& result() const { return result_; }
    uint32_t lastWorkBytes() const { return last_work_bytes_; }

private:
    enum class Step : uint8_t {
        IDLE = 0,
        ENSURE_LAYOUT,
        BEGIN_ORDINARY,
        ADVANCE_ORDINARY,
        LOAD_CONDITIONAL,
        QUARANTINE_CONDITIONAL,
        CLEAN_CONDITIONAL_STAGING,
        BEGIN_CONDITIONAL,
        ADVANCE_CONDITIONAL,
        RESTORE_BOOT_SESSION,
        RECONCILE_SETTINGS,
        BEGIN_SESSION_SAVE,
        ADVANCE_SESSION_SAVE,
        COMPLETE_RECOVERY,
        COMPLETE,
        FAILED,
    };

    bool fail_(
        ProductFileService& files,
        ProjectSessionAutosaveService& autosaveService,
        ProductStorageRecoveryStatus status,
        oc::type::Error error
    );
    void copySessionResult_(
        const ProjectSessionAutosaveService::Result& session
    );
    void advanceAfterConditional_();

    ProductFileRecoveryPlan ordinary_{};
    protocol::filesystem::conditional_mutation::ConditionalMutationPlan
        conditional_{};
    protocol::filesystem::conditional_mutation::Journal conditional_journal_{};
    std::array<uint8_t, protocol::filesystem::FILESYSTEM_RPC_MAX_CHUNK_SIZE>
        scratch_{};
    ProductStorageRecoveryResult result_{};
    ProductMutationLease lease_{};
    ProductStorageRecoveryMode mode_ = ProductStorageRecoveryMode::HOT_SWAP;
    Step step_ = Step::IDLE;
    bool conditional_present_ = false;
    bool conditional_corrupt_ = false;
    bool session_recovery_started_ = false;
    uint8_t layout_index_ = 0U;
    uint32_t last_work_bytes_ = 0U;
};

static_assert(
    sizeof(ProductStorageRecoveryPlan) <= 40U * 1024U,
    "storage recovery continuation exceeds PSRAM control ceiling"
);

}  // namespace core::persistence

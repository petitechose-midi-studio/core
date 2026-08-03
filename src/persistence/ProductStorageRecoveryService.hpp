#pragma once

#include <cstdint>

#include "persistence/PersistenceStatus.hpp"
#include "persistence/ProductFileService.hpp"
#include "persistence/ProjectSessionAutosaveService.hpp"
#include "persistence/ProjectSessionRestoreService.hpp"

namespace core::state {
struct CoreState;
}

namespace core::persistence {

enum class ProductStorageRecoveryMode : uint8_t {
    BOOT = 0,
    HOT_SWAP,
};

enum class ProductStorageRecoveryStatus : uint8_t {
    RECOVERED = 0,
    MEDIA_UNAVAILABLE,
    BUSY,
    RESOURCE_EXHAUSTED,
    LAYOUT_FAILED,
    ORDINARY_TRANSACTION_FAILED,
    CONDITIONAL_FAILED,
    SETTINGS_FAILED,
    SESSION_SAVE_FAILED,
    COMPLETION_FAILED,
};

struct ProductStorageRecoveryResult {
    ProductStorageRecoveryStatus status = ProductStorageRecoveryStatus::BUSY;
    oc::type::ErrorCode error = oc::type::ErrorCode::OK;
    PersistenceWriteStatus settingsStatus = PersistenceWriteStatus::OK;
    ProjectSessionRestoreService::Status sessionRestoreStatus =
        ProjectSessionRestoreService::Status::MISSING;
    ProjectSessionAutosaveService::Status sessionSaveStatus =
        ProjectSessionAutosaveService::Status::IDLE;
    uint32_t sessionRestoreBytes = 0;
    uint32_t sessionSaveBytes = 0;
    bool conditionalJournalQuarantined = false;

    bool recovered() const {
        return status == ProductStorageRecoveryStatus::RECOVERED;
    }
};

/**
 * Stateless, allocation-free product-storage reconciliation transaction.
 *
 * The caller owns timing and physical backend reopen. This operation owns one
 * exact RECOVERY lease across every product mutation and publishes READY only
 * after the live RAM session has durably committed.
 */
class ProductStorageRecoveryService {
public:
    static ProductStorageRecoveryResult reconcile(
        ProductFileService& files,
        ProjectSessionRestoreService& restoreService,
        ProjectSessionAutosaveService& autosaveService,
        core::state::CoreState& state,
        ProductStorageRecoveryMode mode
    );
};

}  // namespace core::persistence

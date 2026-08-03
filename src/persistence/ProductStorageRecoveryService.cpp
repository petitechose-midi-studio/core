#include "persistence/ProductStorageRecoveryService.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "protocol/filesystem/FileSystemRpcConditionalTransaction.hpp"
#include "state/CoreState.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;
namespace conditional = core::protocol::filesystem::conditional_mutation;
using core::protocol::filesystem::FileSystemRpcStatus;

FLASHMEM ErrorCode settingsError(PersistenceWriteStatus status) {
    switch (status) {
        case PersistenceWriteStatus::STORAGE_UNAVAILABLE:
            return ErrorCode::HARDWARE_NOT_FOUND;
        case PersistenceWriteStatus::PAYLOAD_TOO_LARGE:
        case PersistenceWriteStatus::OUT_OF_RANGE:
            return ErrorCode::RESOURCE_EXHAUSTED;
        case PersistenceWriteStatus::INVALID_CONFIG:
            return ErrorCode::INVALID_ARGUMENT;
        case PersistenceWriteStatus::IO_ERROR:
        case PersistenceWriteStatus::ERASE_FAILED:
        case PersistenceWriteStatus::COMMIT_FAILED:
            return ErrorCode::STORAGE_WRITE_FAILED;
        case PersistenceWriteStatus::OK:
        default:
            return ErrorCode::OK;
    }
}

FLASHMEM ErrorCode sessionSaveError(ProjectSessionAutosaveService::Status status) {
    switch (status) {
        case ProjectSessionAutosaveService::Status::BLOCKED:
        case ProjectSessionAutosaveService::Status::WAITING:
            return ErrorCode::HARDWARE_BUSY;
        case ProjectSessionAutosaveService::Status::CAPTURE_FAILED:
            return ErrorCode::RESOURCE_EXHAUSTED;
        case ProjectSessionAutosaveService::Status::SAVE_FAILED:
            return ErrorCode::STORAGE_WRITE_FAILED;
        case ProjectSessionAutosaveService::Status::IDLE:
        case ProjectSessionAutosaveService::Status::SAVING:
        case ProjectSessionAutosaveService::Status::SAVED:
        default:
            return ErrorCode::INVALID_STATE;
    }
}

FLASHMEM ProductStorageRecoveryResult failRecovery(
    ProductFileService& files,
    ProductMutationLease& lease,
    ProductStorageRecoveryResult result,
    ProductStorageRecoveryStatus status,
    ErrorCode error
) {
    result.status = status;
    result.error = error;

    if (files.storageState() == ProductStorageState::ABSENT) {
        result.status = ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE;
        result.error = ErrorCode::HARDWARE_NOT_FOUND;
        return result;
    }
    if (!files.owns(lease, ProductMutationOwner::RECOVERY)) {
        return result;
    }

    auto completed = files.completeRecovery(lease, false, error);
    if (!completed) {
        result.status = files.storageState() == ProductStorageState::ABSENT
            ? ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE
            : ProductStorageRecoveryStatus::COMPLETION_FAILED;
        result.error = completed.error().code;
    }
    return result;
}

FLASHMEM ProductStorageRecoveryResult acquisitionFailure(ErrorCode error) {
    ProductStorageRecoveryResult result{};
    result.error = error;
    switch (error) {
        case ErrorCode::HARDWARE_NOT_FOUND:
        case ErrorCode::HARDWARE_INIT_FAILED:
            result.status = ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE;
            break;
        case ErrorCode::RESOURCE_EXHAUSTED:
            result.status = ProductStorageRecoveryStatus::RESOURCE_EXHAUSTED;
            break;
        case ErrorCode::HARDWARE_BUSY:
        default:
            result.status = ProductStorageRecoveryStatus::BUSY;
            break;
    }
    return result;
}

}  // namespace

FLASHMEM ProductStorageRecoveryResult ProductStorageRecoveryService::reconcile(
    ProductFileService& files,
    ProjectSessionRestoreService& restoreService,
    ProjectSessionAutosaveService& autosaveService,
    core::state::CoreState& state,
    ProductStorageRecoveryMode mode
) {
    auto acquired = files.beginRecovery();
    if (!acquired) return acquisitionFailure(acquired.error().code);
    auto lease = std::move(acquired.value());

    ProductStorageRecoveryResult result{};
    auto layout = files.ensureLayout(lease);
    if (!layout) {
        return failRecovery(
            files,
            lease,
            result,
            ProductStorageRecoveryStatus::LAYOUT_FAILED,
            layout.error().code
        );
    }

    const auto conditionalStatus = conditional::recoverPendingMutation(
        files,
        lease,
        result.conditionalJournalQuarantined
    );
    if (conditionalStatus != FileSystemRpcStatus::OK) {
        return failRecovery(
            files,
            lease,
            result,
            ProductStorageRecoveryStatus::CONDITIONAL_FAILED,
            conditional::recoveryError(conditionalStatus)
        );
    }

    if (mode == ProductStorageRecoveryMode::BOOT) {
        const auto restored = restoreService.restore(state, lease);
        result.sessionRestoreStatus = restored.status;
        result.sessionRestoreBytes = restored.bytes;
        if (!files.owns(lease, ProductMutationOwner::RECOVERY)) {
            return failRecovery(
                files,
                lease,
                result,
                ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE,
                ErrorCode::HARDWARE_NOT_FOUND
            );
        }
    }

    result.settingsStatus = state.recoverSettingsFromRamAfterStorageReopen();
    if (result.settingsStatus != PersistenceWriteStatus::OK) {
        return failRecovery(
            files,
            lease,
            result,
            ProductStorageRecoveryStatus::SETTINGS_FAILED,
            settingsError(result.settingsStatus)
        );
    }

    const auto saved = autosaveService.flushRecovery(state, lease);
    result.sessionSaveStatus = saved.status;
    result.sessionSaveBytes = saved.bytes;
    if (!saved.saved()) {
        const auto error = files.storageState() == ProductStorageState::ABSENT
            ? ErrorCode::HARDWARE_NOT_FOUND
            : sessionSaveError(saved.status);
        return failRecovery(
            files,
            lease,
            result,
            ProductStorageRecoveryStatus::SESSION_SAVE_FAILED,
            error
        );
    }

    auto completed = files.completeRecovery(lease, true);
    if (!completed) {
        result.status = files.storageState() == ProductStorageState::ABSENT
            ? ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE
            : ProductStorageRecoveryStatus::COMPLETION_FAILED;
        result.error = completed.error().code;
        return result;
    }

    result.status = ProductStorageRecoveryStatus::RECOVERED;
    result.error = ErrorCode::OK;
    return result;
}

}  // namespace core::persistence

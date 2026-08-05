#include "persistence/ProductStorageRecoveryService.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/AtomicProductFile.hpp"
#include "persistence/ProductStorageRecoveryPlan.hpp"
#include "protocol/filesystem/FileSystemRpcConditionalTransaction.hpp"
#include "state/CoreState.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;
using oc::type::Error;
namespace conditional = core::protocol::filesystem::conditional_mutation;
using core::protocol::filesystem::FileSystemRpcStatus;

const char kConditionalRecoveryFailed[] PROGMEM =
    "conditional product mutation recovery failed";
const char kTreeCleanupRecoveryFailed[] PROGMEM =
    "hidden product tree cleanup failed";
const char kRecoveryLeaseLost[] PROGMEM =
    "product recovery lease lost during session restore";
const char kSessionSaveBlocked[] PROGMEM =
    "project session recovery save blocked";
const char kSessionCaptureFailed[] PROGMEM =
    "project session recovery capture failed";
const char kSessionSaveStateInvalid[] PROGMEM =
    "project session recovery save returned invalid state";
const char kProductStorageUnavailable[] PROGMEM =
    "product storage unavailable";
const char kStatusRecovered[] PROGMEM = "RECOVERED";
const char kStatusMediaUnavailable[] PROGMEM = "MEDIA_UNAVAILABLE";
const char kStatusBusy[] PROGMEM = "BUSY";
const char kStatusResourceExhausted[] PROGMEM = "RESOURCE_EXHAUSTED";
const char kStatusLayoutFailed[] PROGMEM = "LAYOUT_FAILED";
const char kStatusOrdinaryTransactionFailed[] PROGMEM =
    "ORDINARY_TRANSACTION_FAILED";
const char kStatusConditionalFailed[] PROGMEM = "CONDITIONAL_FAILED";
const char kStatusSettingsFailed[] PROGMEM = "SETTINGS_FAILED";
const char kStatusSessionSaveFailed[] PROGMEM = "SESSION_SAVE_FAILED";
const char kStatusCompletionFailed[] PROGMEM = "COMPLETION_FAILED";
const char kStatusTreeCleanupFailed[] PROGMEM = "TREE_CLEANUP_FAILED";
const char kStatusUnknown[] PROGMEM = "UNKNOWN";

FLASHMEM Error settingsError(PersistenceWriteStatus status) {
    switch (status) {
        case PersistenceWriteStatus::STORAGE_UNAVAILABLE:
            return {ErrorCode::HARDWARE_NOT_FOUND,
                    persistenceWriteStatusLabel(status)};
        case PersistenceWriteStatus::PAYLOAD_TOO_LARGE:
        case PersistenceWriteStatus::OUT_OF_RANGE:
            return {ErrorCode::RESOURCE_EXHAUSTED,
                    persistenceWriteStatusLabel(status)};
        case PersistenceWriteStatus::INVALID_CONFIG:
            return {ErrorCode::INVALID_ARGUMENT,
                    persistenceWriteStatusLabel(status)};
        case PersistenceWriteStatus::IO_ERROR:
        case PersistenceWriteStatus::ERASE_FAILED:
        case PersistenceWriteStatus::COMMIT_FAILED:
            return {ErrorCode::STORAGE_WRITE_FAILED,
                    persistenceWriteStatusLabel(status)};
        case PersistenceWriteStatus::OK:
        default:
            return {ErrorCode::OK, persistenceWriteStatusLabel(status)};
    }
}

FLASHMEM Error sessionSaveError(
    const ProjectSessionAutosaveService::Result& result
) {
    if (result.error != ErrorCode::OK) {
        return {result.error, result.errorContext};
    }
    switch (result.status) {
        case ProjectSessionAutosaveService::Status::BLOCKED:
        case ProjectSessionAutosaveService::Status::WAITING:
            return {ErrorCode::HARDWARE_BUSY, kSessionSaveBlocked};
        case ProjectSessionAutosaveService::Status::CAPTURE_FAILED:
            return {ErrorCode::RESOURCE_EXHAUSTED, kSessionCaptureFailed};
        case ProjectSessionAutosaveService::Status::SAVE_FAILED:
            return {ErrorCode::STORAGE_WRITE_FAILED, kSessionSaveStateInvalid};
        case ProjectSessionAutosaveService::Status::IDLE:
        case ProjectSessionAutosaveService::Status::SAVING:
        case ProjectSessionAutosaveService::Status::SAVED:
        default:
            return {ErrorCode::INVALID_STATE, kSessionSaveStateInvalid};
    }
}

FLASHMEM ProductStorageRecoveryResult failRecovery(
    ProductFileService& files,
    ProductMutationLease& lease,
    ProductStorageRecoveryResult result,
    ProductStorageRecoveryStatus status,
    Error error
) {
    result.status = status;
    result.error = error.code;
    result.errorContext = error.context;

    if (files.storageState() == ProductStorageState::ABSENT) {
        result.status = ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE;
        result.error = ErrorCode::HARDWARE_NOT_FOUND;
        result.errorContext = kProductStorageUnavailable;
        return result;
    }
    if (!files.owns(lease, ProductMutationOwner::RECOVERY)) {
        return result;
    }

    auto completed = files.completeRecovery(lease, false, error.code);
    if (!completed) {
        result.status = files.storageState() == ProductStorageState::ABSENT
            ? ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE
            : ProductStorageRecoveryStatus::COMPLETION_FAILED;
        result.error = completed.error().code;
        result.errorContext = completed.error().context;
    }
    return result;
}

FLASHMEM ProductStorageRecoveryResult acquisitionFailure(Error error) {
    ProductStorageRecoveryResult result{};
    result.error = error.code;
    result.errorContext = error.context;
    switch (error.code) {
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

FLASHMEM const char* productStorageRecoveryStatusLabel(
    ProductStorageRecoveryStatus status
) {
    switch (status) {
        case ProductStorageRecoveryStatus::RECOVERED: return kStatusRecovered;
        case ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE:
            return kStatusMediaUnavailable;
        case ProductStorageRecoveryStatus::BUSY: return kStatusBusy;
        case ProductStorageRecoveryStatus::RESOURCE_EXHAUSTED:
            return kStatusResourceExhausted;
        case ProductStorageRecoveryStatus::LAYOUT_FAILED:
            return kStatusLayoutFailed;
        case ProductStorageRecoveryStatus::ORDINARY_TRANSACTION_FAILED:
            return kStatusOrdinaryTransactionFailed;
        case ProductStorageRecoveryStatus::CONDITIONAL_FAILED:
            return kStatusConditionalFailed;
        case ProductStorageRecoveryStatus::SETTINGS_FAILED:
            return kStatusSettingsFailed;
        case ProductStorageRecoveryStatus::SESSION_SAVE_FAILED:
            return kStatusSessionSaveFailed;
        case ProductStorageRecoveryStatus::COMPLETION_FAILED:
            return kStatusCompletionFailed;
        case ProductStorageRecoveryStatus::TREE_CLEANUP_FAILED:
            return kStatusTreeCleanupFailed;
        default: return kStatusUnknown;
    }
}

FLASHMEM bool productStorageRecoveryRequiresMediaChange(
    ProductStorageRecoveryStatus status
) {
    switch (status) {
        case ProductStorageRecoveryStatus::RECOVERED:
        case ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE:
        case ProductStorageRecoveryStatus::BUSY:
            return false;
        case ProductStorageRecoveryStatus::RESOURCE_EXHAUSTED:
        case ProductStorageRecoveryStatus::LAYOUT_FAILED:
        case ProductStorageRecoveryStatus::ORDINARY_TRANSACTION_FAILED:
        case ProductStorageRecoveryStatus::CONDITIONAL_FAILED:
        case ProductStorageRecoveryStatus::SETTINGS_FAILED:
        case ProductStorageRecoveryStatus::SESSION_SAVE_FAILED:
        case ProductStorageRecoveryStatus::COMPLETION_FAILED:
        case ProductStorageRecoveryStatus::TREE_CLEANUP_FAILED:
        default:
            return true;
    }
}

FLASHMEM bool ProductStorageRecoveryPlan::begin(
    ProductFileService& files,
    ProjectSessionAutosaveService& autosaveService,
    core::state::CoreState& state,
    ProductStorageRecoveryMode mode
) {
    if (active()) return false;
    ordinary_.reset();
    conditional_journal_ = {};
    result_ = {};
    lease_ = ProductMutationLease{};
    mode_ = mode;
    step_ = Step::IDLE;
    conditional_present_ = false;
    conditional_corrupt_ = false;
    session_recovery_started_ = false;
    layout_index_ = 0U;
    last_work_bytes_ = 0U;

    auto acquired = files.beginRecovery();
    if (!acquired) {
        result_ = acquisitionFailure(acquired.error());
        step_ = Step::FAILED;
        return false;
    }
    lease_ = std::move(acquired.value());

    if (mode_ == ProductStorageRecoveryMode::HOT_SWAP) {
        const auto session = autosaveService.beginRecovery(state, lease_);
        copySessionResult_(session);
        if (session.status != ProjectSessionAutosaveService::Status::SAVING) {
            return fail_(
                files,
                autosaveService,
                ProductStorageRecoveryStatus::SESSION_SAVE_FAILED,
                sessionSaveError(session)
            );
        }
        session_recovery_started_ = true;
    }

    step_ = Step::ENSURE_LAYOUT;
    return true;
}

FLASHMEM bool ProductStorageRecoveryPlan::advance(
    ProductFileService& files,
    ProjectSessionRestoreService& restoreService,
    ProjectSessionAutosaveService& autosaveService,
    core::state::CoreState& state,
    ProductPersistenceWorkMeasurement* measurement
) {
    last_work_bytes_ = 0U;
    if (!active() || !files.owns(lease_, ProductMutationOwner::RECOVERY)) {
        return fail_(
            files,
            autosaveService,
            ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE,
            {ErrorCode::HARDWARE_NOT_FOUND, kRecoveryLeaseLost}
        );
    }

    switch (step_) {
        case Step::ENSURE_LAYOUT: {
            auto layout = files.ensureLayoutDirectory(lease_, layout_index_);
            if (!layout) {
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::LAYOUT_FAILED,
                    layout.error()
                );
            }
            ++layout_index_;
            if (layout_index_ == ProductFileService::LAYOUT_DIRECTORY_COUNT) {
                step_ = Step::BEGIN_TREE_CLEANUP;
            }
            return false;
        }
        case Step::BEGIN_TREE_CLEANUP:
            tree_cleanup_.beginRecovery();
            step_ = Step::ADVANCE_TREE_CLEANUP;
            return false;
        case Step::ADVANCE_TREE_CLEANUP:
            if (!tree_cleanup_.advanceRecovery(files, lease_, measurement)) {
                return false;
            }
            if (!tree_cleanup_.completed()) {
                const auto error = tree_cleanup_.error();
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::TREE_CLEANUP_FAILED,
                    {error.code,
                     error.context ? error.context : kTreeCleanupRecoveryFailed}
                );
            }
            step_ = Step::BEGIN_ORDINARY;
            return false;
        case Step::BEGIN_ORDINARY: {
            auto begun = ordinary_.begin(files, lease_);
            if (!begun) {
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::ORDINARY_TRANSACTION_FAILED,
                    begun.error()
                );
            }
            step_ = Step::ADVANCE_ORDINARY;
            return false;
        }
        case Step::ADVANCE_ORDINARY: {
            auto advanced = ordinary_.advance(
                files,
                lease_,
                scratch_.data(),
                scratch_.size()
            );
            if (!advanced) {
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::ORDINARY_TRANSACTION_FAILED,
                    advanced.error()
                );
            }
            if (!advanced.value()) return false;
            step_ = Step::LOAD_CONDITIONAL;
            return false;
        }
        case Step::LOAD_CONDITIONAL: {
            const auto status = conditional::readJournal(
                files,
                lease_,
                conditional_journal_,
                conditional_present_,
                conditional_corrupt_
            );
            if (status != FileSystemRpcStatus::OK) {
                if (conditional_corrupt_) {
                    step_ = Step::QUARANTINE_CONDITIONAL;
                    return false;
                }
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::CONDITIONAL_FAILED,
                    {conditional::recoveryError(status),
                     kConditionalRecoveryFailed}
                );
            }
            step_ = Step::CLEAN_CONDITIONAL_STAGING;
            return false;
        }
        case Step::QUARANTINE_CONDITIONAL: {
            const auto status = conditional::quarantineCorruptJournal(
                files,
                lease_
            );
            if (status != FileSystemRpcStatus::OK) {
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::CONDITIONAL_FAILED,
                    {conditional::recoveryError(status),
                     kConditionalRecoveryFailed}
                );
            }
            result_.conditionalJournalQuarantined = true;
            conditional_present_ = false;
            step_ = Step::CLEAN_CONDITIONAL_STAGING;
            return false;
        }
        case Step::CLEAN_CONDITIONAL_STAGING: {
            const auto status = conditional::removeIfExists(
                files,
                lease_,
                conditional::JOURNAL_STAGING_PATH
            );
            if (status != FileSystemRpcStatus::OK) {
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::CONDITIONAL_FAILED,
                    {conditional::recoveryError(status),
                     kConditionalRecoveryFailed}
                );
            }
            if (!conditional_present_) {
                advanceAfterConditional_();
                return false;
            }
            step_ = Step::BEGIN_CONDITIONAL;
            return false;
        }
        case Step::BEGIN_CONDITIONAL: {
            auto begun = conditional_.beginRecovery(
                files,
                lease_,
                conditional_journal_
            );
            if (!begun) {
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::CONDITIONAL_FAILED,
                    begun.error()
                );
            }
            step_ = Step::ADVANCE_CONDITIONAL;
            return false;
        }
        case Step::ADVANCE_CONDITIONAL:
            if (!conditional_.advance(files, scratch_.data(), scratch_.size())) {
                return false;
            }
            if (conditional_.status() != FileSystemRpcStatus::OK) {
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::CONDITIONAL_FAILED,
                    {conditional::recoveryError(conditional_.status()),
                     kConditionalRecoveryFailed}
                );
            }
            advanceAfterConditional_();
            return false;

        case Step::RESTORE_BOOT_SESSION: {
            const auto restored = restoreService.restore(state, lease_);
            result_.sessionRestoreStatus = restored.status;
            result_.sessionRestoreBytes = restored.bytes;
            if (!files.owns(lease_, ProductMutationOwner::RECOVERY)) {
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE,
                    {ErrorCode::HARDWARE_NOT_FOUND, kRecoveryLeaseLost}
                );
            }
            step_ = Step::RECONCILE_SETTINGS;
            return false;
        }
        case Step::RECONCILE_SETTINGS:
            result_.settingsStatus =
                state.recoverSettingsFromRamAfterStorageReopen();
            if (result_.settingsStatus != PersistenceWriteStatus::OK) {
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::SETTINGS_FAILED,
                    settingsError(result_.settingsStatus)
                );
            }
            step_ = mode_ == ProductStorageRecoveryMode::BOOT
                ? Step::BEGIN_SESSION_SAVE
                : Step::ADVANCE_SESSION_SAVE;
            return false;

        case Step::BEGIN_SESSION_SAVE: {
            const auto session = autosaveService.beginRecovery(state, lease_);
            copySessionResult_(session);
            if (session.status != ProjectSessionAutosaveService::Status::SAVING) {
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::SESSION_SAVE_FAILED,
                    sessionSaveError(session)
                );
            }
            session_recovery_started_ = true;
            step_ = Step::ADVANCE_SESSION_SAVE;
            return false;
        }
        case Step::ADVANCE_SESSION_SAVE: {
            const auto session = autosaveService.advanceRecovery(state, lease_);
            copySessionResult_(session);
            if (session.status == ProjectSessionAutosaveService::Status::SAVING) {
                return false;
            }
            if (!session.saved()) {
                const auto error = files.storageState() == ProductStorageState::ABSENT
                    ? Error{ErrorCode::HARDWARE_NOT_FOUND,
                            kProductStorageUnavailable}
                    : sessionSaveError(session);
                return fail_(
                    files,
                    autosaveService,
                    ProductStorageRecoveryStatus::SESSION_SAVE_FAILED,
                    error
                );
            }
            session_recovery_started_ = false;
            step_ = Step::COMPLETE_RECOVERY;
            return false;
        }
        case Step::COMPLETE_RECOVERY: {
            auto completed = files.completeRecovery(lease_, true);
            if (!completed) {
                result_.status = files.storageState() == ProductStorageState::ABSENT
                    ? ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE
                    : ProductStorageRecoveryStatus::COMPLETION_FAILED;
                result_.error = completed.error().code;
                result_.errorContext = completed.error().context;
                step_ = Step::FAILED;
                return true;
            }
            result_.status = ProductStorageRecoveryStatus::RECOVERED;
            result_.error = ErrorCode::OK;
            result_.errorContext = nullptr;
            step_ = Step::COMPLETE;
            return true;
        }
        case Step::COMPLETE:
        case Step::FAILED:
            return true;
        case Step::IDLE:
        default:
            return fail_(
                files,
                autosaveService,
                ProductStorageRecoveryStatus::BUSY,
                {ErrorCode::INVALID_STATE, kRecoveryLeaseLost}
            );
    }
}

FLASHMEM void ProductStorageRecoveryPlan::cancel(
    ProductFileService& files,
    ProjectSessionAutosaveService& autosaveService,
    ErrorCode errorCode
) {
    if (session_recovery_started_) {
        autosaveService.cancelRecovery();
        session_recovery_started_ = false;
    }
    result_.status = files.storageState() == ProductStorageState::ABSENT
        ? ProductStorageRecoveryStatus::MEDIA_UNAVAILABLE
        : ProductStorageRecoveryStatus::BUSY;
    result_.error = files.storageState() == ProductStorageState::ABSENT
        ? ErrorCode::HARDWARE_NOT_FOUND
        : errorCode;
    result_.errorContext = files.storageState() == ProductStorageState::ABSENT
        ? kProductStorageUnavailable
        : kRecoveryLeaseLost;
    if (files.owns(lease_, ProductMutationOwner::RECOVERY)) {
        (void)files.completeRecovery(lease_, false, result_.error);
    } else {
        lease_ = ProductMutationLease{};
    }
    step_ = Step::FAILED;
}

ProductPersistenceWorkQuota ProductStorageRecoveryPlan::nextWorkQuota(
    const ProjectSessionAutosaveService& autosaveService
) const {
    if (step_ == Step::BEGIN_TREE_CLEANUP ||
        step_ == Step::ADVANCE_TREE_CLEANUP) {
        return PRODUCT_PERSISTENCE_QUOTA_TREE_CLEANUP;
    }
    if (step_ == Step::ADVANCE_CONDITIONAL) {
        return conditional_.nextWorkClass() ==
                       protocol::filesystem::conditional_mutation::
                           ConditionalPlanWorkClass::ORDINARY_IO
            ? PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO
            : PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE;
    }
    if (step_ == Step::ADVANCE_SESSION_SAVE) {
        return autosaveService.recoveryWorkQuota();
    }
    return PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE;
}

bool ProductStorageRecoveryPlan::active() const {
    return step_ != Step::IDLE && step_ != Step::COMPLETE &&
           step_ != Step::FAILED;
}

bool ProductStorageRecoveryPlan::terminal() const {
    return step_ == Step::COMPLETE || step_ == Step::FAILED;
}

FLASHMEM bool ProductStorageRecoveryPlan::fail_(
    ProductFileService& files,
    ProjectSessionAutosaveService& autosaveService,
    ProductStorageRecoveryStatus status,
    Error error
) {
    if (session_recovery_started_) {
        autosaveService.cancelRecovery();
        session_recovery_started_ = false;
    }
    result_ = failRecovery(files, lease_, result_, status, error);
    step_ = Step::FAILED;
    return true;
}

void ProductStorageRecoveryPlan::copySessionResult_(
    const ProjectSessionAutosaveService::Result& session
) {
    last_work_bytes_ = session.workBytes;
    result_.sessionSaveStatus = session.status;
    result_.sessionSaveFailureStage = session.failureStage;
    result_.sessionSaveBytes = session.bytes;
}

void ProductStorageRecoveryPlan::advanceAfterConditional_() {
    step_ = mode_ == ProductStorageRecoveryMode::BOOT
        ? Step::RESTORE_BOOT_SESSION
        : Step::RECONCILE_SETTINGS;
}

FLASHMEM ProductStorageRecoveryResult ProductStorageRecoveryService::reconcile(
    ProductFileService& files,
    ProjectSessionRestoreService& restoreService,
    ProjectSessionAutosaveService& autosaveService,
    core::state::CoreState& state,
    ProductStorageRecoveryMode mode
) {
    auto plan = core::app::makeExtmemUniqueCold<ProductStorageRecoveryPlan>();
    if (!plan) {
        ProductStorageRecoveryResult result{};
        result.status = ProductStorageRecoveryStatus::RESOURCE_EXHAUSTED;
        result.error = ErrorCode::RESOURCE_EXHAUSTED;
        result.errorContext = kSessionCaptureFailed;
        return result;
    }
    if (!plan->begin(files, autosaveService, state, mode)) {
        return plan->result();
    }
    while (plan->active()) {
        (void)plan->advance(files, restoreService, autosaveService, state);
    }
    return plan->result();
}

}  // namespace core::persistence

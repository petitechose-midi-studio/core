#include "persistence/ProductFileCommitPlan.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "persistence/PersistenceChecksum.hpp"

namespace core::persistence {

namespace transaction = product_file_transaction;
using oc::type::ErrorCode;

namespace {

const char kCommitPlanAlreadyActive[] PROGMEM =
    "product file commit continuation already active";
const char kCommitPlanInvalidLease[] PROGMEM =
    "product file commit continuation lease invalid";
const char kCommitPlanNotActive[] PROGMEM =
    "product file commit continuation not active";
const char kCommitPlanUnsupportedJournal[] PROGMEM =
    "unsupported product file journal version";
const char kCommitPlanAmbiguousJournal[] PROGMEM =
    "ambiguous product file journal sequence";
const char kCommitPlanCorruptJournals[] PROGMEM =
    "both product file journal slots corrupt";
const char kCommitPlanJournalChanged[] PROGMEM =
    "product file journal changed during selection";
const char kCommitPlanRecoveryPending[] PROGMEM =
    "product file recovery pending";
const char kCommitPlanSequenceExhausted[] PROGMEM =
    "product file journal sequence exhausted";
const char kCommitPlanTmpSize[] PROGMEM =
    "product file temporary size mismatch";
const char kCommitPlanTmpIntegrity[] PROGMEM =
    "product file temporary checksum mismatch";
const char kCommitPlanPromotedIntegrity[] PROGMEM =
    "promoted product file checksum mismatch";
const char kCommitPlanIntegrityScratch[] PROGMEM =
    "product file integrity scratch unavailable";
const char kCommitPlanIntegrityShortRead[] PROGMEM =
    "short product file integrity read";
const char kCommitPlanBackupTopology[] PROGMEM =
    "invalid product file backup topology";

oc::type::Error planError(ErrorCode code, const char* context) {
    return {code, context};
}

}  // namespace

FLASHMEM oc::type::Result<void> ProductFileCommitPlan::begin(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* current,
    const char* backup,
    const char* tmp,
    uint32_t expectedSize,
    uint32_t expectedCrc32
) {
    if (active()) {
        return oc::type::Result<void>::err(
            planError(ErrorCode::INVALID_STATE, kCommitPlanAlreadyActive)
        );
    }
    reset();
    if (!files.owns(lease)) {
        return oc::type::Result<void>::err(
            planError(ErrorCode::INVALID_STATE, kCommitPlanInvalidLease)
        );
    }

    auto normalized = transaction::normalizePaths(
        files,
        slots_[0],
        current,
        tmp,
        backup
    );
    if (!normalized) return normalized;
    for (uint8_t index = 0U; index < transaction::PATH_COUNT; ++index) {
        std::memcpy(
            requested_paths_[index],
            slots_[0].path(static_cast<transaction::PathIndex>(index)),
            transaction::PATH_CAPACITY
        );
    }
    slots_[0] = {};
    expected_size_ = expectedSize;
    expected_crc32_ = expectedCrc32;
    step_ = Step::READ_SLOT_A;
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<bool> ProductFileCommitPlan::advance(
    ProductFileService& files,
    const ProductMutationLease& lease,
    uint8_t* scratch,
    size_t scratchSize
) {
    if (!active() || !files.owns(lease)) {
        return fail_(
            planError(ErrorCode::INVALID_STATE, kCommitPlanNotActive),
            mapped_
        );
    }

    switch (step_) {
        case Step::READ_SLOT_A: {
            auto read = transaction::readJournalSlot(files, lease, 0U, slots_[0]);
            if (!read) return fail_(read.error(), true);
            observations_[0] = read.value();
            step_ = Step::READ_SLOT_B;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::READ_SLOT_B: {
            auto read = transaction::readJournalSlot(files, lease, 1U, slots_[1]);
            if (!read) return fail_(read.error(), true);
            observations_[1] = read.value();
            return selectSlots_();
        }
        case Step::VERIFY_SELECTED_SLOT: {
            const uint8_t target = static_cast<uint8_t>(1U - active_workspace_);
            slots_[target] = {};
            auto verified = transaction::readJournalSlot(
                files,
                lease,
                selected_slot_,
                slots_[target]
            );
            if (!verified ||
                verified.value().state != transaction::JournalSlotState::VALID ||
                verified.value().sequence != observations_[selected_slot_].sequence) {
                return fail_(
                    verified
                        ? planError(ErrorCode::STORAGE_CORRUPT,
                                    kCommitPlanJournalChanged)
                        : verified.error(),
                    true
                );
            }
            active_workspace_ = target;
            selected_present_ = true;
            return initializeCommitWorkspace_();
        }
        case Step::CLEAN_CORRUPT_SLOT: {
            auto removed = deleteProductFileIfExists(
                files,
                lease,
                transaction::journalSlotPath(corrupt_slot_)
            );
            if (!removed) return fail_(removed.error(), true);
            active_workspace_ = 0U;
            slots_[0] = {};
            selected_present_ = false;
            return initializeCommitWorkspace_();
        }
        case Step::INSPECT_TMP: {
            auto tmp = transaction::inspectFile(
                files,
                lease,
                workspace_().path(transaction::TMP_PATH)
            );
            if (!tmp) return fail_(tmp.error(), false);
            if (!tmp.value().exists || tmp.value().size != expected_size_) {
                return fail_(
                    planError(ErrorCode::STORAGE_WRITE_FAILED, kCommitPlanTmpSize),
                    false
                );
            }
            step_ = Step::FLUSH_TMP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::FLUSH_TMP: {
            auto flushed = files.flush(
                lease,
                workspace_().path(transaction::TMP_PATH)
            );
            if (!flushed) return fail_(flushed.error(), false);
            beginIntegrityCheck_();
            step_ = Step::VERIFY_TMP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::VERIFY_TMP:
            return advanceIntegrityCheck_(
                files,
                lease,
                workspace_().path(transaction::TMP_PATH),
                scratch,
                scratchSize,
                Step::INSPECT_CURRENT,
                false,
                kCommitPlanTmpIntegrity
            );
        case Step::INSPECT_CURRENT: {
            auto current = transaction::inspectFile(
                files,
                lease,
                workspace_().path(transaction::FINAL_PATH)
            );
            if (!current) return fail_(current.error(), false);
            current_ = current.value();
            step_ = Step::INSPECT_BACKUP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::INSPECT_BACKUP: {
            auto backup = transaction::inspectFile(
                files,
                lease,
                workspace_().path(transaction::BACKUP_PATH)
            );
            if (!backup) return fail_(backup.error(), false);
            backup_ = backup.value();
            if (current_.exists && backup_.exists) {
                step_ = Step::CLEAN_STALE_BACKUP;
            } else {
                workspace_().hadCurrent = current_.exists || backup_.exists;
                step_ = Step::PERSIST_PREPARED;
            }
            return oc::type::Result<bool>::ok(false);
        }
        case Step::CLEAN_STALE_BACKUP: {
            auto removed = transaction::cleanupMappedPath(
                files,
                lease,
                workspace_().path(transaction::BACKUP_PATH)
            );
            if (!removed) return fail_(removed.error(), false);
            backup_ = {};
            workspace_().hadCurrent = current_.exists;
            step_ = Step::PERSIST_PREPARED;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::PERSIST_PREPARED: {
            recovery_required_on_error_ = true;
            auto persisted = transaction::persistPhase(
                files,
                lease,
                workspace_(),
                ProductFileTransactionPhase::PREPARED
            );
            if (!persisted) return fail_(persisted.error(), true);
            mapped_ = true;
            step_ = workspace_().hadCurrent
                ? Step::BACK_UP_CURRENT
                : Step::PROMOTE_TMP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::BACK_UP_CURRENT: {
            if (current_.exists && !backup_.exists) {
                auto renamed = files.rename(
                    lease,
                    workspace_().path(transaction::FINAL_PATH),
                    workspace_().path(transaction::BACKUP_PATH)
                );
                if (!renamed) return fail_(renamed.error(), true);
            } else if (current_.exists || !backup_.exists) {
                return fail_(
                    planError(ErrorCode::STORAGE_CORRUPT,
                              kCommitPlanBackupTopology),
                    true
                );
            }
            auto flushed = files.flush(
                lease,
                workspace_().path(transaction::BACKUP_PATH)
            );
            if (!flushed) return fail_(flushed.error(), true);
            step_ = Step::PERSIST_BACKED_UP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::PERSIST_BACKED_UP: {
            auto persisted = transaction::persistPhase(
                files,
                lease,
                workspace_(),
                ProductFileTransactionPhase::BACKED_UP
            );
            if (!persisted) return fail_(persisted.error(), true);
            step_ = Step::PROMOTE_TMP;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::PROMOTE_TMP: {
            auto renamed = files.rename(
                lease,
                workspace_().path(transaction::TMP_PATH),
                workspace_().path(transaction::FINAL_PATH)
            );
            if (!renamed) return fail_(renamed.error(), true);
            auto flushed = files.flush(
                lease,
                workspace_().path(transaction::FINAL_PATH)
            );
            if (!flushed) return fail_(flushed.error(), true);
            beginIntegrityCheck_();
            step_ = Step::VERIFY_PROMOTED;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::VERIFY_PROMOTED:
            return advanceIntegrityCheck_(
                files,
                lease,
                workspace_().path(transaction::FINAL_PATH),
                scratch,
                scratchSize,
                Step::PERSIST_PROMOTED,
                true,
                kCommitPlanPromotedIntegrity
            );
        case Step::PERSIST_PROMOTED: {
            auto persisted = transaction::persistPhase(
                files,
                lease,
                workspace_(),
                ProductFileTransactionPhase::PROMOTED
            );
            if (!persisted) return fail_(persisted.error(), true);
            step_ = workspace_().hadCurrent
                ? Step::CLEAN_BACKUP
                : Step::PERSIST_COMMITTED;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::CLEAN_BACKUP: {
            auto removed = transaction::cleanupMappedPath(
                files,
                lease,
                workspace_().path(transaction::BACKUP_PATH)
            );
            if (!removed) return fail_(removed.error(), true);
            step_ = Step::PERSIST_COMMITTED;
            return oc::type::Result<bool>::ok(false);
        }
        case Step::PERSIST_COMMITTED: {
            auto persisted = transaction::persistPhase(
                files,
                lease,
                workspace_(),
                ProductFileTransactionPhase::COMMITTED
            );
            if (!persisted) return fail_(persisted.error(), true);
            step_ = Step::COMPLETE;
            return oc::type::Result<bool>::ok(true);
        }
        case Step::COMPLETE:
            return oc::type::Result<bool>::ok(true);
        case Step::IDLE:
        case Step::FAILED:
        default:
            return fail_(
                planError(ErrorCode::INVALID_STATE, kCommitPlanNotActive),
                mapped_
            );
    }
}

FLASHMEM void ProductFileCommitPlan::reset() {
    slots_[0] = {};
    slots_[1] = {};
    observations_[0] = {};
    observations_[1] = {};
    std::memset(requested_paths_, 0, sizeof(requested_paths_));
    current_ = {};
    backup_ = {};
    expected_size_ = 0U;
    expected_crc32_ = 0U;
    integrity_crc_state_ = 0U;
    integrity_offset_ = 0U;
    step_ = Step::IDLE;
    active_workspace_ = 0U;
    selected_slot_ = 0U;
    corrupt_slot_ = 0U;
    selected_present_ = false;
    mapped_ = false;
    recovery_required_on_error_ = false;
}

FLASHMEM bool ProductFileCommitPlan::active() const {
    return step_ != Step::IDLE && step_ != Step::COMPLETE &&
           step_ != Step::FAILED;
}

FLASHMEM bool ProductFileCommitPlan::complete() const {
    return step_ == Step::COMPLETE;
}

FLASHMEM bool ProductFileCommitPlan::nextAdvanceReadsData() const {
    return (step_ == Step::VERIFY_TMP || step_ == Step::VERIFY_PROMOTED) &&
           integrity_offset_ < expected_size_;
}

FLASHMEM oc::type::Result<bool> ProductFileCommitPlan::selectSlots_() {
    using transaction::JournalSlotState;
    const auto first = observations_[0];
    const auto second = observations_[1];
    if (first.state == JournalSlotState::UNSUPPORTED ||
        second.state == JournalSlotState::UNSUPPORTED) {
        return fail_(
            planError(ErrorCode::INVALID_STATE, kCommitPlanUnsupportedJournal),
            true
        );
    }
    const bool firstValid = first.state == JournalSlotState::VALID;
    const bool secondValid = second.state == JournalSlotState::VALID;
    if (firstValid && secondValid && first.sequence == second.sequence) {
        return fail_(
            planError(ErrorCode::STORAGE_CORRUPT, kCommitPlanAmbiguousJournal),
            true
        );
    }
    if (firstValid || secondValid) {
        selected_slot_ = !secondValid ||
                                 (firstValid && first.sequence > second.sequence)
            ? 0U
            : 1U;
        active_workspace_ = selected_slot_;
        step_ = Step::VERIFY_SELECTED_SLOT;
        return oc::type::Result<bool>::ok(false);
    }

    const bool firstCorrupt = first.state == JournalSlotState::CORRUPT;
    const bool secondCorrupt = second.state == JournalSlotState::CORRUPT;
    if (firstCorrupt && secondCorrupt) {
        return fail_(
            planError(ErrorCode::STORAGE_CORRUPT, kCommitPlanCorruptJournals),
            true
        );
    }
    if (firstCorrupt || secondCorrupt) {
        corrupt_slot_ = firstCorrupt ? 0U : 1U;
        step_ = Step::CLEAN_CORRUPT_SLOT;
        return oc::type::Result<bool>::ok(false);
    }
    active_workspace_ = 0U;
    slots_[0] = {};
    selected_present_ = false;
    return initializeCommitWorkspace_();
}

FLASHMEM oc::type::Result<bool>
ProductFileCommitPlan::initializeCommitWorkspace_() {
    auto& workspace = workspace_();
    if (selected_present_) {
        if (!transaction::phaseTerminal(workspace.phase)) {
            return fail_(
                planError(ErrorCode::HARDWARE_BUSY, kCommitPlanRecoveryPending),
                true
            );
        }
        if (workspace.sequence > std::numeric_limits<uint64_t>::max() - 4U) {
            return fail_(
                planError(ErrorCode::RESOURCE_EXHAUSTED,
                          kCommitPlanSequenceExhausted),
                false
            );
        }
    }
    for (uint8_t index = 0U; index < transaction::PATH_COUNT; ++index) {
        std::memcpy(
            workspace.path(static_cast<transaction::PathIndex>(index)),
            requested_paths_[index],
            transaction::PATH_CAPACITY
        );
    }
    workspace.expectedSize = expected_size_;
    workspace.expectedCrc32 = expected_crc32_;
    workspace.hasExpectedCrc32 = true;
    workspace.hadCurrent = false;
    step_ = Step::INSPECT_TMP;
    return oc::type::Result<bool>::ok(false);
}

FLASHMEM void ProductFileCommitPlan::beginIntegrityCheck_() {
    integrity_crc_state_ = checksum::CRC32_INITIAL_STATE;
    integrity_offset_ = 0U;
}

FLASHMEM oc::type::Result<bool> ProductFileCommitPlan::advanceIntegrityCheck_(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path,
    uint8_t* scratch,
    size_t scratchSize,
    Step successStep,
    bool recoveryRequired,
    const char* mismatchContext
) {
    if (integrity_offset_ < expected_size_) {
        if (scratch == nullptr || scratchSize == 0U) {
            return fail_(
                planError(ErrorCode::INVALID_ARGUMENT, kCommitPlanIntegrityScratch),
                recoveryRequired
            );
        }
        const size_t remaining = expected_size_ - integrity_offset_;
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
        if (!read) return fail_(read.error(), recoveryRequired);
        if (read.value() == 0U || read.value() > requested) {
            return fail_(
                planError(ErrorCode::STORAGE_READ_FAILED,
                          kCommitPlanIntegrityShortRead),
                recoveryRequired
            );
        }
        integrity_crc_state_ = checksum::crc32Update(
            integrity_crc_state_,
            scratch,
            read.value()
        );
        integrity_offset_ += static_cast<uint32_t>(read.value());
        if (integrity_offset_ < expected_size_) {
            return oc::type::Result<bool>::ok(false);
        }
    }

    if (checksum::crc32Finish(integrity_crc_state_) != expected_crc32_) {
        return fail_(
            planError(ErrorCode::STORAGE_CORRUPT, mismatchContext),
            recoveryRequired
        );
    }
    step_ = successStep;
    return oc::type::Result<bool>::ok(false);
}

FLASHMEM oc::type::Result<bool> ProductFileCommitPlan::fail_(
    oc::type::Error error,
    bool recoveryRequired
) {
    recovery_required_on_error_ =
        recovery_required_on_error_ || recoveryRequired;
    step_ = Step::FAILED;
    return oc::type::Result<bool>::err(error);
}

product_file_transaction::JournalWorkspace& ProductFileCommitPlan::workspace_() {
    return slots_[active_workspace_];
}

const product_file_transaction::JournalWorkspace&
ProductFileCommitPlan::workspace_() const {
    return slots_[active_workspace_];
}

}  // namespace core::persistence

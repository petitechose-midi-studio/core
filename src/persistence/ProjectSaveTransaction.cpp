#include "persistence/ProjectSaveTransaction.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectFileWorkspace.hpp"
#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

FLASHMEM bool validSavePaths(AtomicProductFilePaths paths) {
    return paths.directory != nullptr && paths.current != nullptr &&
           paths.backup != nullptr && paths.tmp != nullptr;
}

}  // namespace

FLASHMEM ProjectSaveTransaction::ProjectSaveTransaction(ProductFileService& files)
    : files_(files) {}

FLASHMEM ProjectSaveTransaction::~ProjectSaveTransaction() {
    cancel();
}

FLASHMEM oc::type::Result<void> ProjectSaveTransaction::begin(
    const core::state::project::ProjectSnapshot& snapshot,
    AtomicProductFilePaths paths
) {
    if (active()) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "project save already active"}
        );
    }
    if (!validSavePaths(paths)) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid project save paths"}
        );
    }

    auto acquired = files_.acquireMutation(ProductMutationOwner::PROJECT);
    if (!acquired) {
        return oc::type::Result<void>::err(acquired.error());
    }

    lease_ = std::move(acquired.value());
    snapshot_ = &snapshot;
    paths_ = paths;
    phase_ = Phase::PREPARE;
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProjectSaveTransaction::beginWithRecoveryLease(
    const core::state::project::ProjectSnapshot& snapshot,
    AtomicProductFilePaths paths,
    const ProductMutationLease& recoveryLease
) {
    if (active()) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "project save already active"}
        );
    }
    if (!validSavePaths(paths)) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid project save paths"}
        );
    }
    if (!files_.owns(recoveryLease, ProductMutationOwner::RECOVERY)) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "exact recovery lease required"}
        );
    }

    snapshot_ = &snapshot;
    paths_ = paths;
    phase_ = Phase::PREPARE;
    recovery_lease_ = &recoveryLease;
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<ProjectSaveProgress> ProjectSaveTransaction::advance(
    ProjectSaveStage* attemptedStage
) {
    const ProductMutationLease* activeLease = recovery_lease_ != nullptr
        ? recovery_lease_
        : &lease_;
    if (!activeLease->valid()) {
        if (attemptedStage) *attemptedStage = ProjectSaveStage::PREPARE;
        return oc::type::Result<ProjectSaveProgress>::err(
            {ErrorCode::INVALID_STATE, "project save lease is not active"}
        );
    }
    if (attemptedStage) *attemptedStage = currentStage_();
    return advance_(*activeLease, recovery_lease_ == nullptr);
}

FLASHMEM oc::type::Result<ProjectSaveProgress>
ProjectSaveTransaction::saveToCompletionWithRecoveryLease(
    const core::state::project::ProjectSnapshot& snapshot,
    AtomicProductFilePaths paths,
    const ProductMutationLease& recoveryLease,
    ProjectSaveStage* failedStage
) {
    if (failedStage) *failedStage = ProjectSaveStage::PREPARE;
    auto begun = beginWithRecoveryLease(snapshot, paths, recoveryLease);
    if (!begun) {
        return oc::type::Result<ProjectSaveProgress>::err(begun.error());
    }

    while (active()) {
        const auto attemptedStage = currentStage_();
        auto progress = advance();
        if (!progress) {
            if (failedStage) *failedStage = attemptedStage;
            return progress;
        }
        if (progress.value().complete) {
            return progress;
        }
    }

    return oc::type::Result<ProjectSaveProgress>::err(
        {ErrorCode::INVALID_STATE, "project recovery save stopped before commit"}
    );
}

FLASHMEM oc::type::Result<ProjectSaveProgress> ProjectSaveTransaction::advance_(
    const ProductMutationLease& lease,
    bool releaseLeaseOnCompletion
) {
    if (!active() || snapshot_ == nullptr) {
        return oc::type::Result<ProjectSaveProgress>::err(
            {ErrorCode::INVALID_STATE, "project save is not active"}
        );
    }

    auto borrowedWorkspace = files_.projectWriteWorkspace(lease);
    if (!borrowedWorkspace) {
        const auto error = borrowedWorkspace.error();
        cancel_(lease, releaseLeaseOnCompletion);
        return oc::type::Result<ProjectSaveProgress>::err(error);
    }
    auto& workspace = *borrowedWorkspace.value();

    switch (phase_) {
        case Phase::PREPARE: {
            OC_PERF_SCOPE(perfPrepare, "persistence.project-save.prepare");
            auto ensureDir = files_.createDirectory(lease, paths_.directory);
            if (!ensureDir) {
                const auto error = ensureDir.error();
                cancel_(lease, releaseLeaseOnCompletion);
                return oc::type::Result<ProjectSaveProgress>::err(error);
            }

            auto deleteTmp = deleteProductFileIfExists(files_, lease, paths_.tmp);
            if (!deleteTmp) {
                const auto error = deleteTmp.error();
                cancel_(lease, releaseLeaseOnCompletion);
                return oc::type::Result<ProjectSaveProgress>::err(error);
            }
            tmp_prepared_ = true;
            if (!workspace.prepare()) {
                cancel_(lease, releaseLeaseOnCompletion);
                return oc::type::Result<ProjectSaveProgress>::err(
                    {ErrorCode::RESOURCE_EXHAUSTED, "project save buffer"}
                );
            }
            phase_ = Phase::ENCODE;
            return oc::type::Result<ProjectSaveProgress>::ok({
                .completedStage = ProjectSaveStage::PREPARE,
            });
        }

        case Phase::ENCODE: {
            OC_PERF_SCOPE(perfEncode, "persistence.project-save.encode");
            auto encoded = core::persistence::project_snapshot_codec::encodeProjectSnapshot(
                *snapshot_,
                workspace.data(),
                workspace.capacity(),
                workspace.codecWorkspace()
            );
            if (encoded.status != core::persistence::project_file::Status::OK) {
                cancel_(lease, releaseLeaseOnCompletion);
                return oc::type::Result<ProjectSaveProgress>::err(
                    {ErrorCode::STORAGE_WRITE_FAILED, "project encode failed"}
                );
            }
            encoded_size_ = encoded.bytesWritten;
            if (encoded_size_ == 0 || encoded_size_ > workspace.capacity()) {
                cancel_(lease, releaseLeaseOnCompletion);
                return oc::type::Result<ProjectSaveProgress>::err(
                    {ErrorCode::STORAGE_WRITE_FAILED, "invalid encoded project size"}
                );
            }
            OC_PERF_UNITS(perfEncode, encoded_size_, 0U);
            phase_ = Phase::BEGIN_WRITE;
            return oc::type::Result<ProjectSaveProgress>::ok({
                .completedStage = ProjectSaveStage::ENCODE,
                .workBytes = encoded_size_,
            });
        }

        case Phase::BEGIN_WRITE: {
            OC_PERF_SCOPE(perfBeginWrite, "persistence.project-save.begin-write");
            auto beginWrite = files_.beginWrite(lease, paths_.tmp, encoded_size_);
            if (!beginWrite) {
                const auto error = beginWrite.error();
                cancel_(lease, releaseLeaseOnCompletion);
                return oc::type::Result<ProjectSaveProgress>::err(error);
            }
            phase_ = Phase::WRITE;
            return oc::type::Result<ProjectSaveProgress>::ok({
                .completedStage = ProjectSaveStage::WRITE,
            });
        }

        case Phase::WRITE: {
            OC_PERF_SCOPE(perfWrite, "persistence.project-save.write-chunk");
            const uint32_t chunkSize = std::min<uint32_t>(
                PROJECT_FILE_WRITE_CHUNK_SIZE,
                encoded_size_ - write_offset_
            );
            auto write = files_.appendWrite(
                lease,
                workspace.data() + write_offset_,
                chunkSize
            );
            if (!write || write.value() != chunkSize) {
                const auto error = write
                    ? oc::type::Error{
                          ErrorCode::STORAGE_WRITE_FAILED,
                          "short project write"
                      }
                    : write.error();
                (void)files_.abortWrite(lease);
                cancel_(lease, releaseLeaseOnCompletion);
                return oc::type::Result<ProjectSaveProgress>::err(error);
            }
            write_offset_ += chunkSize;
            OC_PERF_UNITS(perfWrite, chunkSize, write_offset_);

            if (write_offset_ == encoded_size_) {
                phase_ = Phase::FINISH_WRITE;
            }
            return oc::type::Result<ProjectSaveProgress>::ok({
                .completedStage = ProjectSaveStage::WRITE,
                .bytesWritten = write_offset_,
            });
        }

        case Phase::FINISH_WRITE: {
            OC_PERF_SCOPE(perfFinishWrite, "persistence.project-save.finish-write");
            auto finish = files_.finishWrite(lease);
            if (!finish) {
                const auto error = finish.error();
                cancel_(lease, releaseLeaseOnCompletion);
                return oc::type::Result<ProjectSaveProgress>::err(error);
            }
            phase_ = Phase::COMMIT;
            return oc::type::Result<ProjectSaveProgress>::ok({
                .completedStage = ProjectSaveStage::WRITE,
                .bytesWritten = write_offset_,
            });
        }

        case Phase::COMMIT: {
            OC_PERF_SCOPE(perfCommit, "persistence.project-save.commit");
            auto& commitPlan = commit_plan_started_
                ? workspace.commitPlan()
                : workspace.resetCommitPlan();
            if (!commit_plan_started_) {
                auto begun = commitPlan.begin(
                    files_,
                    lease,
                    paths_.current,
                    paths_.backup,
                    paths_.tmp,
                    encoded_size_
                );
                if (!begun) {
                    const auto error = begun.error();
                    cancel_(lease, releaseLeaseOnCompletion);
                    return oc::type::Result<ProjectSaveProgress>::err(error);
                }
                commit_plan_started_ = true;
            }

            auto committed = commitPlan.advance(files_, lease);
            if (!committed) {
                const auto error = committed.error();
                if (commitPlan.requiresRecoveryOnFailure()) {
                    (void)files_.requireRecovery(lease, error.code);
                }
                cancel_(lease, releaseLeaseOnCompletion);
                return oc::type::Result<ProjectSaveProgress>::err(error);
            }
            if (!committed.value()) {
                return oc::type::Result<ProjectSaveProgress>::ok({
                    .completedStage = ProjectSaveStage::COMMIT,
                    .bytesWritten = encoded_size_,
                });
            }

            const uint32_t bytesWritten = encoded_size_;
            OC_PERF_UNITS(perfCommit, bytesWritten, 0U);
            tmp_prepared_ = false;
            oc::type::Result<void> released = oc::type::Result<void>::ok();
            if (releaseLeaseOnCompletion) {
                released = files_.releaseMutation(lease_);
            }
            reset_();
            if (!released) {
                return oc::type::Result<ProjectSaveProgress>::err(released.error());
            }
            return oc::type::Result<ProjectSaveProgress>::ok({
                .completedStage = ProjectSaveStage::COMMIT,
                .complete = true,
                .bytesWritten = bytesWritten,
            });
        }

        case Phase::IDLE:
            break;
    }

    return oc::type::Result<ProjectSaveProgress>::err(
        {ErrorCode::INVALID_STATE, "invalid project save phase"}
    );
}

FLASHMEM void ProjectSaveTransaction::cancel() {
    const ProductMutationLease* activeLease = recovery_lease_ != nullptr
        ? recovery_lease_
        : &lease_;
    if (!activeLease->valid()) {
        reset_();
        return;
    }
    cancel_(*activeLease, recovery_lease_ == nullptr);
}

FLASHMEM void ProjectSaveTransaction::cancel_(
    const ProductMutationLease& lease,
    bool releaseLease
) {
    if (files_.owns(lease)) {
        if (files_.writeSessionActive()) {
            (void)files_.abortWrite(lease);
        }
        // Once PREPARED has been durably mapped, deleting tmp would destroy
        // the only recoverable continuation. Mark the medium degraded first;
        // cleanupTmp_ will then preserve every journal-owned artifact.
        if (commit_plan_started_) {
            auto workspace = files_.projectWriteWorkspace(lease);
            if (workspace && workspace.value()->commitPlan().mapped()) {
                (void)files_.requireRecovery(lease, ErrorCode::INVALID_STATE);
            }
        }
        cleanupTmp_(lease);
    }
    if (releaseLease && lease_.valid()) {
        (void)files_.releaseMutation(lease_);
    }
    reset_();
}

bool ProjectSaveTransaction::active() const {
    return phase_ != Phase::IDLE;
}

bool ProjectSaveTransaction::writeSessionActive() const {
    const ProductMutationLease* activeLease = recovery_lease_ != nullptr
        ? recovery_lease_
        : &lease_;
    return activeLease->valid() && files_.owns(*activeLease) &&
           files_.writeSessionActive();
}

ProjectSaveStage ProjectSaveTransaction::currentStage_() const {
    switch (phase_) {
        case Phase::ENCODE:
            return ProjectSaveStage::ENCODE;
        case Phase::WRITE:
        case Phase::BEGIN_WRITE:
        case Phase::FINISH_WRITE:
            return ProjectSaveStage::WRITE;
        case Phase::COMMIT:
            return ProjectSaveStage::COMMIT;
        case Phase::IDLE:
        case Phase::PREPARE:
        default:
            return ProjectSaveStage::PREPARE;
    }
}

FLASHMEM void ProjectSaveTransaction::reset_() {
    snapshot_ = nullptr;
    paths_ = {};
    phase_ = Phase::IDLE;
    encoded_size_ = 0;
    write_offset_ = 0;
    tmp_prepared_ = false;
    commit_plan_started_ = false;
    lease_ = ProductMutationLease{};
    recovery_lease_ = nullptr;
}

FLASHMEM void ProjectSaveTransaction::cleanupTmp_(
    const ProductMutationLease& lease
) {
    if (tmp_prepared_ && paths_.tmp != nullptr && files_.owns(lease)) {
        if (!files_.recoveryRequired(lease)) {
            (void)deleteProductFileIfExists(files_, lease, paths_.tmp);
        }
    }
}

}  // namespace core::persistence

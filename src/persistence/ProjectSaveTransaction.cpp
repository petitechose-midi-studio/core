#include "persistence/ProjectSaveTransaction.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

FLASHMEM bool validSavePaths(AtomicProductFilePaths paths) {
    return paths.directory != nullptr && paths.current != nullptr &&
           paths.backup != nullptr && paths.tmp != nullptr;
}

}  // namespace

FLASHMEM ProjectSaveTransaction::ProjectSaveTransaction(
    ProductFileService& files,
    ProjectFileWorkspace& workspace
) : files_(files), workspace_(workspace) {}

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

    snapshot_ = &snapshot;
    paths_ = paths;
    phase_ = Phase::PREPARE;
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<ProjectSaveProgress> ProjectSaveTransaction::advance() {
    if (!active() || snapshot_ == nullptr) {
        return oc::type::Result<ProjectSaveProgress>::err(
            {ErrorCode::INVALID_STATE, "project save is not active"}
        );
    }

    switch (phase_) {
        case Phase::PREPARE: {
            OC_PERF_SCOPE(perfPrepare, "persistence.project-save.prepare");
            auto ensureDir = files_.createDirectory(paths_.directory);
            if (!ensureDir) {
                const auto error = ensureDir.error();
                reset_();
                return oc::type::Result<ProjectSaveProgress>::err(error);
            }

            auto removeTmp = removeProductFileIfExists(files_, paths_.tmp);
            if (!removeTmp) {
                const auto error = removeTmp.error();
                reset_();
                return oc::type::Result<ProjectSaveProgress>::err(error);
            }
            tmp_prepared_ = true;
            if (!workspace_.prepare()) {
                cleanupTmp_();
                reset_();
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
                workspace_.data(),
                workspace_.capacity(),
                workspace_.codecWorkspace()
            );
            if (encoded.status != core::persistence::project_file::Status::OK) {
                cleanupTmp_();
                reset_();
                return oc::type::Result<ProjectSaveProgress>::err(
                    {ErrorCode::STORAGE_WRITE_FAILED, "project encode failed"}
                );
            }
            encoded_size_ = encoded.bytesWritten;
            if (encoded_size_ == 0 || encoded_size_ > workspace_.capacity()) {
                cleanupTmp_();
                reset_();
                return oc::type::Result<ProjectSaveProgress>::err(
                    {ErrorCode::STORAGE_WRITE_FAILED, "invalid encoded project size"}
                );
            }
            OC_PERF_UNITS(perfEncode, encoded_size_, 0U);
            phase_ = Phase::WRITE;
            return oc::type::Result<ProjectSaveProgress>::ok({
                .completedStage = ProjectSaveStage::ENCODE,
            });
        }

        case Phase::WRITE: {
            OC_PERF_SCOPE(perfWrite, "persistence.project-save.write-chunk");
            if (!write_session_active_) {
                auto beginWrite = files_.beginWrite(paths_.tmp, encoded_size_);
                if (!beginWrite) {
                    const auto error = beginWrite.error();
                    cleanupTmp_();
                    reset_();
                    return oc::type::Result<ProjectSaveProgress>::err(error);
                }
                write_session_active_ = true;
            }

            const uint32_t chunkSize = std::min<uint32_t>(
                PROJECT_FILE_WRITE_CHUNK_SIZE,
                encoded_size_ - write_offset_
            );
            auto write = files_.appendWrite(workspace_.data() + write_offset_, chunkSize);
            if (!write || write.value() != chunkSize) {
                const auto error = write
                    ? oc::type::Error{
                          ErrorCode::STORAGE_WRITE_FAILED,
                          "short project write"
                      }
                    : write.error();
                files_.abortWrite();
                write_session_active_ = false;
                cleanupTmp_();
                reset_();
                return oc::type::Result<ProjectSaveProgress>::err(error);
            }
            write_offset_ += chunkSize;
            OC_PERF_UNITS(perfWrite, chunkSize, write_offset_);

            if (write_offset_ == encoded_size_) {
                auto finish = files_.finishWrite();
                write_session_active_ = false;
                if (!finish) {
                    const auto error = finish.error();
                    cleanupTmp_();
                    reset_();
                    return oc::type::Result<ProjectSaveProgress>::err(error);
                }
                phase_ = Phase::COMMIT;
            }
            return oc::type::Result<ProjectSaveProgress>::ok({
                .completedStage = ProjectSaveStage::WRITE,
                .bytesWritten = write_offset_,
            });
        }

        case Phase::COMMIT: {
            OC_PERF_SCOPE(perfCommit, "persistence.project-save.commit");
            auto commit = commitProductFileTemp(
                files_, paths_.current, paths_.backup, paths_.tmp
            );
            if (!commit) {
                const auto error = commit.error();
                cleanupTmp_();
                reset_();
                return oc::type::Result<ProjectSaveProgress>::err(error);
            }

            const uint32_t bytesWritten = encoded_size_;
            OC_PERF_UNITS(perfCommit, bytesWritten, 0U);
            tmp_prepared_ = false;
            reset_();
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
    if (write_session_active_) {
        files_.abortWrite();
        write_session_active_ = false;
    }
    cleanupTmp_();
    reset_();
}

bool ProjectSaveTransaction::active() const {
    return phase_ != Phase::IDLE;
}

bool ProjectSaveTransaction::writeSessionActive() const {
    return write_session_active_;
}

FLASHMEM void ProjectSaveTransaction::reset_() {
    snapshot_ = nullptr;
    paths_ = {};
    phase_ = Phase::IDLE;
    encoded_size_ = 0;
    write_offset_ = 0;
    tmp_prepared_ = false;
    write_session_active_ = false;
}

FLASHMEM void ProjectSaveTransaction::cleanupTmp_() {
    if (tmp_prepared_ && paths_.tmp != nullptr) {
        (void)removeProductFileIfExists(files_, paths_.tmp);
    }
}

}  // namespace core::persistence

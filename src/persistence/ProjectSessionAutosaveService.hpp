#pragma once

#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/ProjectSaveTransaction.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::state {
struct CoreState;
}

namespace core::persistence {

class ProjectSessionStore;
class ProductMutationLease;

class ProjectSessionAutosaveService {
public:
    using MicrosProvider = uint32_t (*)();

    enum class Status : uint8_t {
        IDLE = 0,
        WAITING,
        SAVING,
        BLOCKED,
        SAVED,
        CAPTURE_FAILED,
        SAVE_FAILED,
    };

    enum class FailureStage : uint8_t {
        NONE = 0,
        CAPTURE,
        PREPARE,
        ENCODE,
        WRITE,
        COMMIT,
        ACKNOWLEDGE,
    };

    struct Result {
        Status status = Status::IDLE;
        uint32_t bytes = 0;
        uint32_t modifiedCounter = 0;
        uint32_t workBytes = 0;
        FailureStage failureStage = FailureStage::NONE;
        oc::type::ErrorCode error = oc::type::ErrorCode::OK;
        const char* errorContext = nullptr;

        bool saved() const {
            return status == Status::SAVED;
        }
    };

    static const char* failureStageLabel(FailureStage stage);

    explicit ProjectSessionAutosaveService(ProjectSessionStore& store,
                                           uint32_t delayMs = 0,
                                           MicrosProvider microsProvider = nullptr);
    ~ProjectSessionAutosaveService();

    Result update(core::state::CoreState& state,
                  uint32_t nowMs,
                  bool mutationPending = false,
                  bool playbackActive = false);
    Result flush(core::state::CoreState& state);
    Result flushRecovery(
        core::state::CoreState& state,
        const ProductMutationLease& recoveryLease
    );
    Result beginRecovery(
        core::state::CoreState& state,
        const ProductMutationLease& recoveryLease
    );
    Result advanceRecovery(
        core::state::CoreState& state,
        const ProductMutationLease& recoveryLease
    );
    void cancelRecovery();
    ProductPersistenceWorkQuota recoveryWorkQuota() const;
    bool inspectPersistenceJob(ProductPersistenceJobSnapshot& snapshot) const;

private:
    bool beginCapture_(core::state::CoreState& state);
    Result startCapture_(core::state::CoreState& state, uint32_t nowMs);
    Result updatePending_(core::state::CoreState& state,
                          uint32_t nowMs,
                          bool mutationPending,
                          bool playbackActive,
                          bool inProgress);
    Result advanceOrdinary_(core::state::CoreState& state, uint32_t nowMs);
    Result advanceCapture_(core::state::CoreState& state);
    Result advanceRecoveryCapture_(
        core::state::CoreState& state,
        const ProductMutationLease& recoveryLease
    );
    Result advanceSave_(core::state::CoreState& state);
    ProductPersistenceWorkQuota nextWorkQuota_() const;
    void requestOrdinaryCancel_(Status status);
    bool ordinaryCancelPending_() const;
    void cancelInFlight_();
    void cancelOrdinary_();
    bool inProgress_() const;

    ProjectSessionStore& store_;
    uint32_t delay_ms_ = 0;
    MicrosProvider micros_provider_ = nullptr;
    core::state::project::ProjectSnapshotPtr snapshot_;
    core::state::project::ProjectSnapshotCapture capture_;
    ProductPersistenceJobToken job_token_{};
    Status ordinary_cancel_status_ = Status::IDLE;
    bool recovery_in_progress_ = false;
};

}  // namespace core::persistence

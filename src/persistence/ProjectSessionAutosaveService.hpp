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
        FailureStage failureStage = FailureStage::NONE;
        oc::type::ErrorCode error = oc::type::ErrorCode::OK;
        const char* errorContext = nullptr;

        bool saved() const {
            return status == Status::SAVED;
        }
    };

    static const char* failureStageLabel(FailureStage stage);

    explicit ProjectSessionAutosaveService(ProjectSessionStore& store,
                                           uint32_t delayMs = 0);
    ~ProjectSessionAutosaveService();

    Result update(core::state::CoreState& state, uint32_t nowMs, bool mutationPending = false);
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
    bool writeSessionActive() const;

private:
    Result startCapture_(core::state::CoreState& state);
    Result updatePending_(core::state::CoreState& state,
                          uint32_t nowMs,
                          bool mutationPending,
                          bool inProgress);
    Result advanceCapture_(core::state::CoreState& state);
    Result advanceRecoveryCapture_(
        core::state::CoreState& state,
        const ProductMutationLease& recoveryLease
    );
    Result advanceSave_(core::state::CoreState& state);
    void cancelInFlight_();
    bool inProgress_() const;

    ProjectSessionStore& store_;
    uint32_t delay_ms_ = 0;
    core::state::project::ProjectSnapshotPtr snapshot_;
    core::state::project::ProjectSnapshotCapture capture_;
    bool recovery_in_progress_ = false;
};

}  // namespace core::persistence

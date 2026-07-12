#pragma once

#include <cstdint>

#include "state/project/ProjectSnapshot.hpp"

namespace core::state {
struct CoreState;
}

namespace core::persistence {

class ProjectSessionStore;

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

    struct Result {
        Status status = Status::IDLE;
        uint32_t bytes = 0;
        uint32_t modifiedCounter = 0;

        bool saved() const {
            return status == Status::SAVED;
        }
    };

    explicit ProjectSessionAutosaveService(ProjectSessionStore& store,
                                           uint32_t delayMs = 0);
    ~ProjectSessionAutosaveService();

    Result update(core::state::CoreState& state, uint32_t nowMs, bool mutationPending = false);
    Result flush(core::state::CoreState& state);
    bool writeSessionActive() const;

private:
    Result startCapture_(core::state::CoreState& state);
    Result advanceCapture_(core::state::CoreState& state);
    Result advanceSave_(core::state::CoreState& state);
    void cancelInFlight_();
    bool inProgress_() const;

    ProjectSessionStore& store_;
    uint32_t delay_ms_ = 0;
    core::state::project::ProjectSnapshotPtr snapshot_;
    core::state::project::ProjectSnapshotCapture capture_;
    uint32_t captured_modified_counter_ = 0;
};

}  // namespace core::persistence

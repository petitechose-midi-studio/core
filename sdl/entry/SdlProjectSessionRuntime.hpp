#pragma once

#include <cstdint>
#include <optional>

#include <oc/time/Time.hpp>

#include "persistence/ProductFileService.hpp"
#include "persistence/ProjectSessionAutosaveService.hpp"
#include "persistence/ProjectSessionRestoreService.hpp"
#include "persistence/ProjectSessionStore.hpp"
#include "state/CoreState.hpp"

namespace ms::entry {

/**
 * Desktop project-session lifecycle matching the firmware boot and loop order.
 *
 * Construction restores the current session before autosave starts. update()
 * is called after OpenControlApp::update(); it always advances CoreState
 * coalescing, then opens the one persistence turn. Autosave is admitted behind
 * an explicit owner instead of being suppressed, and every admitted slice is
 * paused while musical playback is active.
 */
class SdlProjectSessionRuntime {
public:
    using PersistenceAdvanceFn = void (*)(void*, uint32_t, bool);

    explicit SdlProjectSessionRuntime(
        core::persistence::ProductFileService& productFiles,
        core::state::CoreState& state,
        uint32_t autosaveDelayMs = 0,
        void* persistenceAdvanceContext = nullptr,
        PersistenceAdvanceFn persistenceAdvance = nullptr
    )
        : product_files_(productFiles)
        , state_(state)
        , store_(productFiles)
        , restore_(store_)
        , persistence_advance_context_(persistenceAdvanceContext)
        , persistence_advance_(persistenceAdvance) {
        restore_result_ = restore_.restore(state_);
        autosave_.emplace(store_, autosaveDelayMs);
    }

    SdlProjectSessionRuntime(const SdlProjectSessionRuntime&) = delete;
    SdlProjectSessionRuntime& operator=(const SdlProjectSessionRuntime&) = delete;
    SdlProjectSessionRuntime(SdlProjectSessionRuntime&&) = delete;
    SdlProjectSessionRuntime& operator=(SdlProjectSessionRuntime&&) = delete;

    const core::persistence::ProjectSessionRestoreService::Result& restoreResult() const {
        return restore_result_;
    }

    void update() {
        state_.update();
        const uint32_t nowMs = oc::time::millis();
        const auto turn = product_files_.persistenceJobs().beginTurn(nowMs);
        if (!turn) return;

        const bool playbackActive = state_.statusBar.playing.get();
        if (persistence_advance_) {
            persistence_advance_(
                persistence_advance_context_,
                nowMs,
                playbackActive
            );
        }

        if (autosave_) {
            autosave_->update(
                state_,
                nowMs,
                false,
                playbackActive
            );
        }
    }

private:
    core::persistence::ProductFileService& product_files_;
    core::state::CoreState& state_;
    core::persistence::ProjectSessionStore store_;
    core::persistence::ProjectSessionRestoreService restore_;
    core::persistence::ProjectSessionRestoreService::Result restore_result_{};
    std::optional<core::persistence::ProjectSessionAutosaveService> autosave_;
    void* persistence_advance_context_ = nullptr;
    PersistenceAdvanceFn persistence_advance_ = nullptr;
};

}  // namespace ms::entry

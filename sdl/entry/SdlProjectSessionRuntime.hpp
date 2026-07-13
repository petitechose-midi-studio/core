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
 * is called after OpenControlApp::update(); it advances CoreState coalescing,
 * then project autosave, unless an external product-file write owns the file
 * service. An autosave-owned write is allowed to keep advancing.
 */
class SdlProjectSessionRuntime {
public:
    explicit SdlProjectSessionRuntime(
        core::persistence::ProductFileService& productFiles,
        core::state::CoreState& state,
        uint32_t autosaveDelayMs = 0
    )
        : product_files_(productFiles)
        , state_(state)
        , store_(productFiles)
        , restore_(store_) {
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
        const bool productFileWriteActive = product_files_.writeSessionActive();
        const bool autosaveWriteActive = autosave_ && autosave_->writeSessionActive();
        const bool externalProductFileWriteActive =
            productFileWriteActive && !autosaveWriteActive;

        if (externalProductFileWriteActive) {
            return;
        }

        state_.update();
        if (autosave_) {
            autosave_->update(
                state_,
                oc::time::millis(),
                state_.hasPendingProjectMutationCoalescing()
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
};

}  // namespace ms::entry

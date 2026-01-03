#pragma once

/**
 * @file OverlayController.hpp
 * @brief Simplified overlay management with authority resolution
 *
 * Configures ExclusiveVisibilityStack's cleanup callback and provides
 * AuthorityResolver for input priority.
 */

#include <array>
#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/core/input/AuthorityResolver.hpp>
#include <oc/log/Log.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "OverlayTypes.hpp"

namespace state {

using oc::core::ScopeID;
using oc::core::input::AuthorityResolver;
using oc::hal::ButtonID;

/**
 * @brief Cleanup info for an overlay (scope and latch button)
 */
struct OverlayCleanupInfo {
    ScopeID scopeId = 0;          ///< Scope for input bindings
    ButtonID latchButton = 0;     ///< Button that latches this overlay
};

/**
 * @brief Simplified overlay controller
 *
 * Configures ExclusiveVisibilityStack's cleanup callback and provides
 * AuthorityResolver for input routing.
 *
 * Usage:
 * @code
 * // In StandaloneContext::initialize()
 * overlayController_ = std::make_unique<OverlayController>(state_.overlays, buttons());
 * overlayController_->registerCleanup(CoreOverlayType::MACRO_EDIT, scope, ButtonID::MACRO_1);
 * @endcode
 */
class OverlayController {
    static constexpr size_t COUNT = static_cast<size_t>(CoreOverlayType::COUNT);

public:
    OverlayController(oc::state::ExclusiveVisibilityStack<CoreOverlayType>& manager, oc::api::ButtonAPI& buttons)
        : manager_(manager)
        , buttons_(&buttons) {
        // Configure cleanup callback on the manager
        manager_.setCleanupCallback([this](CoreOverlayType type) {
            doCleanup(type);
        });

        // Configure authority resolver
        authority_.setOverlayProvider([this]() {
            return currentScope();
        });
    }

    ~OverlayController() = default;

    // Non-copyable, non-movable
    OverlayController(const OverlayController&) = delete;
    OverlayController& operator=(const OverlayController&) = delete;
    OverlayController(OverlayController&&) = delete;
    OverlayController& operator=(OverlayController&&) = delete;

    // =========================================================================
    // Registration
    // =========================================================================

    /**
     * @brief Register cleanup info for an overlay
     */
    void registerCleanup(CoreOverlayType type, ScopeID scopeId, ButtonID latchButton = 0) {
        auto idx = static_cast<size_t>(type);
        if (idx < COUNT) {
            cleanup_[idx] = {scopeId, latchButton};
            OC_LOG_DEBUG("[OverlayController] Registered cleanup for overlay {} (scope={}, latch={})",
                         idx, scopeId, static_cast<int>(latchButton));
        }
    }

    // =========================================================================
    // Delegation to ExclusiveVisibilityStack
    // =========================================================================

    void show(CoreOverlayType type, bool stack = false) { manager_.show(type, stack); }
    void hide() { manager_.hide(); }
    void hideAll() { manager_.hideAll(); }

    CoreOverlayType current() const { return manager_.current(); }
    bool hasVisible() const { return manager_.hasVisible(); }
    bool isCurrent(CoreOverlayType type) const { return manager_.current() == type; }

    // =========================================================================
    // Authority
    // =========================================================================

    ScopeID currentScope() const {
        auto type = manager_.current();
        if (type == CoreOverlayType::NONE) return 0;
        return cleanup_[static_cast<size_t>(type)].scopeId;
    }

    AuthorityResolver& authority() { return authority_; }
    const AuthorityResolver& authority() const { return authority_; }

    bool hasAuthority(ScopeID scope) const { return authority_.hasAuthority(scope); }

    ScopeID getScopeFor(CoreOverlayType type) const {
        auto idx = static_cast<size_t>(type);
        if (idx < COUNT) return cleanup_[idx].scopeId;
        return 0;
    }

private:
    void doCleanup(CoreOverlayType type) {
        if (type == CoreOverlayType::NONE || type == CoreOverlayType::COUNT) return;

        auto idx = static_cast<size_t>(type);
        const auto& info = cleanup_[idx];

        if (info.latchButton != 0 && buttons_) {
            buttons_->clearLatch(info.latchButton);
            OC_LOG_DEBUG("[OverlayController] Cleared latch for button {}",
                         static_cast<int>(info.latchButton));
        }
    }

    oc::state::ExclusiveVisibilityStack<CoreOverlayType>& manager_;
    oc::api::ButtonAPI* buttons_;
    AuthorityResolver authority_;
    std::array<OverlayCleanupInfo, COUNT> cleanup_{};
};

}  // namespace state

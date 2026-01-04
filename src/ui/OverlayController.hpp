#pragma once

/**
 * @file OverlayController.hpp
 * @brief Template overlay management with authority resolution
 *
 * Configures ExclusiveVisibilityStack's cleanup callback and provides
 * AuthorityResolver for input priority.
 *
 * Template class allows reuse between core (CoreOverlayType) and
 * plugin-bitwig (OverlayType) with their respective enum types.
 */

#include <array>
#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/core/input/AuthorityResolver.hpp>
#include <oc/log/Log.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

namespace ui {

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
 * @brief Template overlay controller
 *
 * Configures ExclusiveVisibilityStack's cleanup callback and provides
 * AuthorityResolver for input routing.
 *
 * @tparam EnumT Overlay enum type (must have NONE=0 and COUNT members)
 *
 * Usage:
 * @code
 * // In StandaloneContext::initialize()
 * overlayController_ = std::make_unique<OverlayController<CoreOverlayType>>(state_.overlays, buttons());
 * overlayController_->registerCleanup(CoreOverlayType::MACRO_EDIT, scope, ButtonID::MACRO_1);
 * @endcode
 */
template <typename EnumT>
class OverlayController {
    static_assert(static_cast<int>(EnumT::NONE) == 0, "EnumT::NONE must be 0");
    static constexpr size_t COUNT = static_cast<size_t>(EnumT::COUNT);

public:
    OverlayController(oc::state::ExclusiveVisibilityStack<EnumT>& manager, oc::api::ButtonAPI& buttons)
        : manager_(manager)
        , buttons_(&buttons) {
        // Configure cleanup callback on the manager
        manager_.setCleanupCallback([this](EnumT type) {
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
    void registerCleanup(EnumT type, ScopeID scopeId, ButtonID latchButton = 0) {
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

    void show(EnumT type, bool stack = false) { manager_.show(type, stack); }
    void hide() { manager_.hide(); }
    void hideAll() { manager_.hideAll(); }

    EnumT current() const { return manager_.current(); }
    bool hasVisible() const { return manager_.hasVisible(); }
    bool isCurrent(EnumT type) const { return manager_.current() == type; }

    // =========================================================================
    // Authority
    // =========================================================================

    ScopeID currentScope() const {
        auto type = manager_.current();
        if (type == EnumT::NONE) return 0;
        return cleanup_[static_cast<size_t>(type)].scopeId;
    }

    AuthorityResolver& authority() { return authority_; }
    const AuthorityResolver& authority() const { return authority_; }

    bool hasAuthority(ScopeID scope) const { return authority_.hasAuthority(scope); }

    ScopeID getScopeFor(EnumT type) const {
        auto idx = static_cast<size_t>(type);
        if (idx < COUNT) return cleanup_[idx].scopeId;
        return 0;
    }

private:
    void doCleanup(EnumT type) {
        if (type == EnumT::NONE || type == EnumT::COUNT) return;

        auto idx = static_cast<size_t>(type);
        const auto& info = cleanup_[idx];

        if (info.latchButton != 0 && buttons_) {
            buttons_->clearLatch(info.latchButton);
            OC_LOG_DEBUG("[OverlayController] Cleared latch for button {}",
                         static_cast<int>(info.latchButton));
        }
    }

    oc::state::ExclusiveVisibilityStack<EnumT>& manager_;
    oc::api::ButtonAPI* buttons_;
    AuthorityResolver authority_;
    std::array<OverlayCleanupInfo, COUNT> cleanup_{};
};

}  // namespace ui

#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <config/InputIDs.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"

namespace core::handler::modal {

/**
 * One-level, provisional value selection shared by Macro, Pitch and Device.
 *
 * NAV moves the preview or accepts it on release; LEFT_TOP cancels on release.
 * Only the current selector can act. Acceptance rechecks the choice range and
 * asks the domain to apply it; rejection keeps the selector and preview open.
 * Both successful acceptance and cancellation pop exactly the owned overlay,
 * then restore the parent's flow state. The domain callbacks must not navigate.
 *
 * This is an input value, not another stored state or transaction. The existing
 * selector owns the preview; the domain owns publication, persistence/history
 * and target validity. Parent buffers and authored data are untouched on cancel.
 * The owner supplies its active session phase separately: overlay visibility
 * can change independently while a domain session is being suspended/restored.
 */
struct ValueSelectorInput {
    enum class Kind : uint8_t { MOVE, ACCEPT, CANCEL };
    Kind kind;
    float delta = 0.0f;

    template <typename OverlayManager, typename OverlayEnum, typename SelectorState,
              typename ApplyFn, typename ClosedFn>
    void handle(OverlayManager& overlays, OverlayEnum ownedOverlay,
                SelectorState& selector, bool ownerActive, int choiceCount,
                ApplyFn apply, ClosedFn closed) const {
        if (!ownerActive || overlays.current() != ownedOverlay || !selector.visible.get()) return;

        if (kind == Kind::MOVE) {
            selector.selectedIndex.set(nav::nextWrappedIndex(
                delta, selector.selectedIndex.get(), choiceCount));
            return;
        }

        if (kind == Kind::ACCEPT) {
            const int choice = selector.selectedIndex.get();
            if (choice < 0 || choice >= choiceCount ||
                !apply(selector.editingRow.get(), choice)) return;
        } else if (kind != Kind::CANCEL) {
            return;
        }

        if (hideIfCurrent(overlays, ownedOverlay)) closed();
    }
};

/**
 * Register the family's three routes through the existing scope/press owner.
 * Pass a small capture (normally [this]); domain callbacks are built only when
 * handling an event, so sharing the lifecycle adds no retained callback state.
 */
template <typename HandleFn>
void bindValueSelectorInputs(oc::api::EncoderAPI& encoders, oc::api::ButtonAPI& buttons,
                             oc::type::ScopeID scope, HandleFn handle) {
    encoders.encoder(Config::EncoderID::NAV).turn().scope(scope).then(
        [handle](float delta) { handle({ValueSelectorInput::Kind::MOVE, delta}); });
    buttons.button(Config::ButtonID::NAV).release().scope(scope).then(
        [handle]() { handle({ValueSelectorInput::Kind::ACCEPT}); });
    buttons.button(Config::ButtonID::LEFT_TOP).release().scope(scope).then(
        [handle]() { handle({ValueSelectorInput::Kind::CANCEL}); });
}

}  // namespace core::handler::modal

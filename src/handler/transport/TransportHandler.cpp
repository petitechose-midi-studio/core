#include "TransportHandler.hpp"

#include <config/PlatformCompat.hpp>

namespace core::handler {

FLASHMEM TransportHandler::TransportHandler(StateRefs state,
                                   oc::api::ButtonAPI& buttons,
                                   TransportHandler::ViewScopes playToggleScopes)
    : status_bar_(state.statusBar)
    , buttons_(buttons)
    , play_toggle_scopes_(playToggleScopes) {
    setupBindings();
}

FLASHMEM void TransportHandler::setupBindings() {
    std::array<oc::type::ScopeID, VIEW_SCOPE_COUNT> boundScopes{};
    std::size_t boundScopeCount = 0;
    for (const auto scope : play_toggle_scopes_) {
        if (!scope) continue;
        bool alreadyBound = false;
        for (std::size_t i = 0; i < boundScopeCount; ++i) {
            if (boundScopes[i] == scope) {
                alreadyBound = true;
                break;
            }
        }
        if (alreadyBound) continue;
        buttons_.button(Config::ButtonID::BOTTOM_CENTER)
            .release()
            .scope(scope)
            .when([this]() { return !status_bar_.transportLocked.get(); })
            .then([this]() { handlePlayToggle(); });
        boundScopes[boundScopeCount++] = scope;
    }
}

void TransportHandler::handlePlayToggle() {
    bool playing = status_bar_.playing.get();
    status_bar_.playing.set(!playing);
}

}  // namespace core::handler

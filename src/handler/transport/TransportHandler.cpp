#include "TransportHandler.hpp"

#include <config/PlatformCompat.hpp>

namespace core::handler {

FLASHMEM TransportHandler::TransportHandler(StateRefs state,
                                             oc::api::ButtonAPI& buttons)
    : status_bar_(state.statusBar)
    , buttons_(buttons) {
    setupBindings();
}

FLASHMEM void TransportHandler::setupBindings() {
    buttons_.button(Config::ButtonID::BOTTOM_CENTER)
        .release()
        .globalPassThrough()
        .then([this]() { handlePlayToggle(); });
}

void TransportHandler::handlePlayToggle() {
    if (status_bar_.playing.get()) {
        // Stop is always available, including while an edit flow locks start.
        status_bar_.playing.set(false);
        return;
    }
    if (status_bar_.transportLocked.get()) return;
    status_bar_.playing.set(true);
}

}  // namespace core::handler

#pragma once

/**
 * @file TransportHandler.hpp
 * @brief Handles transport play/stop controls
 *
 * - BOTTOM_CENTER button: invariant global play/stop transport
 */

#include <oc/api/ButtonAPI.hpp>

#include <config/InputIDs.hpp>
#include "state/StatusBarState.hpp"

namespace core::handler {

class TransportHandler {
public:
    struct StateRefs {
        core::state::StatusBarState& statusBar;
    };

    TransportHandler(StateRefs state, oc::api::ButtonAPI& buttons);

    ~TransportHandler() = default;

    TransportHandler(const TransportHandler&) = delete;
    TransportHandler& operator=(const TransportHandler&) = delete;

private:
    void setupBindings();
    void handlePlayToggle();

    core::state::StatusBarState& status_bar_;
    oc::api::ButtonAPI& buttons_;
};

}  // namespace core::handler

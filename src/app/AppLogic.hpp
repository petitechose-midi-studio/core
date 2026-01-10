#pragma once

#include <oc/app/OpenControlApp.hpp>
#include "App.hpp"
#include "context/StandaloneContext.hpp"
#include "state/CoreState.hpp"

namespace core::app {

/**
 * @brief Register application contexts
 *
 * Shared between Teensy and SDL builds.
 * Uses factory registration to inject CoreState reference.
 */
inline void registerContexts(oc::app::OpenControlApp& app,
                             core::state::CoreState& coreState) {
    app.registerContextWithFactory(
        Config::ContextID::STANDALONE,
        "Standalone",
        [&coreState]() {
            return std::make_unique<core::context::StandaloneContext>(coreState);
        });
}

} // namespace core::app

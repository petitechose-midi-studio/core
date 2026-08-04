#pragma once

#include <oc/app/OpenControlApp.hpp>
#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include "context/StandaloneContext.hpp"
#include "persistence/ProductDirectoryCatalog.hpp"
#include "persistence/ProductFileService.hpp"

namespace core::app {

/**
 * @brief Register application contexts
 *
 * Shared between Teensy and SDL builds.
 * Uses factory registration to inject CoreState reference.
 */
inline FLASHMEM void registerContexts(oc::app::OpenControlApp& app,
                                      core::state::CoreState& coreState,
                                      core::persistence::ProductFileService& productFiles,
                                      core::persistence::ProductDirectoryCatalog& productCatalog) {
    app.registerContextWithFactory(
        Config::ContextID::STANDALONE,
        "Standalone",
        [&coreState, &productFiles, &productCatalog]() {
            return std::make_unique<core::context::StandaloneContext>(
                coreState,
                productFiles,
                productCatalog
            );
        });
}

} // namespace core::app

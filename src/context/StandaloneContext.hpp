#pragma once

/**
 * @file StandaloneContext.hpp
 * @brief Main application context for standalone operation
 *
 * Phase 2: Minimal version - no inputs, no UI.
 * Phase 3: Add input handling
 * Phase 4: Add full UI
 */

#include "config/App.hpp"

#include <oc/context/IContext.hpp>
#include <oc/context/Requirements.hpp>

namespace context {

/**
 * @brief Standalone mode context
 *
 * Main application mode when not connected to a DAW.
 * Currently minimal - will be extended with UI and input handling.
 */
class StandaloneContext : public oc::context::IContext {
public:
    /// Phase 2: No requirements (will add button/encoder in Phase 3)
    static constexpr oc::context::Requirements REQUIRES{};

    bool initialize() override {
        Serial.println("[Standalone] Active");
        return true;
    }

    void update() override {
        // Phase 2: Nothing to do yet
        // Phase 3: Input handling will be done via bindings
        // Phase 4: UI updates will be handled by LVGL refresh
    }

    void cleanup() override {
        Serial.println("[Standalone] Cleanup");
    }

    const char* getName() const override { return "Standalone"; }
};

}  // namespace context

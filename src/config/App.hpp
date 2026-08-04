#pragma once

/**
 * @file App.hpp
 * @brief Application configuration constants
 *
 * Platform-agnostic configuration. Does not depend on hardware-specific headers.
 */

#include "Version.hpp"
#include "InputIDs.hpp"
#include "config/Timing.hpp"

#include <cstdint>

#include <oc/core/input/InputConfig.hpp>

namespace Config {

// ═══════════════════════════════════════════════════════════════════════════
// Application Info
// ═══════════════════════════════════════════════════════════════════════════

namespace App {
constexpr const char* NAME = "Midi Studio";
using Core::VERSION;
}  // namespace App

// ═══════════════════════════════════════════════════════════════════════════
// Context IDs
// ═══════════════════════════════════════════════════════════════════════════

enum class ContextID : uint8_t {
    STANDALONE = 0,
};

// ═══════════════════════════════════════════════════════════════════════════
// Input Configuration
// ═══════════════════════════════════════════════════════════════════════════

namespace Input {
// Strict physical-button contract: docs/INPUT_BINDINGS.md.
// Encoder turns remain instantaneous and keep their scoped routing.
constexpr oc::core::input::InputConfig CONFIG = {
    .longPressMs = Timing::LONG_PRESS_MS,
    .doubleTapWindowMs = Timing::DOUBLE_TAP_MS,
    .latchThresholdMs = Timing::LATCH_THRESHOLD_MS,
    .debounceMs = Timing::DEBOUNCE_MS,
    .releaseRoutingPolicy = oc::core::input::ReleaseRoutingPolicy::OwnerOnly,
    .gestureRoutingPolicy = oc::core::input::GestureRoutingPolicy::PressScoped,
    .ambiguityPolicy = oc::core::input::BindingAmbiguityPolicy::FailClosed,
    .globalRoutingPolicy =
        oc::core::input::GlobalRoutingPolicy::ExplicitPassThroughOnly,
};
}  // namespace Input

}  // namespace Config

#pragma once

/**
 * @file App.hpp
 * @brief Application configuration constants
 *
 * Platform-agnostic configuration. Does not depend on hardware-specific headers.
 */

#include "Version.hpp"

#include <cstdint>

#include <ms/device_support/v1/InputConfig.hpp>

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
using ms::device_support::v1::input::CONFIG;
}  // namespace Input

}  // namespace Config

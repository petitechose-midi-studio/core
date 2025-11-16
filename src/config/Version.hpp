#pragma once

#include <cstdint>

/*
 * Version Configuration
 *
 * Edit these values to update versions.
 *
 * For PRERELEASE builds (beta, rc, etc.):
 *   - Keep CORE_IS_PRERELEASE defined
 *   - Set CORE_VERSION_PRERELEASE to "beta.1", "rc.1", etc.
 *   - Result: VERSION = "1.0.0-beta.1"
 *
 * For RELEASE builds:
 *   - Comment out or remove #define CORE_IS_PRERELEASE
 *   - Result: VERSION = "1.0.0"
 */

// Core firmware version components
#define CORE_VERSION_MAJOR 1
#define CORE_VERSION_MINOR 0
#define CORE_VERSION_PATCH 0

// Prerelease flag: comment this line for release builds
#define CORE_IS_PRERELEASE
#define CORE_VERSION_PRERELEASE "beta.1"

// API version components (evolves independently from core)
#define API_VERSION_MAJOR 1
#define API_VERSION_MINOR 0
#define API_VERSION_PATCH 0

/*
 * Internal build-time version string generation
 * Do not modify below this line
 */

// Helper macros for string concatenation
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Build VERSION string at compile-time based on prerelease status
#ifdef CORE_IS_PRERELEASE
    #define CORE_VERSION_STRING TOSTRING(CORE_VERSION_MAJOR) "." TOSTRING(CORE_VERSION_MINOR) "." TOSTRING(CORE_VERSION_PATCH) "-" CORE_VERSION_PRERELEASE
#else
    #define CORE_VERSION_STRING TOSTRING(CORE_VERSION_MAJOR) "." TOSTRING(CORE_VERSION_MINOR) "." TOSTRING(CORE_VERSION_PATCH)
#endif

/**
 * @brief Core firmware version information
 *
 * Follows Semantic Versioning (SemVer): MAJOR.MINOR.PATCH[-PRERELEASE]
 * - MAJOR: Breaking changes
 * - MINOR: New features (backward-compatible)
 * - PATCH: Bug fixes
 * - PRERELEASE: beta.1, rc.1, etc. (optional)
 */
namespace Core {
    constexpr uint8_t VERSION_MAJOR = CORE_VERSION_MAJOR;
    constexpr uint8_t VERSION_MINOR = CORE_VERSION_MINOR;
    constexpr uint8_t VERSION_PATCH = CORE_VERSION_PATCH;
    constexpr const char* VERSION = CORE_VERSION_STRING;

    #ifdef CORE_IS_PRERELEASE
    constexpr bool IS_PRERELEASE = true;
    constexpr const char* VERSION_PRERELEASE = CORE_VERSION_PRERELEASE;
    #else
    constexpr bool IS_PRERELEASE = false;
    #endif
}

/**
 * @brief API version information
 *
 * This version tracks the ControllerAPI interface compatibility.
 * Plugins check this version to determine compatibility.
 *
 * Core can evolve (optimizations, bug fixes) without changing API version.
 * Breaking changes to ControllerAPI require MAJOR version bump.
 */
namespace API {
    constexpr uint8_t VERSION_MAJOR = API_VERSION_MAJOR;
    constexpr uint8_t VERSION_MINOR = API_VERSION_MINOR;
    constexpr uint8_t VERSION_PATCH = API_VERSION_PATCH;
}

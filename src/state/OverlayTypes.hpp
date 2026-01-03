#pragma once

/**
 * @file OverlayTypes.hpp
 * @brief Overlay type definitions for standalone mode
 *
 * Convention: <DOMAIN>_SELECTOR ou <DOMAIN>_EDIT
 */

#include <cstdint>

namespace state {

/**
 * @brief Overlay types managed by OverlayManager
 */
enum class CoreOverlayType : uint8_t {
    NONE = 0,
    PAGE_SELECTOR,    // Sélection de page macro
    MACRO_EDIT,       // Édition d'une macro (CH/CC)
    COUNT             // Sentinel - must be last
};

}  // namespace state

#pragma once

/**
 * @file OverlayTypes.hpp
 * @brief Overlay type definitions for standalone mode
 *
 * Convention: <DOMAIN>_SELECTOR ou <DOMAIN>_EDIT
 */

#include <cstdint>

namespace core::ui {

/**
 * @brief Overlay types managed by ExclusiveVisibilityStack
 */
enum class OverlayType : uint8_t {
    NONE = 0,
    PAGE_SELECTOR,    // Sélection de page macro
    MACRO_EDIT,       // Édition d'une macro (CH/CC)
    MACRO_EDIT_SELECTOR,       // Sélecteur de valeur (CH/CC)
    MACRO_EDIT_MACRO_SELECTOR, // Sélecteur de macro cible (hold LEFT_BOTTOM)
    VIEW_SELECTOR,    // Top-level view selector

    // Sequencer overlays
    SEQ_PATTERN_CONFIG,     // Pattern config (LEN / DIV / CH)
    SEQ_STEP_EDIT,          // Step edit (NOTE / VEL / GATE)
    SEQ_PROPERTY_SELECTOR,  // Which property is edited by encoders

    // Global settings overlays
    GLOBAL_SETTINGS,
    GLOBAL_SETTINGS_SELECTOR,

    // Data manager overlays
    DATA_MANAGER,
    DATA_MANAGER_DIALOG,

    COUNT             // Sentinel - must be last
};

}  // namespace core::ui

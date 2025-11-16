#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "core/Type.hpp"

typedef struct _lv_obj_t lv_obj_t;

namespace Common::UI {
class IView;
}

/**
 * @brief Button input binding definition
 *
 * Represents a binding between a button action and a callback.
 * Supports various trigger types: press, release, long press, double tap, combo.
 *
 * Bindings can be scoped to LVGL objects:
 * - scope = nullptr: Global binding (always active)
 * - scope = lv_obj_t*: Scoped binding (active only if object visible)
 */
struct ButtonBinding {
    ButtonBindingType type;
    ButtonID buttonId;
    std::optional<ButtonID> secondaryButton;  // For COMBO
    uint32_t longPressMs = 0;                 // For LONG_PRESS
    std::function<void()> action;
    bool enabled = true;
    lv_obj_t* scope = nullptr;                // nullptr = global, otherwise scoped to LVGL object
};

/**
 * @brief Encoder input binding definition
 *
 * Represents a binding between an encoder action and a callback.
 * Supports turn events, with optional button press requirement.
 *
 * Bindings can be scoped to LVGL objects:
 * - scope = nullptr: Global binding (always active)
 * - scope = lv_obj_t*: Scoped binding (active only if object visible)
 */
struct EncoderBinding {
    EncoderBindingType type;
    EncoderID encoderId;
    std::optional<ButtonID> requiredButton;  // For TURN_WHILE_PRESSED
    std::function<void(float)> action;       // Receives normalized value (0.0-1.0)
    bool enabled = true;
    lv_obj_t* scope = nullptr;                // nullptr = global, otherwise scoped to LVGL object
};

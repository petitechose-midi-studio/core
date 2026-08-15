#pragma once

#include <cstdint>

#include <lvgl.h>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::interaction {

/**
 * Shared visual contract for compact interactive cards.
 *
 * This is deliberately presentation-only: domain colors remain on icons and
 * data, while focus and availability are communicated by neutral surfaces.
 */
enum class InteractiveSurfaceState : uint8_t {
    IDLE = 0,
    ACTIVE,
    FOCUSED,
    DISABLED,
};

struct InteractiveSurfaceVisual {
    uint32_t backgroundColor = 0;
    uint32_t borderColor = 0;
    uint32_t textColor = 0;
    lv_opa_t backgroundOpacity = LV_OPA_TRANSP;
    lv_opa_t borderOpacity = LV_OPA_TRANSP;
    lv_opa_t textOpacity = LV_OPA_TRANSP;
};

constexpr InteractiveSurfaceVisual interactiveSurfaceVisual(
    InteractiveSurfaceState state
) {
    namespace color = standalone::theme::color;
    switch (state) {
        case InteractiveSurfaceState::FOCUSED:
            return {
                color::SURFACE_RAISED,
                color::FOCUS_EDIT,
                color::TEXT_PRIMARY,
                LV_OPA_COVER,
                LV_OPA_COVER,
                LV_OPA_COVER,
            };
        case InteractiveSurfaceState::ACTIVE:
            return {
                color::SURFACE_IDLE,
                color::BORDER_SUBTLE,
                color::TEXT_PRIMARY,
                LV_OPA_COVER,
                LV_OPA_80,
                LV_OPA_COVER,
            };
        case InteractiveSurfaceState::DISABLED:
            return {
                color::SURFACE_IDLE,
                color::BORDER_SUBTLE,
                color::TEXT_DISABLED,
                LV_OPA_40,
                LV_OPA_30,
                LV_OPA_60,
            };
        case InteractiveSurfaceState::IDLE:
        default:
            return {
                color::SURFACE_IDLE,
                color::BORDER_SUBTLE,
                color::TEXT_SECONDARY,
                LV_OPA_COVER,
                LV_OPA_60,
                LV_OPA_80,
            };
    }
}

inline void applyInteractiveSurfaceChrome(
    lv_obj_t* object,
    const InteractiveSurfaceVisual& visual
) {
    if (!object) return;
    lv_obj_set_style_bg_color(
        object, lv_color_hex(visual.backgroundColor), 0
    );
    lv_obj_set_style_bg_opa(object, visual.backgroundOpacity, 0);
    lv_obj_set_style_border_color(
        object, lv_color_hex(visual.borderColor), 0
    );
    lv_obj_set_style_border_opa(object, visual.borderOpacity, 0);
}

inline void applyInteractiveSurfaceChrome(
    lv_obj_t* object,
    InteractiveSurfaceState state
) {
    applyInteractiveSurfaceChrome(object, interactiveSurfaceVisual(state));
}

static_assert(sizeof(InteractiveSurfaceState) == 1U);

}  // namespace core::ui::interaction

#include "ButtonIndicator.hpp"

#include "../theme/BaseTheme.hpp"

ButtonIndicator::ButtonIndicator(lv_obj_t* parent, lv_coord_t size)
    : led_(nullptr),
      current_state_(State::OFF),
      custom_colors_{lv_color_hex(0), lv_color_hex(0), lv_color_hex(0)},
      custom_opacities_{0, 0, 0} {
    lv_obj_t* actual_parent = parent ? parent : lv_screen_active();

    led_ = lv_obj_create(actual_parent);
    if (!led_) {
        return;
    }

    lv_obj_set_size(led_, size, size);
    lv_obj_set_style_radius(led_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(led_, 0, 0);
    lv_obj_clear_flag(led_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(led_, lv_color_hex(BaseTheme::Color::STATUS_INACTIVE), 0);
    lv_obj_set_style_bg_opa(led_, LV_OPA_60, 0);
}

ButtonIndicator::~ButtonIndicator() {
    if (led_) {
        lv_obj_delete(led_);
        led_ = nullptr;
    }
}

void ButtonIndicator::setState(State state) {
    if (current_state_ != state) {
        current_state_ = state;
        updateVisualState();
    }
}

void ButtonIndicator::setCustomColor(State state, lv_color_t color) {
    custom_colors_[static_cast<int>(state)] = color;
}

void ButtonIndicator::setCustomOpacity(State state, lv_opa_t opacity) {
    custom_opacities_[static_cast<int>(state)] = opacity;
}

lv_color_t ButtonIndicator::getColorForState(State state) const {
    int idx = static_cast<int>(state);

    if (custom_opacities_[idx] != 0) {
        return custom_colors_[idx];
    }

    switch (state) {
    case State::OFF:
        return lv_color_hex(BaseTheme::Color::STATUS_INACTIVE);
    case State::ACTIVE:
        return lv_color_hex(BaseTheme::Color::STATUS_WARNING);
    case State::PRESSED:
        return lv_color_hex(BaseTheme::Color::STATUS_SUCCESS);
    default:
        return lv_color_hex(BaseTheme::Color::STATUS_INACTIVE);
    }
}

lv_opa_t ButtonIndicator::getOpacityForState(State state) const {
    int idx = static_cast<int>(state);

    if (custom_opacities_[idx] != 0) {
        return custom_opacities_[idx];
    }

    switch (state) {
    case State::OFF:
        return LV_OPA_60;
    case State::ACTIVE:
        return LV_OPA_80;
    case State::PRESSED:
        return LV_OPA_COVER;
    default:
        return LV_OPA_60;
    }
}

void ButtonIndicator::updateVisualState() {
    if (!led_) return;

    lv_color_t color = getColorForState(current_state_);
    lv_opa_t opacity = getOpacityForState(current_state_);

    lv_obj_set_style_bg_color(led_, color, 0);
    lv_obj_set_style_bg_opa(led_, opacity, 0);
}

#pragma once

#include <lvgl.h>

/**
 * @brief Simple circular indicator for button states
 *
 * A basic colored circle with configurable state-based colors and opacity.
 * Usage:
 *   auto indicator = std::make_unique<ButtonIndicator>(parent, 12);
 *   indicator->setCustomColor(ButtonIndicator::OFF, COLOR_RED);
 *   indicator->setCustomOpacity(ButtonIndicator::OFF, LV_OPA_30);
 *   indicator->setState(ButtonIndicator::OFF);
 */
class ButtonIndicator {
public:
    enum class State { OFF = 0, ACTIVE = 1, PRESSED = 2 };

    static constexpr State OFF = State::OFF;
    static constexpr State ACTIVE = State::ACTIVE;
    static constexpr State PRESSED = State::PRESSED;

    ButtonIndicator(lv_obj_t* parent, lv_coord_t size = 12);
    ~ButtonIndicator();

    void setState(State state);

    void setCustomColor(State state, lv_color_t color);
    void setCustomOpacity(State state, lv_opa_t opacity);

    lv_obj_t* getLed() const {
        return led_;
    }

private:
    lv_obj_t* led_;
    State current_state_;

    lv_color_t custom_colors_[3];
    lv_opa_t custom_opacities_[3];

    void updateVisualState();
    lv_color_t getColorForState(State state) const;
    lv_opa_t getOpacityForState(State state) const;
};

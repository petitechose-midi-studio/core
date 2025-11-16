#include "ParameterButtonWidget.hpp"

#include "resource/common/ui/font/binary_font_buffer.hpp"
#include "resource/common/ui/theme/BaseTheme.hpp"
#include "resource/common/ui/util/TextUtils.hpp"
#include "log/Macros.hpp"

ParameterButtonWidget::ParameterButtonWidget(lv_obj_t* parent, uint16_t width, uint16_t height,
                                             uint8_t color_index)
    : parent_(parent ? parent : lv_screen_active()),
      width_(width),
      height_(height),
      color_index_(color_index),
      name_("BUTTON") {
    createUI();
    setName(name_);
    setValue(0.0f);  // Start OFF
}

ParameterButtonWidget::~ParameterButtonWidget() {
    if (container_) {
        lv_obj_delete(container_);
    }
}

void ParameterButtonWidget::setName(const String& name) {
    name_ = name;
    if (name_label_) {
        String formatted = TextUtils::formatTextForTwoLines(name, width_ - 20, fonts.parameter_label);
        lv_label_set_text(name_label_, formatted.c_str());
    }
}

void ParameterButtonWidget::setValue(float value) {
    bool new_state = value >= 0.5f;
    if (is_on_ != new_state) {
        is_on_ = new_state;
        updateButtonState(is_on_);
    }
}

void ParameterButtonWidget::setValueWithDisplay(float value, const char* displayValue) {
    setValue(value);

    if (state_label_ && displayValue) {
        lv_label_set_text(state_label_, displayValue);
    }
}

void ParameterButtonWidget::setDiscreteMetadata(int16_t discreteCount,
                                                 const etl::vector<etl::string<16>, 32>& valueNames,
                                                 uint8_t currentIndex) {
    discrete_value_names_ = valueNames;
    current_value_index_ = currentIndex;
    has_discrete_metadata_ = true;
}

void ParameterButtonWidget::setVisible(bool visible) {
    if (container_) {
        if (visible) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ParameterButtonWidget::createUI() {
    container_ = lv_obj_create(parent_);
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);

    createButtonBox();
    createStateLabel();
    createNameLabel();
}

void ParameterButtonWidget::createButtonBox() {
    button_box_ = lv_obj_create(container_);
    lv_obj_set_size(button_box_, BUTTON_SIZE, BUTTON_SIZE);  // 40x40 button

    // Center button in 62x62 area, at same Y offset as other widgets
    lv_obj_align(button_box_,
                 LV_ALIGN_TOP_MID,
                 0,
                 BUTTON_Y_OFFSET + (CONTAINER_SIZE - BUTTON_SIZE) / 2);

    lv_obj_set_style_radius(button_box_, 8, 0);
    lv_obj_set_style_border_width(button_box_, 0, 0);  // No border

    // Initial state: OFF (knob inactive color)
    lv_obj_set_style_bg_color(button_box_, lv_color_hex(BaseTheme::Color::INACTIVE), 0);
    lv_obj_set_style_bg_opa(button_box_, LV_OPA_COVER, 0);
}

void ParameterButtonWidget::createStateLabel() {
    state_label_ = lv_label_create(button_box_);

    lv_obj_set_style_text_font(state_label_, fonts.parameter_label, 0);
    lv_obj_set_style_text_color(state_label_, lv_color_hex(0xd9d9d9), 0);
    lv_obj_set_style_text_align(state_label_, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_center(state_label_);
    lv_label_set_text(state_label_, "OFF");
}

void ParameterButtonWidget::createNameLabel() {
    name_label_ = lv_label_create(container_);

    lv_obj_set_style_text_font(name_label_, fonts.parameter_label, 0);
    lv_obj_set_style_text_color(name_label_, lv_color_hex(0xd9d9d9), 0);
    lv_obj_set_style_text_align(name_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(name_label_, -2, 0);

    lv_obj_set_width(name_label_, width_ - 20);
    lv_obj_set_height(name_label_, 36);

    lv_label_set_long_mode(name_label_, LV_LABEL_LONG_WRAP);

    // Position name label at same Y as other widgets (after 62x62 container area)
    lv_coord_t container_bottom = BUTTON_Y_OFFSET + CONTAINER_SIZE;
    lv_obj_align(name_label_, LV_ALIGN_TOP_MID, 0, container_bottom - 4);
}

void ParameterButtonWidget::updateButtonState(bool isOn) {
    if (!button_box_ || !state_label_) return;

    if (isOn) {
        // Active: theme ACTIVE color (orange/gold) + dark text
        lv_obj_set_style_bg_color(button_box_, lv_color_hex(BaseTheme::Color::ACTIVE), 0);
        lv_obj_set_style_bg_opa(button_box_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(state_label_, lv_color_hex(BaseTheme::Color::INACTIVE), 0);
    } else {
        // Inactive: knob inactive color (dark gray) + light text
        lv_obj_set_style_bg_color(button_box_, lv_color_hex(BaseTheme::Color::INACTIVE), 0);
        lv_obj_set_style_bg_opa(button_box_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(state_label_,
                                    lv_color_hex(BaseTheme::Color::TEXT_PRIMARY),
                                    0);  // Light gray text
    }
}

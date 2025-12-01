#include "ParameterListWidget.hpp"

#include "font/FontLoader.hpp"
#include "theme/BaseTheme.hpp"
#include "util/TextUtils.hpp"
#include "log/Macros.hpp"

ParameterListWidget::ParameterListWidget(lv_obj_t* parent, uint16_t width, uint16_t height,
                                         uint8_t color_index, int16_t discreteCount)
    : parent_(parent ? parent : lv_screen_active()),
      width_(width),
      height_(height),
      color_index_(color_index),
      discrete_count_(discreteCount),
      name_("LIST"),
      display_value_("---") {
    createUI();
}

ParameterListWidget::~ParameterListWidget() {
    if (flash_timer_) {
        lv_timer_delete(flash_timer_);
        flash_timer_ = nullptr;
    }
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}

void ParameterListWidget::setName(const String& name) {
    if (name_ == name) return;

    name_ = name;
    if (name_label_) {
        name_label_->setText(name.c_str());
    }
}

void ParameterListWidget::setValue(float value) {
    value_ = (value < 0.0f) ? 0.0f : (value > 1.0f) ? 1.0f : value;

    // Convert normalized value to index
    int16_t index = static_cast<int16_t>(value_ * (discrete_count_ - 1) + 0.5f);

    // If no displayValue provided, show index
    if (display_value_.length() == 0) {
        display_value_ = String(index);
        if (value_label_) {
            lv_label_set_text(value_label_, display_value_.c_str());
        }
    }

    triggerValueChangeFlash();
}

void ParameterListWidget::setValueWithDisplay(float value, const char* displayValue) {
    const char* newDisplay = displayValue ? displayValue : "---";

    // Skip update if nothing changed
    if (value_ == value && display_value_ == newDisplay) return;

    value_ = value;
    display_value_ = newDisplay;

    if (value_label_) {
        String formatted = TextUtils::formatTextForTwoLines(display_value_,
                                                            VALUE_BOX_SIZE - 8,
                                                            fonts.parameter_label);
        lv_label_set_text(value_label_, formatted.c_str());
        // Note: top_line_ uses fixed width set at creation, no repositioning needed
    }

    triggerValueChangeFlash();
}

void ParameterListWidget::setDiscreteMetadata(int16_t discreteCount,
                                               const etl::vector<etl::string<16>, 32>& valueNames,
                                               uint8_t currentIndex) {
    discrete_count_ = discreteCount;
    discrete_value_names_ = valueNames;
    current_value_index_ = currentIndex;
    has_discrete_metadata_ = true;
}

void ParameterListWidget::setVisible(bool visible) {
    if (container_) {
        if (visible) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ParameterListWidget::createUI() {
    container_ = lv_obj_create(parent_);
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(container_, LV_OBJ_FLAG_HIDDEN, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);

    createValueBox();
    createValueLabel();
    createTopLine();  // Create line after label to position it correctly
    createNameLabel();
}

void ParameterListWidget::createValueBox() {
    // Value box (no border, transparent)
    value_box_ = lv_obj_create(container_);
    lv_obj_set_size(value_box_, VALUE_BOX_SIZE, VALUE_BOX_SIZE);
    lv_obj_align(value_box_, LV_ALIGN_TOP_MID, 0, VALUE_BOX_Y_OFFSET);

    lv_obj_set_style_radius(value_box_, 8, 0);
    lv_obj_set_style_border_width(value_box_, 0, 0);  // No border
    lv_obj_set_style_bg_color(value_box_, lv_color_hex(BaseTheme::Color::KNOB_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(value_box_, LV_OPA_TRANSP, 0);
}

void ParameterListWidget::createValueLabel() {
    value_label_ = lv_label_create(value_box_);

    lv_obj_set_style_text_font(value_label_, fonts.parameter_value_label, 0);
    lv_obj_set_style_text_color(value_label_, lv_color_hex(BaseTheme::Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(value_label_, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_set_width(value_label_, VALUE_BOX_SIZE - 8);
    lv_label_set_long_mode(value_label_, LV_LABEL_LONG_WRAP);

    lv_obj_center(value_label_);
    lv_label_set_text(value_label_, display_value_.c_str());
}

void ParameterListWidget::createTopLine() {
    if (!value_box_) return;

    // Fixed width line centered above text area
    static constexpr lv_coord_t LINE_WIDTH = VALUE_BOX_SIZE - 8;  // Same as label width
    static constexpr lv_coord_t LINE_HEIGHT = 2;
    static constexpr lv_coord_t LINE_Y = 8;  // Fixed position near top of value_box

    top_line_ = lv_obj_create(value_box_);
    lv_obj_set_size(top_line_, LINE_WIDTH, LINE_HEIGHT);
    lv_obj_align(top_line_, LV_ALIGN_TOP_MID, 0, LINE_Y);

    lv_obj_set_style_bg_color(top_line_, lv_color_hex(BaseTheme::Color::INACTIVE), 0);
    lv_obj_set_style_bg_opa(top_line_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(top_line_, 0, 0);
    lv_obj_set_style_radius(top_line_, 0, 0);
}

void ParameterListWidget::createNameLabel() {
    name_label_ = std::make_unique<Label>(container_);
    name_label_->setFlexGrow(false);
    name_label_->setAlignment(LV_TEXT_ALIGN_CENTER);
    name_label_->setFont(fonts.parameter_label);
    name_label_->setColor(lv_color_hex(BaseTheme::Color::TEXT_PRIMARY));

    // Position the label container
    lv_obj_t* label_container = name_label_->getContainer();
    lv_obj_set_width(label_container, width_ - 20);
    lv_coord_t box_bottom = VALUE_BOX_Y_OFFSET + VALUE_BOX_SIZE;
    lv_obj_align(label_container, LV_ALIGN_TOP_MID, 0, box_bottom - 4);
}

void ParameterListWidget::triggerValueChangeFlash() {
    if (!top_line_) return;

    // Cancel any existing flash timer
    if (flash_timer_) {
        lv_timer_delete(flash_timer_);
        flash_timer_ = nullptr;
    }

    // Apply active color to top line
    lv_obj_set_style_bg_color(top_line_, lv_color_hex(BaseTheme::Color::ACTIVE), 0);

    // Create one-shot timer to restore color after 100ms
    flash_timer_ = lv_timer_create(flashTimerCallback, FLASH_DURATION_MS, this);
    lv_timer_set_repeat_count(flash_timer_, 1);
}

void ParameterListWidget::flashTimerCallback(lv_timer_t* timer) {
    auto* widget = static_cast<ParameterListWidget*>(lv_timer_get_user_data(timer));
    if (!widget || !widget->top_line_) return;

    // Restore top line color to INACTIVE
    lv_obj_set_style_bg_color(widget->top_line_, lv_color_hex(BaseTheme::Color::INACTIVE), 0);

    // Clear timer reference
    widget->flash_timer_ = nullptr;
}

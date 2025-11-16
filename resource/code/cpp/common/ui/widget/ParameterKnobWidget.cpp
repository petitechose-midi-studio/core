#include "ParameterKnobWidget.hpp"

#include <arm_math.h>

#include <cmath>

#include "common/ui/font/binary_font_buffer.hpp"
#include "common/ui/theme/BaseTheme.hpp"
#include "common/ui/util/TextUtils.hpp"

ParameterKnobWidget::ParameterKnobWidget(lv_obj_t* parent, uint16_t width, uint16_t height,
                                         uint8_t color_index, bool centered)
    : parent_(parent ? parent : lv_screen_active()),
      value_(centered ? 0.5f : 0.0f),
      origin_(centered ? 0.5f : 0.0f),
      width_(width),
      height_(height),
      name_("PARAM") {
    createUI();
    setName(name_);
}

ParameterKnobWidget::~ParameterKnobWidget() {
    if (flash_timer_) {
        lv_timer_delete(flash_timer_);
        flash_timer_ = nullptr;
    }
    if (container_) {
        lv_obj_delete(container_);
    }
}

// ===== PUBLIC INTERFACE =====

void ParameterKnobWidget::setName(const String& name) {
    if (name_ == name) return;
    if (!name_label_) return;

    name_ = name;
    String formatted = TextUtils::formatTextForTwoLines(name,
                                                        width_ - LABEL_HORIZONTAL_PADDING,
                                                        fonts.parameter_label);
    lv_label_set_text(name_label_, formatted.c_str());
}

void ParameterKnobWidget::setValue(float value) {
    float clamped = constrain(value, 0.0f, 1.0f);

    // Only update if difference is significant (avoid micro-updates)
    if (fabsf(value_ - clamped) <= VALUE_CHANGE_THRESHOLD) return;

    value_ = clamped;
    updateValue();
    triggerValueChangeFlash();
}

void ParameterKnobWidget::setOrigin(float origin) {
    float clamped = constrain(origin, 0.0f, 1.0f);
    if (origin_ == clamped) return;

    origin_ = clamped;
    updateValue();
}

void ParameterKnobWidget::setValueWithDisplay(float value, const char* displayValue) {
    setValue(value);
}

void ParameterKnobWidget::setVisible(bool visible) {
    if (!container_) return;

    if (visible) {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }
}

// ===== UI CREATION =====

void ParameterKnobWidget::createUI() {
    container_ = lv_obj_create(parent_);
    lv_obj_set_size(container_, width_, height_);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);

    arc_center_x_ = width_ / 2;
    arc_center_y_ = ARC_Y_OFFSET + ARC_RADIUS;

    createArc();
    createValueIndicator();
    createNameLabel();
    createCenterCircles();
}

void ParameterKnobWidget::createArc() {
    arc_ = lv_arc_create(container_);
    lv_obj_set_size(arc_, ARC_SIZE, ARC_SIZE);
    lv_obj_align(arc_, LV_ALIGN_TOP_MID, 0, ARC_Y_OFFSET);

    // Background arc (full range display)
    lv_arc_set_bg_angles(arc_, START_ANGLE, END_ANGLE);

    // Main arc style (inactive background)
    lv_obj_set_style_arc_width(arc_, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_, lv_color_hex(BaseTheme::Color::INACTIVE), LV_PART_MAIN);

    // Indicator arc style (value track)
    lv_obj_set_style_arc_width(arc_, ARC_WIDTH / 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_, lv_color_hex(BaseTheme::Color::KNOB_TRACK), LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(arc_, ARC_WIDTH / 4, LV_PART_INDICATOR);

    // Remove knob (we use custom indicator line)
    lv_obj_remove_style(arc_, NULL, LV_PART_KNOB);
}

void ParameterKnobWidget::createValueIndicator() {
    value_indicator_ = lv_line_create(container_);
    lv_obj_set_style_line_width(value_indicator_, INDICATOR_THICKNESS, 0);
    lv_obj_set_style_line_color(value_indicator_, lv_color_hex(BaseTheme::Color::KNOB_VALUE), 0);
    lv_obj_set_style_line_rounded(value_indicator_, true, 0);

    // Line from center to current value position
    line_points_[0].x = arc_center_x_;
    line_points_[0].y = arc_center_y_;

    // Initial position at origin
    float initial_angle = normalizedToAngle(origin_);
    float angle_rad = angleToRadians(initial_angle);
    // LVGL coordinate system: Y increases downward, so we negate sin
    line_points_[1].x = arc_center_x_ + static_cast<lv_coord_t>(ARC_RADIUS * cosf(angle_rad));
    line_points_[1].y = arc_center_y_ - static_cast<lv_coord_t>(ARC_RADIUS * sinf(angle_rad));

    lv_line_set_points(value_indicator_, line_points_, 2);
}

void ParameterKnobWidget::createNameLabel() {
    name_label_ = lv_label_create(container_);

    lv_obj_set_style_text_font(name_label_, fonts.parameter_label, 0);
    lv_obj_set_style_text_color(name_label_, lv_color_hex(BaseTheme::Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_align(name_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(name_label_, LABEL_LINE_SPACING, 0);

    lv_obj_set_width(name_label_, width_ - LABEL_HORIZONTAL_PADDING);
    lv_obj_set_height(name_label_, LABEL_HEIGHT);
    lv_label_set_long_mode(name_label_, LV_LABEL_LONG_WRAP);

    lv_obj_align(name_label_, LV_ALIGN_TOP_MID, 0, getArcBottom() - ARC_LABEL_GAP);
}

void ParameterKnobWidget::createCenterCircles() {
    lv_coord_t center_y = getCenterY(CENTER_CIRCLE_SIZE);
    lv_coord_t inner_y = getCenterY(INNER_CIRCLE_SIZE);

    // Outer circle (same color as indicator line)
    center_circle_ = lv_obj_create(container_);
    lv_obj_set_size(center_circle_, CENTER_CIRCLE_SIZE, CENTER_CIRCLE_SIZE);
    lv_obj_align(center_circle_, LV_ALIGN_TOP_MID, 0, center_y);
    lv_obj_set_style_radius(center_circle_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(center_circle_, 0, 0);
    lv_obj_set_style_bg_color(center_circle_, lv_color_hex(BaseTheme::Color::KNOB_VALUE), 0);
    lv_obj_set_style_bg_opa(center_circle_, LV_OPA_COVER, 0);

    // Inner circle (inactive background, flashes on value change)
    inner_circle_ = lv_obj_create(container_);
    lv_obj_set_size(inner_circle_, INNER_CIRCLE_SIZE, INNER_CIRCLE_SIZE);
    lv_obj_align(inner_circle_, LV_ALIGN_TOP_MID, 0, inner_y);
    lv_obj_set_style_radius(inner_circle_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(inner_circle_, 0, 0);
    lv_obj_set_style_bg_color(inner_circle_, lv_color_hex(BaseTheme::Color::INACTIVE), 0);
    lv_obj_set_style_bg_opa(inner_circle_, LV_OPA_COVER, 0);
}

// ===== VALUE UPDATE =====

void ParameterKnobWidget::updateValue() {
    if (!arc_ || !value_indicator_) return;

    // Convert normalized values to angles
    float origin_angle = normalizedToAngle(origin_);
    float value_angle = normalizedToAngle(value_);

    // Only update arc if angles changed (avoid redundant LVGL calls)
    if (origin_angle != last_origin_angle_ || value_angle != last_value_angle_) {
        // Arc extends from origin to value (bidirectional)
        // LVGL draws arc clockwise, so swap angles when value < origin
        if (value_ >= origin_) {
            lv_arc_set_angles(arc_, origin_angle, value_angle);
        } else {
            lv_arc_set_angles(arc_, value_angle, origin_angle);
        }

        last_origin_angle_ = origin_angle;
        last_value_angle_ = value_angle;
    }

    // Indicator line uses the same calculated angle (no duplication)
    updateIndicatorLine(value_angle);
}

void ParameterKnobWidget::updateIndicatorLine(float value_angle) {
    // Compute sin and cos simultaneously (faster on ARM)
    float sin_val, cos_val;
    arm_sin_cos_f32(value_angle, &sin_val, &cos_val);

    line_points_[1].x = arc_center_x_ + static_cast<lv_coord_t>(ARC_RADIUS * cos_val);
    line_points_[1].y = arc_center_y_ + static_cast<lv_coord_t>(ARC_RADIUS * sin_val);

    lv_line_set_points(value_indicator_, line_points_, 2);
}

// ===== ANIMATION =====

void ParameterKnobWidget::triggerValueChangeFlash() {
    if (!inner_circle_) return;

    // Cancel existing timer if any
    if (flash_timer_) {
        lv_timer_delete(flash_timer_);
        flash_timer_ = nullptr;
    }

    // Flash inner circle
    lv_obj_set_style_bg_color(inner_circle_, lv_color_hex(BaseTheme::Color::ACTIVE), 0);
    flash_timer_ = lv_timer_create(flashTimerCallback, FLASH_DURATION_MS, this);
    lv_timer_set_repeat_count(flash_timer_, 1);
}

void ParameterKnobWidget::flashTimerCallback(lv_timer_t* timer) {
    auto* widget = static_cast<ParameterKnobWidget*>(lv_timer_get_user_data(timer));
    if (!widget || !widget->inner_circle_) return;

    lv_obj_set_style_bg_color(widget->inner_circle_, lv_color_hex(BaseTheme::Color::INACTIVE), 0);
    widget->flash_timer_ = nullptr;
}

// ===== GEOMETRY HELPERS =====
// (Implementations moved to header as inline functions for performance)

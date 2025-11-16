#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "IParameterWidget.hpp"

/**
 * @brief Knob widget for continuous parameters (normal and centered)
 *
 * Displays a circular arc knob with parameter name and value indicator.
 * Supports two modes:
 * - Normal: Arc starts from 0° (left)
 * - Centered: Arc is centered at 180° (useful for pan, balance)
 */
class ParameterKnobWidget : public IParameterWidget {
public:
    /**
     * @brief Construct a knob widget
     * @param parent Parent LVGL object
     * @param width Widget width
     * @param height Widget height
     * @param color_index Color index for center circle (0-7)
     * @param centered True for centered knob (pan/balance), false for normal (level)
     */
    ParameterKnobWidget(lv_obj_t* parent, uint16_t width = 80, uint16_t height = 120,
                        uint8_t color_index = 0, bool centered = false);
    ~ParameterKnobWidget() override;

    // IParameterWidget interface
    void setName(const String& name) override;
    void setValue(float value) override;
    void setValueWithDisplay(float value, const char* displayValue) override;
    void setVisible(bool visible) override;
    lv_obj_t* getContainer() const override { return container_; }

    // Knob-specific methods
    /**
     * @brief Set parameter origin for bidirectional controls
     * @param origin Origin point (0.0-1.0), e.g., 0.5 for pan/balance, 0.0 for level
     *
     * The arc track will extend from origin to value (can go both directions).
     */
    void setOrigin(float origin);

private:
    // Arc geometry
    static constexpr uint16_t ARC_SIZE = 62;
    static constexpr uint16_t ARC_RADIUS = ARC_SIZE / 2;
    static constexpr uint8_t ARC_WIDTH = 8;
    static constexpr uint8_t INDICATOR_THICKNESS = 8;
    static constexpr lv_coord_t ARC_Y_OFFSET = INDICATOR_THICKNESS / 2;
    static constexpr int16_t START_ANGLE = 135;
    static constexpr int16_t END_ANGLE = 45;
    static constexpr float ARC_SWEEP_DEGREES = 270.0f;  // Total arc sweep (END - START in circular space)

    // Center circles
    static constexpr uint8_t CENTER_CIRCLE_SIZE = 14;
    static constexpr uint8_t INNER_CIRCLE_SIZE = 6;

    // Label layout
    static constexpr lv_coord_t LABEL_HORIZONTAL_PADDING = 20;
    static constexpr lv_coord_t LABEL_HEIGHT = 36;
    static constexpr int8_t LABEL_LINE_SPACING = -2;
    static constexpr lv_coord_t ARC_LABEL_GAP = 4;

    // Flash animation
    static constexpr uint32_t FLASH_DURATION_MS = 100;

    // Performance thresholds
    static constexpr float VALUE_CHANGE_THRESHOLD = 0.001f;  // Avoid micro-updates (<0.1%)

    // UI creation
    void createUI();
    void createArc();
    void createValueIndicator();
    void createNameLabel();
    void createCenterCircles();

    // Value update
    void updateValue();
    void updateIndicatorLine(float value_angle);

    // Animation
    void triggerValueChangeFlash();
    static void flashTimerCallback(lv_timer_t* timer);

    // Geometry helpers (inline for performance)
    inline float normalizedToAngle(float normalized) const {
        return START_ANGLE + (normalized * ARC_SWEEP_DEGREES);
    }

    inline float angleToRadians(float angle) const {
        return angle * DEG_TO_RAD;  // Uses Arduino's DEG_TO_RAD macro
    }

    constexpr lv_coord_t getArcBottom() const {
        return ARC_Y_OFFSET + ARC_SIZE;
    }

    constexpr lv_coord_t getCenterY(lv_coord_t elementSize) const {
        return ARC_Y_OFFSET + ARC_RADIUS - (elementSize / 2);
    }

    // LVGL objects (pointers grouped for better cache alignment)
    lv_obj_t* parent_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* arc_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* value_indicator_ = nullptr;
    lv_obj_t* center_circle_ = nullptr;
    lv_obj_t* inner_circle_ = nullptr;
    lv_timer_t* flash_timer_ = nullptr;

    // State (floats grouped)
    float value_ = 0.0f;
    float origin_ = 0.0f;
    float last_origin_angle_ = -1.0f;  // Cache to avoid redundant LVGL calls
    float last_value_angle_ = -1.0f;

    // Geometry (coordinates grouped)
    lv_coord_t arc_center_x_;
    lv_coord_t arc_center_y_;
    lv_point_precise_t line_points_[2];

    // Dimensions
    uint16_t width_;
    uint16_t height_;

    // Name (variable size, last)
    String name_;
};

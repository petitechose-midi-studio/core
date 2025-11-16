#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <etl/array.h>
#include <etl/string.h>

#include "IParameterWidget.hpp"

/**
 * @brief List/Enum widget for discrete selection parameters
 *
 * Displays parameter name and current selection text.
 * No arc - just text display (e.g., "Sine Wave", "Sawtooth", "50%")
 */
class ParameterListWidget : public IParameterWidget {
public:
    /**
     * @brief Construct a list widget
     * @param parent Parent LVGL object
     * @param width Widget width
     * @param height Widget height
     * @param color_index Color index for border (0-7)
     * @param discreteCount Number of discrete values (>2)
     */
    ParameterListWidget(lv_obj_t* parent, uint16_t width = 80, uint16_t height = 120,
                        uint8_t color_index = 0, int16_t discreteCount = 3);
    ~ParameterListWidget() override;

    // IParameterWidget interface
    void setName(const String& name) override;
    void setValue(float value) override;
    void setValueWithDisplay(float value, const char* displayValue) override;
    void setDiscreteMetadata(int16_t discreteCount,
                            const etl::vector<etl::string<16>, 32>& valueNames,
                            uint8_t currentIndex) override;
    void setVisible(bool visible) override;
    lv_obj_t* getContainer() const override { return container_; }

private:
    static constexpr uint16_t VALUE_BOX_SIZE = 62;
    static constexpr lv_coord_t VALUE_BOX_Y_OFFSET = 4;
    static constexpr uint32_t FLASH_DURATION_MS = 100;

    void createUI();
    void createValueBox();
    void createValueLabel();
    void createTopLine();
    void createNameLabel();
    void triggerValueChangeFlash();
    static void flashTimerCallback(lv_timer_t* timer);

    lv_obj_t* parent_;
    uint16_t width_;
    uint16_t height_;
    uint8_t color_index_;
    int16_t discrete_count_;
    String name_;
    String display_value_;
    float value_ = 0.0f;

    // Optimistic display metadata (from Bitwig)
    etl::vector<etl::string<16>, 32> discrete_value_names_;
    uint8_t current_value_index_ = 0;
    bool has_discrete_metadata_ = false;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* value_box_ = nullptr;
    lv_obj_t* value_label_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
    lv_obj_t* top_line_ = nullptr;

    lv_timer_t* flash_timer_ = nullptr;
};

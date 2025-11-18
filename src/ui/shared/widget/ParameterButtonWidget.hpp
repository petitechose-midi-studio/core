#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <etl/array.h>
#include <etl/string.h>

#include "IParameterWidget.hpp"

/**
 * @brief Button/Toggle widget for binary on/off parameters
 *
 * Displays parameter name and ON/OFF state with colored box.
 * State changes via background color and label text.
 */
class ParameterButtonWidget : public IParameterWidget {
public:
    /**
     * @brief Construct a button widget
     * @param parent Parent LVGL object
     * @param width Widget width
     * @param height Widget height
     * @param color_index Color index for active state (0-7)
     */
    ParameterButtonWidget(lv_obj_t* parent, uint16_t width = 80, uint16_t height = 120,
                          uint8_t color_index = 0);
    ~ParameterButtonWidget() override;

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
    static constexpr uint16_t CONTAINER_SIZE = 62;     // Container size (matches other widgets)
    static constexpr uint16_t BUTTON_SIZE = 40;        // Inner button size (smaller, centered)
    static constexpr lv_coord_t BUTTON_Y_OFFSET = 4;   // Same as other widgets for alignment

    void createUI();
    void createButtonBox();
    void createStateLabel();
    void createNameLabel();
    void updateButtonState(bool isOn);

    lv_obj_t* parent_;
    uint16_t width_;
    uint16_t height_;
    uint8_t color_index_;
    String name_;
    bool is_on_ = false;

    // Optimistic display metadata (from Bitwig)
    etl::vector<etl::string<16>, 32> discrete_value_names_;
    uint8_t current_value_index_ = 0;
    bool has_discrete_metadata_ = false;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* button_box_ = nullptr;
    lv_obj_t* state_label_ = nullptr;
    lv_obj_t* name_label_ = nullptr;
};

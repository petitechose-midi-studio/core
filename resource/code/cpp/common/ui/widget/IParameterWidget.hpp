#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <etl/array.h>
#include <etl/string.h>
#include <etl/vector.h>

/**
 * @brief Interface for parameter widgets (polymorphism)
 *
 * Defines common operations for all parameter widget types:
 * - ParameterKnobWidget (continuous/centered knobs)
 * - ParameterListWidget (enum/list selectors)
 * - ParameterButtonWidget (toggle buttons)
 */
class IParameterWidget {
public:
    virtual ~IParameterWidget() = default;

    /**
     * @brief Set parameter name
     * @param name Parameter name (e.g., "Cutoff", "Waveform")
     */
    virtual void setName(const String& name) = 0;

    /**
     * @brief Set normalized value (0.0-1.0)
     * @param value Normalized value
     */
    virtual void setValue(float value) = 0;

    /**
     * @brief Set value with formatted display text
     * @param value Normalized value (0.0-1.0)
     * @param displayValue Formatted text (e.g., "50.0 Hz", "Sine")
     */
    virtual void setValueWithDisplay(float value, const char* displayValue) = 0;

    /**
     * @brief Set discrete value metadata for optimistic display (List/Button widgets only)
     * @param discreteCount Total number of discrete values
     * @param valueNames Array of discrete value names (e.g., ["Off", "On"] or ["Sine", "Saw", "Square"])
     * @param currentIndex Current index in valueNames array
     *
     * This enables optimistic UI updates: when user changes value, widget can calculate
     * new index locally and display valueNames[newIndex] immediately without waiting for host.
     */
    virtual void setDiscreteMetadata(int16_t discreteCount,
                                     const etl::vector<etl::string<16>, 32>& valueNames,
                                     uint8_t currentIndex) {
        // Default implementation does nothing (for Knob widgets)
        (void)discreteCount;
        (void)valueNames;
        (void)currentIndex;
    }

    /**
     * @brief Show or hide widget
     * @param visible True to show, false to hide
     */
    virtual void setVisible(bool visible) = 0;

    /**
     * @brief Get widget container for layout management
     * @return LVGL container object
     */
    virtual lv_obj_t* getContainer() const = 0;
};

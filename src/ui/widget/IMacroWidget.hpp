#pragma once

/**
 * @file IMacroWidget.hpp
 * @brief Macro widget interface
 */

#include <cstdint>
#include <oc/ui/lvgl/IWidget.hpp>

namespace core::ui {

/**
 * @brief Interface for macro widgets (knob or button)
 */
class IMacroWidget : public oc::ui::lvgl::IWidget {
public:
    ~IMacroWidget() override = default;

    /// Set the parameter value [0.0, 1.0]
    virtual void setValue(float value) = 0;

    /// Update config labels (channel 0-15 raw, CC 0-127)
    virtual void setConfig(uint8_t channel, uint8_t cc) = 0;
};

}  // namespace core::ui

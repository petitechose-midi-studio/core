#pragma once

/**
 * @file MacroKnobWidget.hpp
 * @brief Knob-style macro widget
 */

#include <memory>
#include <oc/ui/lvgl/widget/KnobWidget.hpp>
#include "BaseMacroWidget.hpp"

namespace core::ui {

/**
 * @brief Macro widget with rotary knob visualization
 */
class MacroKnobWidget : public BaseMacroWidget {
public:
    MacroKnobWidget(lv_obj_t* parent, uint8_t index);
    ~MacroKnobWidget() override;

    // IMacroWidget
    void setValue(float value) override;

    // Direct access for advanced configuration
    oc::ui::lvgl::KnobWidget& knob() { return *knob_; }

private:
    void createUI(lv_obj_t* parent);

    std::unique_ptr<oc::ui::lvgl::KnobWidget> knob_;
};

}  // namespace core::ui

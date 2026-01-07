#pragma once

/**
 * @file MacroButtonWidget.hpp
 * @brief Button-style macro widget
 */

#include <memory>
#include <oc/ui/lvgl/widget/ButtonWidget.hpp>
#include "BaseMacroWidget.hpp"

namespace core::ui {

/**
 * @brief Macro widget with toggle button visualization
 */
class MacroButtonWidget : public BaseMacroWidget {
public:
    /**
     * @param parent LVGL parent object
     * @param index Macro index (0-7)
     */
    MacroButtonWidget(lv_obj_t* parent, uint8_t index);
    ~MacroButtonWidget() override;

    // IMacroWidget
    void setValue(float value) override;

    // Direct access
    oc::ui::lvgl::ButtonWidget& button() { return *button_; }

private:
    void createUI(lv_obj_t* parent);

    std::unique_ptr<oc::ui::lvgl::ButtonWidget> button_;
};

}  // namespace core::ui

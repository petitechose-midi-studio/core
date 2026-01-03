#pragma once

#include <memory>
#include <oc/ui/lvgl/widget/ButtonWidget.hpp>
#include "BaseMacroWidget.hpp"

namespace ui {

/**
 * @brief Macro widget with toggle button visualization
 */
class MacroButtonWidget : public BaseMacroWidget {
public:
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

}  // namespace ui

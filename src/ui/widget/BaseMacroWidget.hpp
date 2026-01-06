#pragma once

#include <memory>
#include <lvgl.h>
#include <oc/ui/lvgl/widget/Label.hpp>
#include "IMacroWidget.hpp"

namespace core::ui {

/**
 * @brief Base class for macro widgets with common grid layout and config labels
 *
 * Grid layout:
 * - Row 0 (FR(1)): Widget area (knob or button)
 * - Row 1 (CONTENT): Vertical stack with CH and CC lines
 */
class BaseMacroWidget : public IMacroWidget {
public:
    ~BaseMacroWidget() override;

    // Non-copyable, non-movable
    BaseMacroWidget(const BaseMacroWidget&) = delete;
    BaseMacroWidget& operator=(const BaseMacroWidget&) = delete;
    BaseMacroWidget(BaseMacroWidget&&) = delete;
    BaseMacroWidget& operator=(BaseMacroWidget&&) = delete;

    // IWidget
    lv_obj_t* getElement() const override { return container_; }

    // IMacroWidget
    void setConfig(uint8_t channel, uint8_t cc) override;

protected:
    explicit BaseMacroWidget(uint8_t index);

    void createContainerWithGrid(lv_obj_t* parent);
    void createConfigLabels(lv_obj_t* labelParent);  // Floating labels inside widget

    lv_obj_t* container_ = nullptr;

    // Config labels (framework Labels)
    std::unique_ptr<oc::ui::lvgl::Label> ch_prefix_;   // "CH" (small, dim)
    std::unique_ptr<oc::ui::lvgl::Label> ch_value_;    // channel number
    std::unique_ptr<oc::ui::lvgl::Label> cc_prefix_;   // "CC" (small, dim)
    std::unique_ptr<oc::ui::lvgl::Label> cc_value_;    // CC number

    uint8_t index_ = 0;
};

}  // namespace core::ui

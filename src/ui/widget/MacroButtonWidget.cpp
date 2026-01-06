#include "MacroButtonWidget.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace Theme = oc::ui::lvgl::BaseTheme;
namespace style = oc::ui::lvgl::style;

MacroButtonWidget::MacroButtonWidget(lv_obj_t* parent, uint8_t index)
    : BaseMacroWidget(index) {
    createUI(parent);
}

MacroButtonWidget::~MacroButtonWidget() {
    button_.reset();
}

void MacroButtonWidget::createUI(lv_obj_t* parent) {
    createContainerWithGrid(parent);

    // ButtonWidget - stretch horizontally, CONTENT row sizes to button height
    button_ = std::make_unique<oc::ui::lvgl::ButtonWidget>(container_);
    button_->sizeMode(oc::ui::lvgl::SizeMode::SquareFromWidth)  // Explicit: height = width
            .offColor(Theme::Color::KNOB_BACKGROUND)
            .onColor(Theme::Color::getMacroColor(index_));
    lv_obj_set_grid_cell(button_->getElement(),
        LV_GRID_ALIGN_STRETCH, 0, 1,  // Horizontal: stretch to get width
        LV_GRID_ALIGN_START, 0, 1);   // Vertical: start in CONTENT row

    // Floating labels (can now be sibling since ButtonWidget no longer subtracts sibling height)
    createConfigLabels(container_);
}

void MacroButtonWidget::setValue(float value) {
    if (button_) {
        button_->setState(value > 0.5f);
    }
}

}  // namespace core::ui

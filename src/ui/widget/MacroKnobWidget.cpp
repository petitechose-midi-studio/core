#include "MacroKnobWidget.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace Theme = oc::ui::lvgl::BaseTheme;
namespace style = oc::ui::lvgl::style;

MacroKnobWidget::MacroKnobWidget(lv_obj_t* parent, uint8_t index)
    : BaseMacroWidget(index) {
    createUI(parent);
}

MacroKnobWidget::~MacroKnobWidget() {
    knob_.reset();
    // BaseMacroWidget destructor handles labels and container
}

void MacroKnobWidget::createUI(lv_obj_t* parent) {
    createContainerWithGrid(parent);

    // KnobWidget - stretch horizontally, CONTENT row sizes to knob height
    knob_ = std::make_unique<oc::ui::lvgl::KnobWidget>(container_);
    knob_->sizeMode(oc::ui::lvgl::SizeMode::SquareFromWidth)  // Explicit: height = width
          .centered(false)
          .bgColor(Theme::Color::KNOB_BACKGROUND)
          .trackColor(Theme::Color::getMacroColor(index_))
          .valueColor(Theme::Color::KNOB_VALUE)
          .flashColor(Theme::Color::getMacroColor(index_));
    lv_obj_set_grid_cell(knob_->getElement(),
        LV_GRID_ALIGN_STRETCH, 0, 1,  // Horizontal: stretch to get width
        LV_GRID_ALIGN_START, 0, 1);   // Vertical: start in CONTENT row

    // Floating labels (can now be sibling since KnobWidget no longer subtracts sibling height)
    createConfigLabels(container_);
}

void MacroKnobWidget::setValue(float value) {
    if (knob_) {
        knob_->setValue(value);
    }
}

}  // namespace core::ui

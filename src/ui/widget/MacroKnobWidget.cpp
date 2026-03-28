#include "MacroKnobWidget.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = oc::ui::lvgl::base_theme;

MacroKnobWidget::MacroKnobWidget(lv_obj_t* parent, uint8_t index)
    : BaseMacroWidget(index) {
    createUI(parent);
}

MacroKnobWidget::~MacroKnobWidget() {
    knob_.reset();
}

void MacroKnobWidget::createUI(lv_obj_t* parent) {
    createContainerWithGrid(parent);

    knob_ = std::make_unique<oc::ui::lvgl::KnobWidget>(container_);
    knob_
        ->sizeMode(oc::ui::lvgl::SizeMode::SquareFromWidth)
        .renderProfile(oc::ui::lvgl::KnobRenderProfile::ArcOnly)
        .centered(false)
        .bgColor(theme::color::KNOB_BACKGROUND)
        .trackColor(theme::color::KNOB_VALUE)
        .flashEnabled(false);
    knob_->setRibbonEnabled(false);

    lv_obj_set_grid_cell(knob_->getElement(),
        LV_GRID_ALIGN_STRETCH, 0, 1,
        LV_GRID_ALIGN_START, 0, 1);

    createConfigLabels(container_);
}

void MacroKnobWidget::setValue(float value) {
    if (!knob_) return;
    knob_->setValue(value);
}

}  // namespace core::ui

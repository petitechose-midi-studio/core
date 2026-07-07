#include "BaseMacroWidget.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include "ui/font/StandaloneIcons.hpp"
#include "ui/font/StandaloneFonts.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace style = oc::ui::lvgl::style;
namespace stheme = standalone::theme;
namespace icons = standalone::icons;

FLASHMEM BaseMacroWidget::BaseMacroWidget(uint8_t index)
    : index_(index) {}

FLASHMEM BaseMacroWidget::~BaseMacroWidget() {
    cc_value_.reset();
    cc_prefix_.reset();

    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}

FLASHMEM void BaseMacroWidget::createContainerWithGrid(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    style::apply(container_).transparent().noScroll().noBorder().pad(0);

    // Give the knob a full-height lane so each macro stays centered vertically
    // within the shared 4x2 grid, even when side strips reserve space.
    static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(container_, col_dsc, row_dsc);
    lv_obj_set_layout(container_, LV_LAYOUT_GRID);
}

FLASHMEM void BaseMacroWidget::createConfigLabels(lv_obj_t* labelParent) {
    config_label_container_ = lv_obj_create(labelParent);
    style::apply(config_label_container_).transparent().noScroll();
    lv_obj_set_size(config_label_container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_flag(config_label_container_, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(config_label_container_, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_layout(config_label_container_, LV_LAYOUT_GRID);

    // Fixed-width columns keep the CC icon and value visually stable.
    static constexpr lv_coord_t COL_WIDTH = 18;
    static const int32_t col_dsc[] = {COL_WIDTH, COL_WIDTH, LV_GRID_TEMPLATE_LAST};
    static const int32_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(config_label_container_, col_dsc, row_dsc);
    lv_obj_set_style_pad_column(config_label_container_, 2, 0);

    // CC icon - right aligned in left column
    cc_prefix_ = std::make_unique<oc::ui::lvgl::Label>(config_label_container_);
    cc_prefix_->alignment(LV_TEXT_ALIGN_RIGHT)
              .color(stheme::color::MACRO_CC_COLOR)
              .autoScroll(false)
              .ownsLvglObjects(false);
    style::apply(cc_prefix_->getElement()).textOpa(stheme::color::MACRO_PREFIX_OPA);
    if (standalone_fonts.icons_12) {
        cc_prefix_->font(standalone_fonts.icons_12);
    }
    cc_prefix_->setText(icons::MIDI_CC);
    lv_obj_set_grid_cell(cc_prefix_->getElement(),
        LV_GRID_ALIGN_STRETCH, 0, 1,
        LV_GRID_ALIGN_CENTER, 0, 1);

    // CC value - left aligned in right column
    cc_value_ = std::make_unique<oc::ui::lvgl::Label>(config_label_container_);
    cc_value_->alignment(LV_TEXT_ALIGN_LEFT)
             .color(stheme::color::MACRO_CC_COLOR)
             .autoScroll(false)
             .ownsLvglObjects(false);
    if (fonts.inter_13_bold) {
        cc_value_->font(fonts.inter_13_bold);
    }
    cc_value_->setText("-");
    lv_obj_set_grid_cell(cc_value_->getElement(),
        LV_GRID_ALIGN_STRETCH, 1, 1,
        LV_GRID_ALIGN_CENTER, 0, 1);
}

void BaseMacroWidget::setConfig(uint8_t channel, uint8_t cc) {
    (void)channel;
    if (cc_value_ && current_cc_ != cc) {
        cc_value_->setText(static_cast<int>(cc));
        current_cc_ = cc;
    }
}

void BaseMacroWidget::setConfigLabelsVisible(bool visible) {
    if (!config_label_container_) return;
    if (config_labels_visible_ == visible) return;
    config_labels_visible_ = visible;
    if (visible) {
        lv_obj_clear_flag(config_label_container_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(config_label_container_, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace core::ui

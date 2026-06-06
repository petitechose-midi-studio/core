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
    ch_value_.reset();
    ch_prefix_.reset();

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
    lv_obj_t* labelContainer = lv_obj_create(labelParent);
    style::apply(labelContainer).transparent().noScroll();
    lv_obj_set_size(labelContainer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_flag(labelContainer, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(labelContainer, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_flex_flow(labelContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(labelContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(labelContainer, 0, 0);

    // Shared grid definition for CH and CC rows
    static constexpr lv_coord_t COL_WIDTH = 18;
    static const int32_t col_dsc[] = {COL_WIDTH, COL_WIDTH, LV_GRID_TEMPLATE_LAST};
    static const int32_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

    // --- CH line: 2 fixed-width columns for perfect alignment ---
    lv_obj_t* chRow = lv_obj_create(labelContainer);
    style::apply(chRow).transparent().noScroll();
    lv_obj_set_size(chRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_dsc_array(chRow, col_dsc, row_dsc);
    lv_obj_set_layout(chRow, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(chRow, 2, 0);

    // Channel icon - right aligned in left column
    ch_prefix_ = std::make_unique<oc::ui::lvgl::Label>(chRow);
    ch_prefix_->alignment(LV_TEXT_ALIGN_RIGHT)
              .color(stheme::color::MACRO_CH_COLOR)
              .autoScroll(false)
              .ownsLvglObjects(false);
    style::apply(ch_prefix_->getElement()).textOpa(stheme::color::MACRO_PREFIX_OPA);
    if (standalone_fonts.icons_12) {
        ch_prefix_->font(standalone_fonts.icons_12);
    }
    ch_prefix_->setText(icons::MIDI_CHANNEL);
    lv_obj_set_grid_cell(ch_prefix_->getElement(),
        LV_GRID_ALIGN_STRETCH, 0, 1,
        LV_GRID_ALIGN_CENTER, 0, 1);

    // Channel value - left aligned in right column
    ch_value_ = std::make_unique<oc::ui::lvgl::Label>(chRow);
    ch_value_->alignment(LV_TEXT_ALIGN_LEFT)
             .color(stheme::color::MACRO_CH_COLOR)
             .autoScroll(false)
             .ownsLvglObjects(false);
    if (fonts.inter_13_bold) {
        ch_value_->font(fonts.inter_13_bold);
    }
    ch_value_->setText("-");
    lv_obj_set_grid_cell(ch_value_->getElement(),
        LV_GRID_ALIGN_STRETCH, 1, 1,
        LV_GRID_ALIGN_CENTER, 0, 1);

    // --- CC line: same structure as CH ---
    lv_obj_t* ccRow = lv_obj_create(labelContainer);
    style::apply(ccRow).transparent().noScroll();
    lv_obj_set_size(ccRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_grid_dsc_array(ccRow, col_dsc, row_dsc);
    lv_obj_set_layout(ccRow, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(ccRow, 2, 0);

    // CC icon - right aligned in left column
    cc_prefix_ = std::make_unique<oc::ui::lvgl::Label>(ccRow);
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
    cc_value_ = std::make_unique<oc::ui::lvgl::Label>(ccRow);
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
    if (ch_value_ && current_channel_ != channel) {
        ch_value_->setText(static_cast<int>(channel + 1));  // 1-indexed
        current_channel_ = channel;
    }
    if (cc_value_ && current_cc_ != cc) {
        cc_value_->setText(static_cast<int>(cc));
        current_cc_ = cc;
    }
}

}  // namespace core::ui

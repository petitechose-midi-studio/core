#include "ui/view/MainViewFrame.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

namespace style = oc::ui::lvgl::style;

namespace core::ui {

namespace {

constexpr lv_coord_t STRUCTURE_ROW_HEIGHT = 6;

}  // namespace

MainViewFrame::MainViewFrame(lv_obj_t* parent) {
    layout_ = std::make_unique<ms::ui::LayoutView>(parent);
    container_ = layout_->getElement();
    header_root_ = layout_->header();
    body_ = layout_->content();

    style::apply(header_root_).transparent().pad(0);
    lv_obj_set_layout(header_root_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header_root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        header_root_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );
    lv_obj_set_style_pad_row(header_root_, 0, 0);

    header_ = lv_obj_create(header_root_);
    style::apply(header_).size(LV_PCT(100), LV_SIZE_CONTENT).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_layout(header_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(header_, 0, 0);

    style::apply(body_).transparent().pad(0).noScroll();
    lv_obj_set_layout(body_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(body_, 0, 0);
}

void MainViewFrame::createInteractionRow() {
    if (interaction_row_ || !body_) return;

    interaction_row_ = lv_obj_create(body_);
    style::apply(interaction_row_).size(LV_PCT(100), LV_PCT(100)).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_flex_grow(interaction_row_, 1);
    lv_obj_set_layout(interaction_row_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(interaction_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        interaction_row_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );
    lv_obj_set_style_pad_column(interaction_row_, 0, 0);
}

void MainViewFrame::createCenterColumn() {
    if (center_column_ || !interaction_row_) return;

    center_column_ = lv_obj_create(interaction_row_);
    style::apply(center_column_).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_width(center_column_, 0);
    lv_obj_set_height(center_column_, LV_PCT(100));
    lv_obj_set_flex_grow(center_column_, 1);
    lv_obj_set_layout(center_column_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(center_column_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(center_column_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(center_column_, 0, 0);
}

void MainViewFrame::createStructureRow() {
    if (structure_row_ || !header_root_) return;

    structure_row_ = lv_obj_create(header_root_);
    style::apply(structure_row_)
        .size(LV_PCT(100), STRUCTURE_ROW_HEIGHT)
        .transparent()
        .noBorder()
        .pad(0)
        .noScroll();
    lv_obj_add_flag(structure_row_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_move_to_index(structure_row_, 0);
}

}  // namespace core::ui

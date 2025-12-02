#include "HintBar.hpp"

HintBar::HintBar(lv_obj_t* parent, HintBarPosition position)
    : position_(position), parent_(parent) {
    if (!parent_) return;

    container_ = lv_obj_create(parent_);
    if (!container_) return;

    // Transparent background
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(container_, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    applyGridLayout();
}

HintBar::~HintBar() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}

void HintBar::applyGridLayout() {
    if (!container_) return;

    if (position_ == HintBarPosition::BOTTOM) {
        // Horizontal: 3 equal columns, 1 row taking full height
        // LV_GRID_FR(1) ensures the row takes full container height for proper vertical centering
        static const lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                             LV_GRID_TEMPLATE_LAST};
        static const lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

        lv_obj_set_size(container_, LV_PCT(100), size_);
        lv_obj_set_layout(container_, LV_LAYOUT_GRID);
        lv_obj_set_grid_dsc_array(container_, col_dsc, row_dsc);
    } else {
        // Vertical (Left or Right): 1 column with content width, 3 equal rows
        static const lv_coord_t col_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
        static const lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                             LV_GRID_TEMPLATE_LAST};

        lv_obj_set_size(container_, size_, LV_PCT(100));
        lv_obj_set_layout(container_, LV_LAYOUT_GRID);
        lv_obj_set_grid_dsc_array(container_, col_dsc, row_dsc);
        // Align column based on position
        lv_grid_align_t col_align =
            (position_ == HintBarPosition::LEFT) ? LV_GRID_ALIGN_START : LV_GRID_ALIGN_END;
        lv_obj_set_style_grid_column_align(container_, col_align, LV_STATE_DEFAULT);
    }
}

void HintBar::setSize(lv_coord_t size) {
    size_ = size;
    if (container_) {
        if (position_ == HintBarPosition::BOTTOM) {
            lv_obj_set_height(container_, size_);
        } else {
            lv_obj_set_width(container_, size_);
        }
    }
}

void HintBar::setCell(int index, lv_obj_t* element) {
    if (!container_ || !element || index < 0 || index >= 3) return;

    if (position_ == HintBarPosition::BOTTOM) {
        // Horizontal: columns 0,1,2
        // col_align: START/CENTER/END for horizontal positioning within cell
        // row_align: CENTER for vertical centering
        static const lv_grid_align_t h_aligns[] = {LV_GRID_ALIGN_START, LV_GRID_ALIGN_CENTER,
                                                   LV_GRID_ALIGN_END};
        lv_obj_set_grid_cell(element, h_aligns[index], index, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        // Edge padding: 12px on outer edges
        if (index == 0) lv_obj_set_style_pad_left(element, 24, LV_STATE_DEFAULT);
        else if (index == 2) lv_obj_set_style_pad_right(element, 24, LV_STATE_DEFAULT);
    } else {
        // Vertical: rows 0,1,2
        // row_align: START/CENTER/END for vertical positioning within cell
        // col_align: START (Left) or END (Right) for horizontal alignment
        static const lv_grid_align_t v_aligns[] = {LV_GRID_ALIGN_START, LV_GRID_ALIGN_CENTER,
                                                   LV_GRID_ALIGN_END};
        lv_grid_align_t h_align =
            (position_ == HintBarPosition::LEFT) ? LV_GRID_ALIGN_START : LV_GRID_ALIGN_END;
        lv_obj_set_grid_cell(element, h_align, 0, 1, v_aligns[index], index, 1);
        // Edge padding: 12px on outer edges
        if (index == 0) lv_obj_set_style_pad_top(element, 24, LV_STATE_DEFAULT);
        else if (index == 2) lv_obj_set_style_pad_bottom(element, 24, LV_STATE_DEFAULT);
    }
}

void HintBar::show() {
    if (container_) lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void HintBar::hide() {
    if (container_) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

bool HintBar::isVisible() const {
    return container_ && !lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

#include "ui/common/TrackHeaderRow.hpp"

#include <cstring>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace style = oc::ui::lvgl::style;

namespace {

constexpr uint32_t COLOR_DIM_TEXT = theme::color::TEXT_PRIMARY;
constexpr lv_coord_t ROW_HEIGHT = 20;
constexpr lv_coord_t HORIZONTAL_INSET = oc::ui::lvgl::base_theme::layout::MARGIN_SM + 4;
constexpr lv_opa_t LABEL_OPA = LV_OPA_80;
constexpr lv_coord_t ACCENT_WIDTH = 4;
constexpr lv_coord_t ITEM_SIZE_WIDE = 9;
constexpr lv_coord_t ITEM_GAP_WIDE = 4;
constexpr lv_coord_t ITEM_SIZE_DENSE = 7;
constexpr lv_coord_t ITEM_GAP_DENSE = 3;
constexpr lv_coord_t CURSOR_HEIGHT = 3;
constexpr lv_coord_t CURSOR_OFFSET_Y = 1;
constexpr lv_opa_t ACTIVE_MIN_OPA = LV_OPA_70;

template <size_t N>
void setLabelTextIfChanged(lv_obj_t* label, std::array<char, N>& cache, const char* text) {
    if (!label) return;

    const char* next = (text && text[0]) ? text : "";
    if (std::strncmp(cache.data(), next, N) == 0) {
        return;
    }

    std::strncpy(cache.data(), next, N - 1);
    cache[N - 1] = '\0';
    lv_label_set_text(label, cache.data());
}

}  // namespace

TrackHeaderRow::TrackHeaderRow(lv_obj_t* parent) {
    createUI(parent);
}

TrackHeaderRow::~TrackHeaderRow() {
    if (container_) {
        lv_obj_delete(container_);
    }
}

FLASHMEM void TrackHeaderRow::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_)
        .size(LV_PCT(100), ROW_HEIGHT)
        .noScroll()
        .noBorder()
        .pad(0);
    lv_obj_set_style_pad_left(container_, 0, 0);
    lv_obj_set_style_pad_right(container_, HORIZONTAL_INSET, 0);
    lv_obj_set_style_pad_top(container_, 0, 0);
    lv_obj_set_style_pad_bottom(container_, 0, 0);
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container_, 4, 0);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    accent_ = lv_obj_create(container_);
    style::apply(accent_)
        .size(ACCENT_WIDTH, LV_PCT(100))
        .noBorder()
        .noScroll()
        .pad(0);
    lv_obj_set_style_radius(accent_, 0, 0);
    lv_obj_set_style_bg_opa(accent_, LV_OPA_COVER, 0);

    label_ = lv_label_create(container_);
    lv_obj_set_style_text_font(label_, fonts.inter_14_medium, 0);
    lv_obj_set_style_text_color(label_, lv_color_hex(COLOR_DIM_TEXT), 0);
    lv_obj_set_style_text_opa(label_, LABEL_OPA, 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_CLIP);

    spacer_ = lv_obj_create(container_);
    style::apply(spacer_).size(0, 1).transparent().noBorder().noScroll().pad(0);
    lv_obj_set_flex_grow(spacer_, 1);

    items_row_ = lv_obj_create(container_);
    style::apply(items_row_).transparent().noBorder().noScroll().pad(0);
    lv_obj_set_layout(items_row_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(items_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(items_row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(items_row_, ITEM_GAP_WIDE, 0);
    lv_obj_add_flag(items_row_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    for (uint8_t i = 0; i < items_.size(); ++i) {
        items_[i] = lv_obj_create(items_row_);
        style::apply(items_[i])
            .size(ITEM_SIZE_WIDE, ITEM_SIZE_WIDE)
            .noBorder()
            .noScroll()
            .pad(0);
        lv_obj_set_style_radius(items_[i], 1, 0);
        lv_obj_add_flag(items_[i], LV_OBJ_FLAG_OVERFLOW_VISIBLE);

        item_add_labels_[i] = lv_label_create(items_[i]);
        lv_label_set_text(item_add_labels_[i], "+");
        lv_obj_set_style_text_font(item_add_labels_[i], fonts.inter_13_bold, 0);
        lv_obj_set_style_text_color(item_add_labels_[i], lv_color_hex(theme::color::TEXT_PRIMARY), 0);
        lv_obj_set_style_text_opa(item_add_labels_[i], LV_OPA_COVER, 0);
        lv_obj_center(item_add_labels_[i]);
        lv_obj_add_flag(item_add_labels_[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(item_add_labels_[i], LV_OBJ_FLAG_HIDDEN);
    }

    selection_cursor_ = lv_obj_create(items_row_);
    lv_obj_remove_style_all(selection_cursor_);
    lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(selection_cursor_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(selection_cursor_, 1, 0);
    lv_obj_set_style_border_width(selection_cursor_, 0, 0);
    lv_obj_set_style_bg_opa(selection_cursor_, LV_OPA_80, 0);
    lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
}

void TrackHeaderRow::render(const TrackHeaderRowProps& props) {
    if (!container_) return;

    setLabelTextIfChanged(label_, left_text_cache_, props.leftText);

    const bool denseLayout = props.itemCount > 8;
    const lv_coord_t itemSize = denseLayout ? ITEM_SIZE_DENSE : ITEM_SIZE_WIDE;
    const lv_coord_t itemGap = denseLayout ? ITEM_GAP_DENSE : ITEM_GAP_WIDE;
    lv_obj_set_style_pad_column(items_row_, itemGap, 0);

    for (uint8_t i = 0; i < items_.size(); ++i) {
        lv_obj_set_size(items_[i], itemSize, itemSize);
        if (item_add_labels_[i]) {
            lv_obj_center(item_add_labels_[i]);
        }
    }

    if (!surface_cache_initialized_ || accent_cache_color_ != props.accentColor) {
        lv_obj_set_style_bg_color(accent_, lv_color_hex(props.accentColor), 0);
        accent_cache_color_ = props.accentColor;
    }
    if (!surface_cache_initialized_ || accent_cache_opa_ != props.accentOpa) {
        lv_obj_set_style_bg_opa(accent_, props.accentOpa, 0);
        accent_cache_opa_ = props.accentOpa;
    }

    if (!surface_cache_initialized_ || background_cache_color_ != props.backgroundColor) {
        lv_obj_set_style_bg_color(container_, lv_color_hex(props.backgroundColor), 0);
        background_cache_color_ = props.backgroundColor;
    }
    if (!surface_cache_initialized_ || background_cache_opa_ != props.backgroundOpa) {
        lv_obj_set_style_bg_opa(container_, props.backgroundOpa, 0);
        background_cache_opa_ = props.backgroundOpa;
    }

    for (uint8_t i = 0; i < items_.size(); ++i) {
        if (i < props.itemCount) {
            lv_obj_clear_flag(items_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(items_[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        if (!surface_cache_initialized_ || item_color_cache_[i] != props.itemColors[i]) {
            lv_obj_set_style_bg_color(items_[i], lv_color_hex(props.itemColors[i]), 0);
            item_color_cache_[i] = props.itemColors[i];
        }
        const bool cursorItem = props.showCursor && props.cursorIndex == i;
        const bool selected = (props.selectedMask & static_cast<uint16_t>(1U << i)) != 0;
        const bool active = props.itemActive[i];
        if (props.itemAddSlot[i]) {
            lv_obj_set_style_bg_opa(items_[i], cursorItem ? LV_OPA_20 : static_cast<lv_opa_t>(6), 0);
            lv_obj_set_style_border_width(items_[i], 0, 0);
            if (cursorItem) {
                lv_obj_clear_flag(item_add_labels_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(item_add_labels_[i], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_add_flag(item_add_labels_[i], LV_OBJ_FLAG_HIDDEN);
        }
        lv_opa_t emphasizedOpa = props.itemOpacities[i];
        if (active) {
            emphasizedOpa = static_cast<lv_opa_t>(std::max<uint16_t>(emphasizedOpa, ACTIVE_MIN_OPA));
        }
        if (!props.itemAddSlot[i]) {
            lv_obj_set_style_bg_opa(items_[i], emphasizedOpa, 0);
            lv_obj_set_style_border_width(items_[i], selected ? 1 : 0, 0);
        } else {
            lv_obj_set_style_bg_opa(items_[i], cursorItem ? LV_OPA_20 : static_cast<lv_opa_t>(6), 0);
            lv_obj_set_style_border_width(items_[i], 0, 0);
        }
        item_opa_cache_[i] = emphasizedOpa;
        item_selected_cache_[i] = selected;
        if (selected) {
            lv_obj_set_style_border_color(
                items_[i],
                lv_color_hex(props.cursorColor != 0 ? props.cursorColor : props.accentColor),
                0
            );
            lv_obj_set_style_border_opa(items_[i], LV_OPA_70, 0);
        }
    }

    if (selection_cursor_) {
        const bool showCursor = props.showCursor && props.cursorIndex < props.itemCount &&
                                props.cursorIndex < items_.size() && items_[props.cursorIndex] &&
                                !lv_obj_has_flag(items_[props.cursorIndex], LV_OBJ_FLAG_HIDDEN);

        if (!showCursor) {
            if (cursor_visible_cache_) {
                lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
                cursor_visible_cache_ = false;
            }
        } else {
            lv_obj_update_layout(items_row_);

            const lv_coord_t itemX = lv_obj_get_x(items_[props.cursorIndex]);
            const lv_coord_t itemY = lv_obj_get_y(items_[props.cursorIndex]);
            const lv_coord_t itemW = lv_obj_get_width(items_[props.cursorIndex]);
            const lv_coord_t itemH = lv_obj_get_height(items_[props.cursorIndex]);
            const lv_coord_t cursorWidth = std::max<lv_coord_t>(1, itemW - 2);
            const lv_coord_t cursorX = static_cast<lv_coord_t>(itemX + (itemW - cursorWidth) / 2);
            const lv_coord_t cursorY = static_cast<lv_coord_t>(itemY + itemH + CURSOR_OFFSET_Y);

            if (!cursor_visible_cache_) {
                lv_obj_clear_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
                cursor_visible_cache_ = true;
            }
            if (cursor_x_cache_ != cursorX || cursor_y_cache_ != cursorY) {
                lv_obj_set_pos(selection_cursor_, cursorX, cursorY);
                cursor_x_cache_ = cursorX;
                cursor_y_cache_ = cursorY;
            }
            if (cursor_width_cache_ != cursorWidth) {
                lv_obj_set_size(selection_cursor_, cursorWidth, CURSOR_HEIGHT);
                cursor_width_cache_ = cursorWidth;
            }
            const uint32_t cursorColor = props.cursorColor != 0 ? props.cursorColor : props.accentColor;
            if (cursor_color_cache_ != cursorColor) {
                lv_obj_set_style_bg_color(selection_cursor_, lv_color_hex(cursorColor), 0);
                cursor_color_cache_ = cursorColor;
            }
            if (cursor_opa_cache_ != props.cursorOpa) {
                lv_obj_set_style_bg_opa(selection_cursor_, props.cursorOpa, 0);
                cursor_opa_cache_ = props.cursorOpa;
            }
        }
    }

    surface_cache_initialized_ = true;
}

}  // namespace core::ui

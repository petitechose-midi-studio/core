#include "ui/common/TrackNavigationStrip.hpp"

#include <algorithm>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace style = oc::ui::lvgl::style;

namespace {

constexpr lv_coord_t STRIP_HEIGHT = 6;
constexpr lv_coord_t ITEM_MIN_SIZE = 10;
constexpr lv_coord_t ITEM_GAP = 2;
constexpr lv_opa_t ITEM_BASE_OPA = static_cast<lv_opa_t>(18);
constexpr lv_opa_t ITEM_ACTIVE_MIN_OPA = LV_OPA_70;
constexpr lv_opa_t ITEM_ACTIVITY_RANGE = static_cast<lv_opa_t>(35);
constexpr lv_coord_t CURSOR_HEIGHT = 2;
constexpr lv_coord_t CURSOR_OFFSET_Y = 1;

}  // namespace

TrackNavigationStrip::TrackNavigationStrip(lv_obj_t* parent) {
    createUI(parent);
}

TrackNavigationStrip::~TrackNavigationStrip() {
    if (container_) {
        lv_obj_delete(container_);
    }
}

FLASHMEM void TrackNavigationStrip::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_)
        .size(LV_PCT(100), STRIP_HEIGHT)
        .transparent()
        .noBorder()
        .pad(0)
        .noScroll();

    items_row_ = lv_obj_create(container_);
    style::apply(items_row_)
        .size(LV_PCT(100), STRIP_HEIGHT)
        .transparent()
        .noBorder()
        .pad(0)
        .noScroll()
        .flexRow(LV_FLEX_ALIGN_START, ITEM_GAP);

    for (uint8_t i = 0; i < items_.size(); ++i) {
        items_[i] = lv_obj_create(items_row_);
        style::apply(items_[i])
            .size(0, STRIP_HEIGHT)
            .noBorder()
            .pad(0)
            .noScroll();
        lv_obj_set_flex_grow(items_[i], 1);
        lv_obj_set_style_min_width(items_[i], ITEM_MIN_SIZE, 0);
        lv_obj_set_style_radius(items_[i], 1, 0);
        lv_obj_add_flag(items_[i], LV_OBJ_FLAG_OVERFLOW_VISIBLE);

        item_add_labels_[i] = lv_label_create(items_[i]);
        lv_label_set_text(item_add_labels_[i], "+");
        lv_obj_set_style_text_font(item_add_labels_[i], fonts.inter_13_bold, 0);
        lv_obj_set_style_text_color(item_add_labels_[i], lv_color_hex(theme::color::TEXT_PRIMARY), 0);
        lv_obj_set_style_text_opa(item_add_labels_[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(item_add_labels_[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_add_flag(item_add_labels_[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(item_add_labels_[i]);
    }

    selection_cursor_ = lv_obj_create(items_row_);
    lv_obj_remove_style_all(selection_cursor_);
    lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(selection_cursor_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(selection_cursor_, 1, 0);
    lv_obj_set_style_border_width(selection_cursor_, 0, 0);
    lv_obj_set_style_bg_color(selection_cursor_, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_opa(selection_cursor_, LV_OPA_COVER, 0);
    lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
}

void TrackNavigationStrip::render(const TrackNavigationStripProps& props) {
    if (!container_ || !items_row_) return;

    lv_obj_update_layout(items_row_);

    for (uint8_t i = 0; i < items_.size(); ++i) {
        const bool enabled = (props.enabledMask & static_cast<uint16_t>(1U << i)) != 0;
        const bool addSlot = props.addTrackIndex == i && !enabled;
        const bool isActive = props.activeTrack == i;
        const bool isPreview = props.previewTrack == i;
        const bool focusedCursor = (props.focusingTrack || props.selectingTrack) && isPreview;
        const bool selected = (props.selectedMask & static_cast<uint16_t>(1U << i)) != 0;
        const lv_color_t baseColor = lv_color_hex(enabled ? theme::color::trackColor(i)
                                                          : theme::color::INACTIVE);
        lv_color_t fillColor = baseColor;
        lv_opa_t fillOpa = static_cast<lv_opa_t>(
            ITEM_BASE_OPA +
            (static_cast<uint16_t>(props.activity[i]) * static_cast<uint16_t>(ITEM_ACTIVITY_RANGE) / 127U)
        );
        if (isActive) {
            fillOpa = static_cast<lv_opa_t>(std::max<uint16_t>(fillOpa, ITEM_ACTIVE_MIN_OPA));
            fillColor = lv_color_lighten(fillColor, LV_OPA_20);
        }

        lv_obj_set_style_bg_color(items_[i], fillColor, 0);

        if (addSlot) {
            lv_obj_set_style_bg_opa(items_[i], focusedCursor ? LV_OPA_20 : static_cast<lv_opa_t>(6), 0);
            lv_obj_set_style_border_width(items_[i], 0, 0);
            if (focusedCursor) {
                lv_obj_clear_flag(item_add_labels_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_center(item_add_labels_[i]);
            } else {
                lv_obj_add_flag(item_add_labels_[i], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_add_flag(item_add_labels_[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_opa(items_[i], fillOpa, 0);
            if (selected) {
                lv_obj_set_style_border_width(items_[i], 1, 0);
                lv_obj_set_style_border_color(items_[i], lv_color_hex(theme::color::TEXT_PRIMARY), 0);
                lv_obj_set_style_border_opa(items_[i], LV_OPA_70, 0);
            } else {
                lv_obj_set_style_border_width(items_[i], 0, 0);
            }
        }

        if (!addSlot && !selected) {
            lv_obj_set_style_border_width(items_[i], 0, 0);
        }
    }

    const bool showCursor = (props.focusingTrack || props.selectingTrack) &&
                            props.previewTrack < items_.size();
    if (!showCursor || !selection_cursor_) {
        if (selection_cursor_) {
            lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    lv_obj_t* cursorTarget = items_[props.previewTrack];
    if (!cursorTarget || lv_obj_has_flag(cursorTarget, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_update_layout(items_row_);
    const lv_coord_t itemX = lv_obj_get_x(cursorTarget);
    const lv_coord_t itemY = lv_obj_get_y(cursorTarget);
    const lv_coord_t itemW = lv_obj_get_width(cursorTarget);
    const lv_coord_t itemH = lv_obj_get_height(cursorTarget);
    const lv_coord_t cursorWidth = std::max<lv_coord_t>(1, itemW - 2);
    const lv_coord_t cursorX = static_cast<lv_coord_t>(itemX + (itemW - cursorWidth) / 2);
    const lv_coord_t cursorY = static_cast<lv_coord_t>(itemY + itemH + CURSOR_OFFSET_Y);

    lv_obj_clear_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(selection_cursor_, cursorX, cursorY);
    lv_obj_set_size(selection_cursor_, cursorWidth, CURSOR_HEIGHT);
    lv_obj_set_style_bg_color(selection_cursor_, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
    lv_obj_set_style_bg_opa(
        selection_cursor_,
        props.selectingTrack ? LV_OPA_COVER : LV_OPA_80,
        0
    );
}

}  // namespace core::ui

#include "ui/common/TrackNavigationStrip.hpp"

#include <algorithm>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace style = oc::ui::lvgl::style;
namespace add_slot_icon_ns = core::ui::add_slot_icon;

namespace {

constexpr lv_coord_t STRIP_HEIGHT = 12;
constexpr lv_coord_t ITEM_MIN_SIZE = 11;
constexpr lv_coord_t ITEM_GAP = 2;
constexpr lv_opa_t ITEM_BASE_OPA = static_cast<lv_opa_t>(34);
constexpr lv_opa_t ITEM_ACTIVE_MIN_OPA = LV_OPA_80;
constexpr lv_opa_t ITEM_ACTIVITY_RANGE = static_cast<lv_opa_t>(42);
constexpr lv_coord_t ACTIVE_CURSOR_HEIGHT = 2;
constexpr lv_coord_t CURRENT_CURSOR_WIDTH = 2;
constexpr lv_opa_t CURRENT_CURSOR_OPA = LV_OPA_COVER;
constexpr lv_coord_t OUTLINE_WIDTH = 1;
constexpr lv_opa_t OUTLINE_OPA_SELECTED = LV_OPA_70;

}  // namespace

TrackNavigationStrip::TrackNavigationStrip(lv_obj_t* parent) {
    createUI(parent);
}

TrackNavigationStrip::~TrackNavigationStrip() {
    if (container_) {
        lv_obj_delete(container_);
    }
}

void TrackNavigationStrip::refreshItemGeometryCache_() {
    if (!items_row_) return;

    for (uint8_t i = 0; i < items_.size(); ++i) {
        item_x_cache_[i] = lv_obj_get_x(items_[i]);
        item_y_cache_[i] = lv_obj_get_y(items_[i]);
        item_width_cache_[i] = lv_obj_get_width(items_[i]);
        item_height_cache_[i] = lv_obj_get_height(items_[i]);
    }
    item_geometry_cache_initialized_ = true;
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
        lv_obj_set_style_outline_pad(items_[i], 0, 0);

        item_add_icons_[i] =
            add_slot_icon_ns::createCentered(items_[i], theme::color::TEXT_PRIMARY);
    }

    active_cursor_ = lv_obj_create(items_row_);
    lv_obj_remove_style_all(active_cursor_);
    lv_obj_add_flag(active_cursor_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(active_cursor_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(active_cursor_, 1, 0);
    lv_obj_set_style_border_width(active_cursor_, 0, 0);
    lv_obj_set_style_bg_color(active_cursor_, lv_color_hex(theme::color::MACRO_1), 0);
    lv_obj_set_style_bg_opa(active_cursor_, LV_OPA_COVER, 0);
    lv_obj_add_flag(active_cursor_, LV_OBJ_FLAG_HIDDEN);

    current_cursor_ = lv_obj_create(items_row_);
    lv_obj_remove_style_all(current_cursor_);
    lv_obj_add_flag(current_cursor_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(current_cursor_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(current_cursor_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(current_cursor_, CURRENT_CURSOR_WIDTH, 0);
    lv_obj_set_style_border_color(current_cursor_, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
    lv_obj_set_style_border_opa(current_cursor_, CURRENT_CURSOR_OPA, 0);
    lv_obj_set_style_radius(current_cursor_, 1, 0);
    lv_obj_add_flag(current_cursor_, LV_OBJ_FLAG_HIDDEN);
}

void TrackNavigationStrip::render(const TrackNavigationStripProps& props) {
    if (!container_ || !items_row_) return;

    lv_coord_t rowWidth = lv_obj_get_width(items_row_);
    if (rowWidth <= 0 || rowWidth != cached_row_width_) {
        lv_obj_update_layout(items_row_);
        rowWidth = lv_obj_get_width(items_row_);
        if (rowWidth != cached_row_width_) {
            cached_row_width_ = rowWidth;
            for (auto& cache : item_cache_) {
                cache.initialized = false;
            }
            item_geometry_cache_initialized_ = false;
        }
    }
    if (!item_geometry_cache_initialized_) {
        refreshItemGeometryCache_();
    }

    for (uint8_t i = 0; i < items_.size(); ++i) {
        auto& cache = item_cache_[i];
        const bool enabled = (props.enabledMask & static_cast<uint16_t>(1U << i)) != 0;
        const bool addSlot = props.addTrackIndex == i && !enabled;
        const bool isActive = props.activeTrack == i;
        const bool isPreview = props.previewTrack == i;
        const bool focusedCursor = (props.focusingTrack || props.selectingTrack) && isPreview;
        const bool selected = (props.selectedMask & static_cast<uint16_t>(1U << i)) != 0;
        const lv_coord_t width = item_width_cache_[i];
        if (!cache.initialized || cache.width != width) {
            cache.width = width;
            cache.bgColor = 0;
            cache.bgOpa = LV_OPA_TRANSP;
            cache.addVisible = false;
            cache.outlineWidth = -1;
            cache.outlineOpa = LV_OPA_TRANSP;
        }
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

        const uint32_t fillColorHex = lv_color_to_int(fillColor);
        if (!cache.initialized || cache.bgColor != fillColorHex) {
            lv_obj_set_style_bg_color(items_[i], fillColor, 0);
            cache.bgColor = fillColorHex;
        }

        const lv_opa_t bgOpa = addSlot
            ? (focusedCursor ? LV_OPA_30 : static_cast<lv_opa_t>(10))
            : fillOpa;
        if (!cache.initialized || cache.bgOpa != bgOpa) {
            lv_obj_set_style_bg_opa(items_[i], bgOpa, 0);
            cache.bgOpa = bgOpa;
        }
        if (addSlot) {
            if (focusedCursor && (!cache.initialized || !cache.addVisible)) {
                add_slot_icon_ns::setVisible(item_add_icons_[i], true);
                cache.addVisible = true;
            } else if (!focusedCursor && (!cache.initialized || cache.addVisible)) {
                add_slot_icon_ns::setVisible(item_add_icons_[i], false);
                cache.addVisible = false;
            }
        } else if (!cache.initialized || cache.addVisible) {
            add_slot_icon_ns::setVisible(item_add_icons_[i], false);
            cache.addVisible = false;
        }

        const lv_coord_t outlineWidth = selected ? OUTLINE_WIDTH : 0;
        if (!cache.initialized || cache.outlineWidth != outlineWidth) {
            lv_obj_set_style_outline_width(items_[i], outlineWidth, 0);
            cache.outlineWidth = outlineWidth;
        }
        if (selected) {
            if (!cache.initialized || cache.outlineOpa != OUTLINE_OPA_SELECTED) {
                lv_obj_set_style_outline_color(items_[i], lv_color_hex(theme::color::TEXT_PRIMARY), 0);
                lv_obj_set_style_outline_opa(items_[i], OUTLINE_OPA_SELECTED, 0);
                cache.outlineOpa = OUTLINE_OPA_SELECTED;
            }
        } else {
            if (!cache.initialized || cache.outlineOpa != LV_OPA_TRANSP) {
                lv_obj_set_style_outline_opa(items_[i], LV_OPA_TRANSP, 0);
                cache.outlineOpa = LV_OPA_TRANSP;
            }
        }

        cache.initialized = true;
    }

    if (active_cursor_) {
        const bool showActiveCursor =
            props.activeTrack < items_.size() &&
            (props.enabledMask & static_cast<uint16_t>(1U << props.activeTrack)) != 0;
        if (!showActiveCursor) {
            if (active_cursor_visible_cache_) {
                lv_obj_add_flag(active_cursor_, LV_OBJ_FLAG_HIDDEN);
                active_cursor_visible_cache_ = false;
            }
        } else {
            const lv_coord_t itemX = item_x_cache_[props.activeTrack];
            const lv_coord_t itemY = item_y_cache_[props.activeTrack];
            const lv_coord_t itemW = item_width_cache_[props.activeTrack];
            const lv_coord_t itemH = item_height_cache_[props.activeTrack];
            const lv_coord_t cursorWidth = std::max<lv_coord_t>(1, itemW - 2);
            const lv_coord_t cursorX = static_cast<lv_coord_t>(itemX + (itemW - cursorWidth) / 2);
            const lv_coord_t cursorY = static_cast<lv_coord_t>(itemY + itemH - ACTIVE_CURSOR_HEIGHT);

            if (!active_cursor_visible_cache_) {
                lv_obj_clear_flag(active_cursor_, LV_OBJ_FLAG_HIDDEN);
                active_cursor_visible_cache_ = true;
            }
            if (active_cursor_x_cache_ != cursorX || active_cursor_y_cache_ != cursorY) {
                lv_obj_set_pos(active_cursor_, cursorX, cursorY);
                active_cursor_x_cache_ = cursorX;
                active_cursor_y_cache_ = cursorY;
            }
            if (active_cursor_width_cache_ != cursorWidth) {
                lv_obj_set_size(active_cursor_, cursorWidth, ACTIVE_CURSOR_HEIGHT);
                active_cursor_width_cache_ = cursorWidth;
            }
        }
    }

    const bool showCursor = (props.focusingTrack || props.selectingTrack) &&
                            props.previewTrack < items_.size();
    if (!showCursor || !current_cursor_) {
        if (current_cursor_ && current_cursor_visible_cache_) {
            lv_obj_add_flag(current_cursor_, LV_OBJ_FLAG_HIDDEN);
            current_cursor_visible_cache_ = false;
        }
        return;
    }

    lv_obj_t* cursorTarget = items_[props.previewTrack];
    if (!cursorTarget || lv_obj_has_flag(cursorTarget, LV_OBJ_FLAG_HIDDEN)) {
        if (current_cursor_visible_cache_) {
            lv_obj_add_flag(current_cursor_, LV_OBJ_FLAG_HIDDEN);
            current_cursor_visible_cache_ = false;
        }
        return;
    }

    const lv_coord_t itemX = item_x_cache_[props.previewTrack];
    const lv_coord_t itemY = item_y_cache_[props.previewTrack];
    const lv_coord_t itemW = item_width_cache_[props.previewTrack];
    const lv_coord_t itemH = item_height_cache_[props.previewTrack];
    const lv_opa_t cursorOpa = props.selectingTrack ? LV_OPA_COVER : CURRENT_CURSOR_OPA;

    if (!current_cursor_visible_cache_) {
        lv_obj_clear_flag(current_cursor_, LV_OBJ_FLAG_HIDDEN);
        current_cursor_visible_cache_ = true;
    }
    if (current_cursor_x_cache_ != itemX || current_cursor_y_cache_ != itemY) {
        lv_obj_set_pos(current_cursor_, itemX, itemY);
        current_cursor_x_cache_ = itemX;
        current_cursor_y_cache_ = itemY;
    }
    if (current_cursor_width_cache_ != itemW || current_cursor_height_cache_ != itemH) {
        lv_obj_set_size(current_cursor_, itemW, itemH);
        current_cursor_width_cache_ = itemW;
        current_cursor_height_cache_ = itemH;
    }
    if (current_cursor_opa_cache_ != cursorOpa) {
        lv_obj_set_style_border_opa(current_cursor_, cursorOpa, 0);
        current_cursor_opa_cache_ = cursorOpa;
    }
}

}  // namespace core::ui

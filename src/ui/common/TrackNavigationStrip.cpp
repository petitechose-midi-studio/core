#include "ui/common/TrackNavigationStrip.hpp"

#include <algorithm>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>

#include "ui/interaction/StructureSelectionVisual.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace style = oc::ui::lvgl::style;
namespace selection_visual = core::ui::interaction;
namespace add_slot_icon_ns = core::ui::add_slot_icon;

namespace {

constexpr lv_coord_t STRIP_HEIGHT = 6;
constexpr lv_coord_t ITEM_MIN_SIZE = 11;
constexpr lv_coord_t ITEM_GAP = 2;
constexpr lv_opa_t ITEM_BASE_OPA = static_cast<lv_opa_t>(34);
constexpr lv_opa_t ITEM_MUTED_OPA = static_cast<lv_opa_t>(18);
constexpr lv_opa_t ITEM_SOLO_EXCLUDED_OPA = static_cast<lv_opa_t>(13);
constexpr lv_opa_t ITEM_INAUDIBLE_ACTIVE_MIN_OPA = LV_OPA_20;
constexpr lv_opa_t ITEM_ACTIVE_MIN_OPA = LV_OPA_80;
constexpr lv_opa_t ITEM_ACTIVITY_RANGE = static_cast<lv_opa_t>(42);
constexpr lv_coord_t ACTIVE_CURSOR_HEIGHT = 1;
constexpr lv_coord_t CURRENT_CURSOR_WIDTH = 1;
constexpr lv_opa_t CURRENT_CURSOR_OPA = LV_OPA_COVER;
constexpr lv_coord_t SOLO_BORDER_WIDTH = 1;
constexpr lv_opa_t SOLO_BORDER_OPA = LV_OPA_COVER;
constexpr lv_coord_t SELECTION_OUTLINE_WIDTH = 1;
constexpr lv_opa_t SELECTION_OUTLINE_OPA = LV_OPA_70;
constexpr lv_coord_t DESTINATION_MARKER_HEIGHT = 1;

}  // namespace

FLASHMEM TrackNavigationStrip::TrackNavigationStrip(lv_obj_t* parent) {
    createUI(parent);
}

FLASHMEM TrackNavigationStrip::~TrackNavigationStrip() {
    if (container_) {
        lv_obj_delete(container_);
    }
}

FLASHMEM void TrackNavigationStrip::refreshItemGeometryCache_() {
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

        destination_markers_[i] = lv_obj_create(items_[i]);
        lv_obj_remove_style_all(destination_markers_[i]);
        lv_obj_set_size(
            destination_markers_[i],
            LV_PCT(100),
            DESTINATION_MARKER_HEIGHT
        );
        lv_obj_align(
            destination_markers_[i],
            LV_ALIGN_TOP_MID,
            0,
            0
        );
        lv_obj_set_style_radius(destination_markers_[i], 1, 0);
        lv_obj_set_style_bg_opa(
            destination_markers_[i],
            LV_OPA_COVER,
            0
        );
        lv_obj_add_flag(
            destination_markers_[i],
            LV_OBJ_FLAG_HIDDEN
        );
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

FLASHMEM void TrackNavigationStrip::render(const TrackNavigationStripProps& props) {
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
        const uint16_t trackBit = static_cast<uint16_t>(1U << i);
        const bool enabled = (props.enabledMask & trackBit) != 0;
        const bool explicitlyMuted =
            enabled && (props.explicitMutedMask & trackBit) != 0;
        const bool soloed = enabled && (props.soloMask & trackBit) != 0;
        const bool inaudible = enabled && (props.inaudibleMask & trackBit) != 0;
        const bool addSlot = props.addTrackIndex == i && !enabled;
        const bool isActive = props.activeTrack == i;
        const bool isPreview = props.previewTrack == i;
        const bool focusedCursor =
            (props.focusingTrack || props.selectingTrack) && isPreview;
        const bool selected =
            (props.selectedMask & trackBit) != 0U;
        const lv_coord_t width = item_width_cache_[i];
        if (!cache.initialized || cache.width != width) {
            cache.width = width;
            cache.bgColor = 0;
            cache.bgOpa = LV_OPA_TRANSP;
            cache.addVisible = false;
            cache.borderWidth = -1;
            cache.borderOpa = LV_OPA_TRANSP;
            cache.outlineWidth = -1;
            cache.outlineOpa = LV_OPA_TRANSP;
            cache.destinationVisible = false;
            cache.destinationColor = 0;
        }
        const lv_color_t baseColor = lv_color_hex(enabled ? theme::color::trackColor(i)
                                                          : theme::color::INACTIVE);
        // Mute is authored and therefore darkened. A Track excluded only by
        // another Track's Solo remains chromatically identifiable but dim.
        lv_color_t fillColor = explicitlyMuted
            ? lv_color_darken(baseColor, LV_OPA_50)
            : baseColor;
        lv_opa_t fillOpa = static_cast<lv_opa_t>(
            ITEM_BASE_OPA +
            (static_cast<uint16_t>(props.activity[i]) * static_cast<uint16_t>(ITEM_ACTIVITY_RANGE) / 127U)
        );
        if (explicitlyMuted) {
            fillOpa = ITEM_MUTED_OPA;
        } else if (inaudible) {
            fillOpa = ITEM_SOLO_EXCLUDED_OPA;
        }
        if (isActive) {
            const lv_opa_t minOpa = inaudible
                ? ITEM_INAUDIBLE_ACTIVE_MIN_OPA
                : ITEM_ACTIVE_MIN_OPA;
            fillOpa = static_cast<lv_opa_t>(std::max<uint16_t>(fillOpa, minOpa));
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

        const lv_coord_t borderWidth = soloed ? SOLO_BORDER_WIDTH : 0;
        if (!cache.initialized || cache.borderWidth != borderWidth) {
            lv_obj_set_style_border_width(items_[i], borderWidth, 0);
            cache.borderWidth = borderWidth;
        }
        const lv_opa_t borderOpa = soloed ? SOLO_BORDER_OPA : LV_OPA_TRANSP;
        if (!cache.initialized || cache.borderOpa != borderOpa) {
            if (soloed) {
                lv_obj_set_style_border_color(
                    items_[i], lv_color_hex(theme::color::MACRO_3), 0
                );
            }
            lv_obj_set_style_border_opa(items_[i], borderOpa, 0);
            cache.borderOpa = borderOpa;
        }

        const lv_coord_t outlineWidth =
            selected ? SELECTION_OUTLINE_WIDTH : 0;
        if (!cache.initialized || cache.outlineWidth != outlineWidth) {
            lv_obj_set_style_outline_width(items_[i], outlineWidth, 0);
            cache.outlineWidth = outlineWidth;
        }
        const lv_opa_t outlineOpa =
            selected ? SELECTION_OUTLINE_OPA : LV_OPA_TRANSP;
        if (!cache.initialized || cache.outlineOpa != outlineOpa) {
            if (selected) {
                lv_obj_set_style_outline_color(
                    items_[i],
                    lv_color_hex(selection_visual::structureSelectionColor(
                        selection_visual::StructureSelectionVisualRole::SELECTED
                    )),
                    0
                );
            }
            lv_obj_set_style_outline_opa(items_[i], outlineOpa, 0);
            cache.outlineOpa = outlineOpa;
        }

        const bool destination =
            (props.destinationPreviewMask & trackBit) != 0U;
        const bool destinationOverwrite =
            (props.destinationOverwriteMask & trackBit) != 0U;
        const bool destinationBlocked =
            (props.destinationBlockedMask & trackBit) != 0U;
        if (!cache.initialized ||
            cache.destinationVisible != destination) {
            if (destination) {
                lv_obj_clear_flag(
                    destination_markers_[i],
                    LV_OBJ_FLAG_HIDDEN
                );
            } else {
                lv_obj_add_flag(
                    destination_markers_[i],
                    LV_OBJ_FLAG_HIDDEN
                );
            }
            cache.destinationVisible = destination;
        }
        if (destination) {
            const uint32_t destinationColor = destinationBlocked
                ? selection_visual::structureSelectionColor(
                      selection_visual::StructureSelectionVisualRole::DESTINATION_BLOCKED
                  )
                : (destinationOverwrite
                    ? selection_visual::structureSelectionColor(
                          selection_visual::StructureSelectionVisualRole::DESTINATION_OVERWRITE
                      )
                    : selection_visual::structureSelectionColor(
                          selection_visual::StructureSelectionVisualRole::DESTINATION_FREE
                      ));
            if (!cache.initialized ||
                cache.destinationColor != destinationColor) {
                lv_obj_set_style_bg_color(
                    destination_markers_[i],
                    lv_color_hex(destinationColor),
                    0
                );
                cache.destinationColor = destinationColor;
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
    const lv_opa_t cursorOpa = CURRENT_CURSOR_OPA;

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

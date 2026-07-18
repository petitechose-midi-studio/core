#include "ui/sequencer/SequencerStructureWorkspace.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer {
namespace theme = standalone::theme;

FLASHMEM SequencerStructureWorkspace::SequencerStructureWorkspace(lv_obj_t* parent) {
    createUi(parent);
}

FLASHMEM SequencerStructureWorkspace::~SequencerStructureWorkspace() {
    if (container_) lv_obj_delete(container_);
    container_ = nullptr;
}

FLASHMEM void SequencerStructureWorkspace::createUi(lv_obj_t* parent) {
    if (!parent) return;
    container_ = lv_obj_create(parent);
    if (!container_) return;
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_top(container_, 4, 0);
    lv_obj_set_style_pad_row(container_, 2, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    breadcrumb_ = lv_label_create(container_);
    context_ = lv_label_create(container_);
    grid_ = lv_obj_create(container_);
    if (!breadcrumb_ || !context_ || !grid_) return;

    lv_obj_set_width(breadcrumb_, LV_PCT(100));
    lv_obj_set_style_text_align(breadcrumb_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(breadcrumb_, fonts.inter_14_semibold, 0);
    lv_obj_set_style_text_color(
        breadcrumb_,
        lv_color_hex(theme::color::TEXT_PRIMARY),
        0
    );
    lv_label_set_long_mode(breadcrumb_, LV_LABEL_LONG_CLIP);

    lv_obj_set_width(context_, LV_PCT(100));
    lv_obj_set_style_text_align(context_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(context_, fonts.inter_12_medium, 0);
    lv_obj_set_style_text_color(
        context_,
        lv_color_hex(theme::color::TEXT_SECONDARY),
        0
    );
    lv_label_set_long_mode(context_, LV_LABEL_LONG_CLIP);

    lv_obj_remove_style_all(grid_);
    lv_obj_set_size(grid_, 224, 126);
    lv_obj_set_layout(grid_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(
        grid_,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );
    lv_obj_set_style_pad_row(grid_, 3, 0);
    lv_obj_set_style_pad_column(grid_, 4, 0);
    lv_obj_clear_flag(grid_, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0; i < cells_.size(); ++i) {
        auto* cell = lv_obj_create(grid_);
        auto* label = cell ? lv_label_create(cell) : nullptr;
        auto* marker = cell ? lv_obj_create(cell) : nullptr;
        cells_[i] = cell;
        labels_[i] = label;
        markers_[i] = marker;
        if (!cell || !label || !marker) continue;

        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, 50, 28);
        lv_obj_set_style_radius(cell, 4, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_border_color(
            cell,
            lv_color_hex(theme::color::INACTIVE),
            0
        );
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_center(label);
        lv_obj_set_style_text_font(label, fonts.inter_12_medium, 0);
        lv_obj_set_style_text_color(
            label,
            lv_color_hex(theme::color::TEXT_SECONDARY),
            0
        );
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);

        lv_obj_remove_style_all(marker);
        lv_obj_set_size(marker, 4, 4);
        lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(marker, lv_color_hex(theme::color::ACTIVE), 0);
        lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
        lv_obj_align(marker, LV_ALIGN_TOP_RIGHT, -3, 3);
        lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
    }
}

FLASHMEM void SequencerStructureWorkspace::render(
    const SequencerStructureWorkspaceProps& props
) {
    if (!container_) return;
    if (!props.visible) {
        if (visible_cache_) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        visible_cache_ = false;
        return;
    }
    if (!visible_cache_) {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        visible_cache_ = true;
    }

    lv_label_set_text(breadcrumb_, props.breadcrumb.data());
    lv_label_set_text(context_, props.context.data());

    for (uint8_t i = 0; i < props.items.size(); ++i) {
        auto* cell = cells_[i];
        auto* label = labels_[i];
        auto* marker = markers_[i];
        if (!cell || !label || !marker) continue;
        const auto& item = props.items[i];
        if (!item.visible) {
            lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(label, item.label.data());

        const uint32_t tone = item.add
            ? theme::color::MACRO_4
            : item.focused ? theme::color::ACTIVE
                           : theme::color::INACTIVE;
        lv_obj_set_style_border_color(cell, lv_color_hex(tone), 0);
        lv_obj_set_style_border_width(cell, item.focused ? 2 : 1, 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(tone), 0);
        lv_obj_set_style_bg_opa(
            cell,
            item.focused ? LV_OPA_20 : (item.active ? LV_OPA_10 : LV_OPA_TRANSP),
            0
        );
        lv_obj_set_style_text_color(
            label,
            lv_color_hex(item.focused || item.add
                ? tone
                : theme::color::TEXT_SECONDARY),
            0
        );
        if (item.active && !item.add) {
            lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

}  // namespace core::ui::sequencer

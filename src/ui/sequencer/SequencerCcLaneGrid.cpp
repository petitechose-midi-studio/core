#include "ui/sequencer/SequencerCcLaneGrid.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {
namespace {

namespace theme = standalone::theme;

constexpr lv_coord_t GRID_X = 8;
constexpr lv_coord_t GRID_Y = 34;
constexpr lv_coord_t GRID_WIDTH = 304;
constexpr lv_coord_t GRID_HEIGHT = 130;
constexpr lv_coord_t CELL_GAP = 3;
constexpr lv_coord_t PLOT_X = 4;
constexpr lv_coord_t PLOT_Y = 23;
constexpr lv_coord_t PLOT_WIDTH = 27;
constexpr lv_coord_t PLOT_HEIGHT = 72;

template <size_t N>
bool copyText(std::array<char, N>& destination, const char* source) {
    const char* text = source ? source : "";
    if (std::strncmp(destination.data(), text, N) == 0) return false;
    std::strncpy(destination.data(), text, N - 1U);
    destination[N - 1U] = '\0';
    return true;
}

lv_obj_t* createLabel(
    lv_obj_t* parent,
    const lv_font_t* font,
    uint32_t color,
    lv_text_align_t align
) {
    auto* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font ? font : LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

}  // namespace

FLASHMEM SequencerCcLaneGrid::SequencerCcLaneGrid(lv_obj_t* parent) {
    createUi(parent);
}

FLASHMEM SequencerCcLaneGrid::~SequencerCcLaneGrid() {
    if (root_) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

FLASHMEM void SequencerCcLaneGrid::createUi(lv_obj_t* parent) {
    if (!parent) return;

    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(theme::color::BACKGROUND), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    title_ = createLabel(
        root_,
        fonts.inter_14_semibold,
        theme::color::TEXT_PRIMARY,
        LV_TEXT_ALIGN_LEFT
    );
    lv_obj_set_pos(title_, 10, 8);
    lv_obj_set_size(title_, 140, 18);

    meta_ = createLabel(
        root_,
        fonts.inter_12_medium,
        theme::color::TEXT_SECONDARY,
        LV_TEXT_ALIGN_RIGHT
    );
    lv_obj_set_pos(meta_, 150, 9);
    lv_obj_set_size(meta_, 160, 16);

    grid_ = lv_obj_create(root_);
    lv_obj_remove_style_all(grid_);
    lv_obj_set_pos(grid_, GRID_X, GRID_Y);
    lv_obj_set_size(grid_, GRID_WIDTH, GRID_HEIGHT);
    lv_obj_set_layout(grid_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        grid_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(grid_, CELL_GAP, 0);
    lv_obj_clear_flag(grid_, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t index = 0; index < cells_.size(); ++index) {
        createCell(index);
    }

    hint_ = createLabel(
        root_,
        fonts.inter_12_medium,
        theme::color::TEXT_SECONDARY,
        LV_TEXT_ALIGN_CENTER
    );
    lv_obj_set_style_text_opa(hint_, LV_OPA_80, 0);
    lv_obj_set_pos(hint_, 8, 174);
    lv_obj_set_size(hint_, 304, 16);
}

FLASHMEM void SequencerCcLaneGrid::createCell(size_t index) {
    if (!grid_ || index >= cells_.size()) return;
    auto& widgets = cells_[index];

    widgets.root = lv_obj_create(grid_);
    lv_obj_remove_style_all(widgets.root);
    lv_obj_set_width(widgets.root, 0);
    lv_obj_set_height(widgets.root, LV_PCT(100));
    lv_obj_set_flex_grow(widgets.root, 1);
    lv_obj_set_style_bg_color(
        widgets.root,
        lv_color_hex(theme::color::MACRO_CC_COLOR),
        0
    );
    lv_obj_set_style_bg_opa(widgets.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(widgets.root, 1, 0);
    lv_obj_set_style_border_color(
        widgets.root,
        lv_color_hex(theme::color::MACRO_CC_COLOR),
        0
    );
    lv_obj_set_style_border_opa(widgets.root, LV_OPA_20, 0);
    lv_obj_set_style_radius(widgets.root, 3, 0);
    lv_obj_clear_flag(widgets.root, LV_OBJ_FLAG_SCROLLABLE);

    widgets.playhead = lv_obj_create(widgets.root);
    lv_obj_remove_style_all(widgets.playhead);
    lv_obj_set_pos(widgets.playhead, 2, 0);
    lv_obj_set_size(widgets.playhead, 29, 2);
    lv_obj_set_style_bg_color(
        widgets.playhead,
        lv_color_hex(theme::color::PLAY_ACTIVE),
        0
    );
    lv_obj_set_style_bg_opa(widgets.playhead, LV_OPA_COVER, 0);
    lv_obj_add_flag(widgets.playhead, LV_OBJ_FLAG_HIDDEN);

    widgets.step = createLabel(
        widgets.root,
        fonts.inter_12_medium,
        theme::color::TEXT_SECONDARY,
        LV_TEXT_ALIGN_CENTER
    );
    lv_obj_set_pos(widgets.step, 0, 5);
    lv_obj_set_size(widgets.step, LV_PCT(100), 15);

    widgets.plot = lv_obj_create(widgets.root);
    lv_obj_remove_style_all(widgets.plot);
    lv_obj_set_pos(widgets.plot, PLOT_X, PLOT_Y);
    lv_obj_set_size(widgets.plot, PLOT_WIDTH, PLOT_HEIGHT);
    lv_obj_set_style_bg_color(
        widgets.plot,
        lv_color_hex(theme::color::TEXT_SECONDARY),
        0
    );
    lv_obj_set_style_bg_opa(widgets.plot, LV_OPA_10, 0);
    lv_obj_set_style_radius(widgets.plot, 2, 0);
    lv_obj_clear_flag(widgets.plot, LV_OBJ_FLAG_SCROLLABLE);

    widgets.fill = lv_obj_create(widgets.plot);
    lv_obj_remove_style_all(widgets.fill);
    lv_obj_set_width(widgets.fill, LV_PCT(100));
    lv_obj_set_height(widgets.fill, 2);
    lv_obj_align(widgets.fill, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(
        widgets.fill,
        lv_color_hex(theme::color::MACRO_CC_COLOR),
        0
    );
    lv_obj_set_style_bg_opa(widgets.fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(widgets.fill, 2, 0);
    lv_obj_add_flag(widgets.fill, LV_OBJ_FLAG_HIDDEN);

    widgets.value = createLabel(
        widgets.root,
        fonts.inter_12_medium,
        theme::color::TEXT_PRIMARY,
        LV_TEXT_ALIGN_CENTER
    );
    lv_obj_set_pos(widgets.value, 0, 101);
    lv_obj_set_size(widgets.value, LV_PCT(100), 16);
}

FLASHMEM void SequencerCcLaneGrid::renderCell(
    CellWidgets& widgets,
    const SequencerCcLaneGridCell& cell,
    uint32_t accentColor
) {
    if (!widgets.root) return;

    if (!widgets.rendered || widgets.visible != cell.visible) {
        lv_obj_set_style_opa(
            widgets.root,
            cell.visible ? LV_OPA_COVER : LV_OPA_20,
            0
        );
        widgets.visible = cell.visible;
    }

    char stepText[4] = {};
    char valueText[5] = {};
    if (cell.visible) {
        std::snprintf(
            stepText,
            sizeof(stepText),
            "%u",
            static_cast<unsigned>(cell.step + 1U)
        );
        if (cell.authored) {
            std::snprintf(
                valueText,
                sizeof(valueText),
                "%u",
                static_cast<unsigned>(cell.value)
            );
        } else {
            std::snprintf(valueText, sizeof(valueText), "--");
        }
    }
    if (copyText(widgets.stepText, stepText)) {
        lv_label_set_text_static(widgets.step, widgets.stepText.data());
    }
    if (copyText(widgets.valueText, valueText)) {
        lv_label_set_text_static(widgets.value, widgets.valueText.data());
    }

    if (!widgets.rendered || widgets.accentColor != accentColor) {
        const auto color = lv_color_hex(accentColor);
        lv_obj_set_style_bg_color(widgets.root, color, 0);
        lv_obj_set_style_border_color(widgets.root, color, 0);
        lv_obj_set_style_bg_color(widgets.fill, color, 0);
        widgets.accentColor = accentColor;
    }

    if (!widgets.rendered || widgets.focused != cell.focused) {
        lv_obj_set_style_border_opa(
            widgets.root,
            cell.focused ? LV_OPA_COVER : LV_OPA_20,
            0
        );
        lv_obj_set_style_bg_opa(
            widgets.root,
            cell.focused ? LV_OPA_10 : LV_OPA_TRANSP,
            0
        );
        widgets.focused = cell.focused;
    }

    if (!widgets.rendered || widgets.authored != cell.authored ||
        widgets.valueCache != cell.value) {
        if (cell.authored && cell.visible) {
            const lv_coord_t height = static_cast<lv_coord_t>(
                2 + (static_cast<uint16_t>(cell.value) * (PLOT_HEIGHT - 2)) / 127U
            );
            lv_obj_set_height(widgets.fill, height);
            lv_obj_align(widgets.fill, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_clear_flag(widgets.fill, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_opa(widgets.value, LV_OPA_COVER, 0);
        } else {
            lv_obj_add_flag(widgets.fill, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_opa(widgets.value, LV_OPA_60, 0);
        }
        widgets.authored = cell.authored;
        widgets.valueCache = cell.value;
    }

    if (!widgets.rendered || widgets.playheadVisible != cell.playhead) {
        if (cell.playhead && cell.visible) {
            lv_obj_clear_flag(widgets.playhead, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(widgets.playhead, LV_OBJ_FLAG_HIDDEN);
        }
        widgets.playheadVisible = cell.playhead;
    }

    widgets.rendered = true;
}

FLASHMEM void SequencerCcLaneGrid::render(
    const SequencerCcLaneGridProps& props
) {
    if (!root_) return;
    if (!props.visible) {
        if (visible_) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
            visible_ = false;
        }
        return;
    }

    if (!visible_) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
        visible_ = true;
    }

    if (copyText(titleText_, props.title)) {
        lv_label_set_text_static(title_, titleText_.data());
    }
    if (copyText(metaText_, props.meta)) {
        lv_label_set_text_static(meta_, metaText_.data());
    }
    if (copyText(hintText_, props.hint)) {
        lv_label_set_text_static(hint_, hintText_.data());
    }
    if (statusColor_ != props.statusColor) {
        lv_obj_set_style_text_color(meta_, lv_color_hex(props.statusColor), 0);
        statusColor_ = props.statusColor;
    }

    const uint32_t accent = props.accentColor == 0
        ? theme::color::MACRO_CC_COLOR
        : props.accentColor;
    for (size_t index = 0; index < cells_.size(); ++index) {
        renderCell(cells_[index], props.cells[index], accent);
    }
}

}  // namespace core::ui

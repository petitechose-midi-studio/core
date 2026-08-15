#include "ui/sequencer/StepGridWidgets.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <ms/ui/font/CoreFonts.hpp>

#include "config/PlatformCompat.hpp"
#include "ui/font/StandaloneIcons.hpp"

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace core::ui::sequencer::grid::widgets {

namespace {

constexpr uint32_t STEP_INLINE_NOTE_COLOR = theme::color::TEXT_PRIMARY;
constexpr lv_opa_t STEP_INLINE_NOTE_OPA = LV_OPA_COVER;
constexpr lv_coord_t HORIZONTAL_INSET = 2;
constexpr lv_coord_t OVERLAY_SAFE_TOP = 2;
constexpr lv_coord_t OVERLAY_SAFE_BOTTOM = 2;
constexpr lv_coord_t GRID_ROW_PAD = 2;

}  // namespace

FLASHMEM void createRoot(lv_obj_t* parent,
                         lv_obj_t*& container,
                         lv_obj_t*& grid,
                         lv_obj_t*& noteLayer,
                         lv_event_cb_t geometryEvent,
                         void* geometryUserData) {
    if (!parent) return;

    container = lv_obj_create(parent);
    style::apply(container).size(LV_PCT(100), LV_PCT(100)).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_flex_grow(container, 1);
    lv_obj_add_flag(container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    grid = lv_obj_create(container);
    style::apply(grid).size(LV_PCT(100), LV_PCT(100)).transparent().noBorder();

    static lv_coord_t col_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_top(grid, OVERLAY_SAFE_TOP, 0);
    lv_obj_set_style_pad_bottom(grid, OVERLAY_SAFE_BOTTOM, 0);
    lv_obj_set_style_pad_left(grid, HORIZONTAL_INSET, 0);
    lv_obj_set_style_pad_right(grid, HORIZONTAL_INSET, 0);
    lv_obj_set_style_pad_column(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, GRID_ROW_PAD, 0);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(grid, geometryEvent, LV_EVENT_SIZE_CHANGED, geometryUserData);
    lv_obj_add_event_cb(grid, geometryEvent, LV_EVENT_LAYOUT_CHANGED, geometryUserData);

    noteLayer = lv_obj_create(container);
    style::apply(noteLayer).size(LV_PCT(100), LV_PCT(100)).transparent().noBorder().pad(0).noScroll();
    lv_obj_add_flag(noteLayer, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(noteLayer, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_align(noteLayer, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_background(noteLayer);
    lv_obj_add_event_cb(noteLayer, geometryEvent, LV_EVENT_SIZE_CHANGED, geometryUserData);
}

FLASHMEM lv_coord_t noteLabelHeight() {
    return static_cast<lv_coord_t>(
        lv_font_get_line_height(fonts.compact_selected())
    );
}

FLASHMEM void createTile(uint8_t tileIndex,
                         lv_obj_t* grid,
                         lv_obj_t* noteLayer,
                         lv_obj_t*& tile,
                         lv_obj_t*& noteLabel,
                         lv_obj_t*& originalNoteLabel,
                         lv_obj_t*& stepInlineIcon,
                         lv_obj_t*& stepButton,
                         lv_coord_t& inlineIconWidth,
                         lv_coord_t& inlineIconHeight,
                         lv_event_cb_t geometryEvent,
                         void* geometryUserData) {
    const uint8_t col = tileIndex % 4;
    const uint8_t row = tileIndex / 4;

    tile = lv_obj_create(grid);
    style::apply(tile).transparent().noBorder().pad(0).noScroll();
    lv_obj_add_flag(tile, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_grid_cell(
        tile,
        LV_GRID_ALIGN_STRETCH, col, 1,
        LV_GRID_ALIGN_STRETCH, row, 1
    );

    lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(tile, 0, 0);

    lv_obj_t* buttonWrap = lv_obj_create(tile);
    lv_obj_remove_style_all(buttonWrap);
    lv_obj_clear_flag(buttonWrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(buttonWrap, LV_PCT(100));
    lv_obj_set_flex_grow(buttonWrap, 1);
    lv_obj_set_layout(buttonWrap, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(buttonWrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttonWrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(buttonWrap, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    stepButton = lv_obj_create(buttonWrap);
    lv_obj_clear_flag(stepButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(stepButton, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_width(stepButton, LV_PCT(100));
    lv_obj_set_height(stepButton, LV_PCT(100));
    lv_obj_set_flex_grow(stepButton, 1);
    lv_obj_set_style_radius(stepButton, 10, 0);
    lv_obj_set_style_border_width(stepButton, 1, 0);
    lv_obj_set_style_border_color(stepButton, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
    lv_obj_set_style_border_opa(stepButton, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(stepButton, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(stepButton, lv_color_hex(theme::color::INACTIVE), 0);
    lv_obj_set_style_pad_all(stepButton, 0, 0);
    lv_obj_add_event_cb(stepButton, geometryEvent, LV_EVENT_SIZE_CHANGED, geometryUserData);

    noteLabel = lv_label_create(noteLayer);
    lv_label_set_text(noteLabel, "");
    lv_obj_add_flag(noteLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(noteLabel, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(noteLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(noteLabel, fonts.compact_selected(), 0);
    lv_obj_set_style_text_color(noteLabel, lv_color_hex(STEP_INLINE_NOTE_COLOR), 0);
    lv_obj_set_style_text_opa(noteLabel, STEP_INLINE_NOTE_OPA, 0);
    lv_obj_set_style_pad_all(noteLabel, 0, 0);
    lv_obj_add_flag(noteLabel, LV_OBJ_FLAG_HIDDEN);

    originalNoteLabel = lv_label_create(noteLayer);
    lv_label_set_text(originalNoteLabel, "");
    lv_obj_add_flag(originalNoteLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(originalNoteLabel, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(originalNoteLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(originalNoteLabel, fonts.compact_selected(), 0);
    lv_obj_set_style_text_color(originalNoteLabel, lv_color_hex(theme::color::INACTIVE_LIGHTER), 0);
    lv_obj_set_style_text_opa(originalNoteLabel, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(originalNoteLabel, 0, 0);
    lv_obj_add_flag(originalNoteLabel, LV_OBJ_FLAG_HIDDEN);

    stepInlineIcon = lv_label_create(noteLayer);
    standalone::icons::set(
        stepInlineIcon,
        standalone::icons::NOTE_PROP_RANDOM,
        standalone::icons::Size::S
    );
    lv_obj_add_flag(stepInlineIcon, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(stepInlineIcon, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(stepInlineIcon, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(stepInlineIcon, lv_color_hex(STEP_INLINE_NOTE_COLOR), 0);
    lv_obj_set_style_text_opa(stepInlineIcon, STEP_INLINE_NOTE_OPA, 0);
    lv_obj_set_style_pad_all(stepInlineIcon, 0, 0);
    lv_obj_add_flag(stepInlineIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(stepInlineIcon);
    inlineIconWidth = lv_obj_get_width(stepInlineIcon);
    inlineIconHeight = lv_obj_get_height(stepInlineIcon);
}

}  // namespace core::ui::sequencer::grid::widgets

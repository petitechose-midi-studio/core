#include "ui/sequencer/StepGridWidgets.hpp"

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <ms/ui/font/CoreFonts.hpp>

#include "config/PlatformCompat.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepGridGeometryLogic.hpp"
#include "ui/sequencer/StepGridRenderLogic.hpp"

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace core::ui::sequencer::grid::widgets {

namespace {

constexpr lv_coord_t STEP_GUIDE_WIDTH = 1;
constexpr uint8_t STEP_GUIDE_COUNT = 3;
constexpr lv_coord_t STEP_SHAPE_RADIUS = 0;
constexpr lv_coord_t STEP_SHAPE_STROKE_WIDTH = 2;
constexpr lv_coord_t STEP_SHAPE_MIN_WIDTH = grid::STEP_SHAPE_MIN_WIDTH;
constexpr lv_coord_t STEP_SHAPE_MIN_HEIGHT = grid::STEP_SHAPE_MIN_HEIGHT;
constexpr lv_coord_t STEP_MARKER_SIZE = 6;
constexpr lv_coord_t STEP_SELECTION_DOT_SIZE = 6;
constexpr uint32_t COLOR_STEP_PLAY_HEX = 0x5CA8EE;
constexpr uint32_t COLOR_SELECTION_COPY_HEX = 0x59B7C9;
constexpr uint32_t STEP_INDEX_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_INDEX_OPA = LV_OPA_60;
constexpr uint32_t STEP_GUIDE_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_GUIDE_OPA = LV_OPA_50;
constexpr uint32_t STEP_INLINE_NOTE_COLOR = theme::color::TEXT_PRIMARY;
constexpr lv_opa_t STEP_INLINE_NOTE_OPA = LV_OPA_COVER;
constexpr lv_coord_t HORIZONTAL_INSET = 2;
constexpr lv_coord_t OVERLAY_SAFE_TOP = 2;
constexpr lv_coord_t OVERLAY_SAFE_BOTTOM = 2;
constexpr lv_coord_t GRID_INTERNAL_PAD = 2;

void initStepGuide(lv_obj_t* guide) {
    if (!guide) return;

    lv_obj_clear_flag(guide, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(guide, STEP_GUIDE_WIDTH, 8);
    lv_obj_set_style_radius(guide, 0, 0);
    lv_obj_set_style_border_width(guide, 0, 0);
    lv_obj_set_style_bg_color(guide, lv_color_hex(STEP_GUIDE_COLOR), 0);
    lv_obj_set_style_bg_opa(guide, STEP_GUIDE_OPA, 0);
}

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
    lv_obj_set_style_pad_column(grid, GRID_INTERNAL_PAD, 0);
    lv_obj_set_style_pad_row(grid, GRID_INTERNAL_PAD, 0);
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
    return static_cast<lv_coord_t>(lv_font_get_line_height(fonts.inter_13_bold));
}

FLASHMEM void createTile(uint8_t tileIndex,
                         lv_obj_t* grid,
                         lv_obj_t* noteLayer,
                         lv_obj_t*& tile,
                         lv_obj_t*& noteLabel,
                         lv_obj_t*& stepIndexLabel,
                         lv_obj_t*& stepInlineIcon,
                         lv_obj_t*& stepButton,
                         lv_obj_t*& stepShape,
                         lv_obj_t*& stepMarker,
                         lv_obj_t*& stepIndicator,
                         lv_obj_t*& stepSelectionDot,
                         std::array<lv_obj_t*, 3>& stepGuides,
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
    lv_obj_add_event_cb(stepButton, geometryEvent, LV_EVENT_LAYOUT_CHANGED, geometryUserData);

    stepIndexLabel = lv_label_create(stepButton);
    lv_label_set_text(stepIndexLabel, "");
    lv_obj_set_width(stepIndexLabel, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(stepIndexLabel, fonts.inter_13_bold, 0);
    lv_obj_set_style_text_color(stepIndexLabel, lv_color_hex(STEP_INDEX_COLOR), 0);
    lv_obj_set_style_text_opa(stepIndexLabel, STEP_INDEX_OPA, 0);
    lv_obj_set_style_pad_all(stepIndexLabel, 0, 0);
    lv_obj_align(stepIndexLabel, LV_ALIGN_TOP_RIGHT, -4, 2);

    for (uint8_t g = 0; g < STEP_GUIDE_COUNT; ++g) {
        lv_obj_t* guide = lv_obj_create(stepButton);
        stepGuides[g] = guide;
        initStepGuide(guide);
    }
    positionStepGuides(stepButton, stepGuides);

    stepShape = lv_obj_create(noteLayer);
    lv_obj_remove_style_all(stepShape);
    lv_obj_add_flag(stepShape, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(stepShape, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(stepShape, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_size(stepShape, STEP_SHAPE_MIN_WIDTH, STEP_SHAPE_MIN_HEIGHT);
    lv_obj_set_style_radius(stepShape, STEP_SHAPE_RADIUS, 0);
    lv_obj_set_style_border_width(stepShape, STEP_SHAPE_STROKE_WIDTH, 0);
    lv_obj_set_style_border_side(
        stepShape,
        static_cast<lv_border_side_t>(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_BOTTOM),
        0
    );
    lv_obj_set_style_border_color(stepShape, lv_color_hex(theme::color::INACTIVE_LIGHTER), 0);
    lv_obj_set_style_border_opa(stepShape, grid::STEP_SHAPE_OPA_ENABLED, 0);
    lv_obj_set_style_bg_opa(stepShape, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(stepShape, LV_OBJ_FLAG_HIDDEN);

    stepMarker = lv_obj_create(noteLayer);
    lv_obj_remove_style_all(stepMarker);
    lv_obj_add_flag(stepMarker, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(stepMarker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(stepMarker, STEP_MARKER_SIZE, STEP_MARKER_SIZE);
    lv_obj_set_style_radius(stepMarker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(stepMarker, 0, 0);
    lv_obj_set_style_bg_color(stepMarker, lv_color_hex(theme::color::INACTIVE_LIGHTER), 0);
    lv_obj_set_style_bg_opa(stepMarker, LV_OPA_COVER, 0);
    lv_obj_add_flag(stepMarker, LV_OBJ_FLAG_HIDDEN);

    stepIndicator = lv_obj_create(stepButton);
    lv_obj_clear_flag(stepIndicator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(stepIndicator, LV_PCT(100));
    lv_obj_set_height(stepIndicator, grid::STEP_BAR_HEIGHT);
    lv_obj_set_style_radius(stepIndicator, 0, 0);
    lv_obj_set_style_border_width(stepIndicator, 0, 0);
    lv_obj_set_style_bg_color(stepIndicator, lv_color_hex(COLOR_STEP_PLAY_HEX), 0);
    lv_obj_set_style_bg_opa(stepIndicator, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(stepIndicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(stepIndicator, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    stepSelectionDot = lv_obj_create(stepButton);
    lv_obj_remove_style_all(stepSelectionDot);
    lv_obj_clear_flag(stepSelectionDot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(stepSelectionDot, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(stepSelectionDot, STEP_SELECTION_DOT_SIZE, STEP_SELECTION_DOT_SIZE);
    lv_obj_set_style_radius(stepSelectionDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(stepSelectionDot, 0, 0);
    lv_obj_set_style_bg_color(stepSelectionDot, lv_color_hex(COLOR_SELECTION_COPY_HEX), 0);
    lv_obj_set_style_bg_opa(stepSelectionDot, LV_OPA_TRANSP, 0);
    lv_obj_center(stepSelectionDot);
    lv_obj_add_flag(stepSelectionDot, LV_OBJ_FLAG_HIDDEN);

    noteLabel = lv_label_create(noteLayer);
    lv_label_set_text(noteLabel, "");
    lv_obj_add_flag(noteLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_width(noteLabel, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(noteLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(noteLabel, fonts.inter_13_bold, 0);
    lv_obj_set_style_text_color(noteLabel, lv_color_hex(STEP_INLINE_NOTE_COLOR), 0);
    lv_obj_set_style_text_opa(noteLabel, STEP_INLINE_NOTE_OPA, 0);
    lv_obj_set_style_pad_all(noteLabel, 0, 0);
    lv_obj_add_flag(noteLabel, LV_OBJ_FLAG_HIDDEN);

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

    for (auto* guide : stepGuides) {
        if (guide) {
            lv_obj_move_foreground(guide);
        }
    }
}

void positionStepGuides(lv_obj_t* button, const std::array<lv_obj_t*, 3>& guides) {
    if (!button) return;

    const lv_coord_t railWidth = grid::measureRailWidth(lv_obj_get_content_width(button));
    const lv_coord_t buttonHeight = grid::measureButtonHeight(lv_obj_get_content_height(button));

    for (uint8_t g = 0; g < STEP_GUIDE_COUNT; ++g) {
        lv_obj_t* guide = guides[g];
        if (!guide) continue;

        const auto layout = grid::buildGuideLayout(g, railWidth, buttonHeight);
        lv_obj_set_height(guide, layout.height);
        lv_obj_align(guide, LV_ALIGN_TOP_LEFT, layout.x, layout.y);
    }
}

}  // namespace core::ui::sequencer::grid::widgets

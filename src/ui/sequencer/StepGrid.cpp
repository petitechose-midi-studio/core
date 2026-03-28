#include "StepGrid.hpp"

#include <array>
#include <cmath>
#include <cstring>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "state/sequencer/StepPropertyDisplay.hpp"
#include "ui/sequencer/StepGridGeometryLogic.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepGridLabelLogic.hpp"
#include "ui/sequencer/StepGridRenderLogic.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;
namespace grid = core::ui::sequencer::grid;

namespace core::ui {

namespace {

constexpr uint32_t COLOR_STEP_PLAY_HEX = 0x5CA8EE;
constexpr lv_coord_t STEP_BUTTON_SIZE = grid::STEP_BUTTON_SIZE;
constexpr lv_coord_t STEP_SHAPE_RADIUS = 0;
constexpr lv_coord_t STEP_SHAPE_STROKE_WIDTH = 2;
constexpr lv_coord_t STEP_SHAPE_MIN_WIDTH = grid::STEP_SHAPE_MIN_WIDTH;
constexpr lv_coord_t STEP_SHAPE_MIN_HEIGHT = grid::STEP_SHAPE_MIN_HEIGHT;
constexpr lv_coord_t STEP_GUIDE_WIDTH = 1;
constexpr uint8_t STEP_GUIDE_COUNT = 3;

constexpr lv_coord_t STEP_BAR_WIDTH = static_cast<lv_coord_t>(STEP_BUTTON_SIZE / 2);
constexpr lv_coord_t STEP_BAR_HEIGHT = grid::STEP_BAR_HEIGHT;
constexpr lv_coord_t STEP_MARKER_SIZE = 6;
constexpr lv_opa_t STEP_BAR_ACTIVE_OPA = LV_OPA_COVER;
constexpr lv_opa_t STEP_SHAPE_OPA_ENABLED = grid::STEP_SHAPE_OPA_ENABLED;

constexpr lv_coord_t HORIZONTAL_INSET = theme::layout::MARGIN_SM + 4;
constexpr lv_coord_t OVERLAY_SAFE_TOP = theme::layout::MARGIN_XS;
constexpr lv_coord_t OVERLAY_SAFE_BOTTOM = theme::layout::MARGIN_SM * 6;
constexpr uint32_t STEP_TEXT_DISABLED_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_TEXT_DISABLED_OPA = static_cast<lv_opa_t>(theme::opacity::OPA_50);
constexpr uint32_t STEP_GUIDE_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_GUIDE_OPA = LV_OPA_50;
constexpr uint32_t STEP_INLINE_NOTE_COLOR = theme::color::TEXT_PRIMARY;
constexpr lv_opa_t STEP_INLINE_NOTE_OPA = LV_OPA_COVER;
constexpr lv_opa_t STEP_INLINE_VALUE_OPA = LV_OPA_70;
constexpr lv_opa_t STEP_PROBABILITY_MASKED_OPA = LV_OPA_30;
constexpr uint32_t COLOR_SELECTION_CLEAR_HEX = 0xF28B5B;
constexpr uint32_t COLOR_SELECTION_COPY_HEX = 0x59B7C9;
constexpr uint32_t COLOR_SELECTION_PASTE_HEX = 0x79C96B;
constexpr lv_opa_t STEP_SELECTION_RANGE_OPA = LV_OPA_20;
constexpr lv_opa_t STEP_SELECTION_CURSOR_OPA = LV_OPA_80;
void initStepGuide(lv_obj_t* guide) {
    if (!guide) return;

    lv_obj_clear_flag(guide, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(guide, STEP_GUIDE_WIDTH, 8);
    lv_obj_set_style_radius(guide, 0, 0);
    lv_obj_set_style_border_width(guide, 0, 0);
    lv_obj_set_style_bg_color(guide, lv_color_hex(STEP_GUIDE_COLOR), 0);
    lv_obj_set_style_bg_opa(guide, STEP_GUIDE_OPA, 0);
}

void positionStepGuides(lv_obj_t* button, const std::array<lv_obj_t*, STEP_GUIDE_COUNT>& guides) {
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

bool feedbackActiveForStep(
    const core::ui::sequencer::grid::InlineFeedbackSnapshot& feedback,
    uint8_t absoluteStep
) {
    return feedback.visible && ((feedback.touchedMask & (1ULL << absoluteStep)) != 0);
}

bool tileFeedbackChanged(
    uint8_t absoluteStep,
    const core::ui::sequencer::grid::InlineFeedbackSnapshot& before,
    const core::ui::sequencer::grid::InlineFeedbackSnapshot& after
) {
    const bool beforeActive = feedbackActiveForStep(before, absoluteStep);
    const bool afterActive = feedbackActiveForStep(after, absoluteStep);
    if (beforeActive != afterActive) return true;
    if (!(beforeActive || afterActive)) return false;
    return before.property != after.property;
}

bool sameSelectionSnapshot(const grid::RangeSelectionSnapshot& a,
                           const grid::RangeSelectionSnapshot& b) {
    return a.active == b.active &&
           a.kind == b.kind &&
           a.phase == b.phase &&
           a.cursorStep == b.cursorStep &&
           a.sourceRangeVisible == b.sourceRangeVisible &&
           a.sourceStart == b.sourceStart &&
           a.sourceEnd == b.sourceEnd;
}

bool stepInSelectionRange(uint8_t absoluteStep, const grid::RangeSelectionSnapshot& selection) {
    return selection.sourceRangeVisible &&
           absoluteStep >= selection.sourceStart &&
           absoluteStep <= selection.sourceEnd;
}

}  // namespace

StepGrid::StepGrid(lv_obj_t* parent) {
    createUI(parent);
    createTiles();
}

StepGrid::~StepGrid() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        grid_ = nullptr;
        note_layer_ = nullptr;
    }
}

void StepGrid::forceRefresh() {
    geometry_dirty_ = true;
    cached_feedback_ = {};
    invalidateTileCaches();
}

FLASHMEM void StepGrid::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_).size(LV_PCT(100), LV_PCT(100)).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_flex_grow(container_, 1);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    grid_ = lv_obj_create(container_);
    style::apply(grid_).size(LV_PCT(100), LV_PCT(100)).transparent().noBorder();

    static lv_coord_t col_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid_, col_dsc, row_dsc);
    lv_obj_set_layout(grid_, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_top(grid_, OVERLAY_SAFE_TOP, 0);
    lv_obj_set_style_pad_bottom(grid_, OVERLAY_SAFE_BOTTOM, 0);
    lv_obj_set_style_pad_left(grid_, HORIZONTAL_INSET, 0);
    lv_obj_set_style_pad_right(grid_, HORIZONTAL_INSET, 0);
    lv_obj_set_style_pad_column(grid_, 0, 0);
    lv_obj_set_style_pad_row(grid_, theme::layout::MARGIN_SM, 0);
    lv_obj_add_flag(grid_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(grid_, onGeometryChangedEvent, LV_EVENT_SIZE_CHANGED, this);
    lv_obj_add_event_cb(grid_, onGeometryChangedEvent, LV_EVENT_LAYOUT_CHANGED, this);

    note_layer_ = lv_obj_create(container_);
    style::apply(note_layer_)
        .size(LV_PCT(100), LV_PCT(100))
        .transparent()
        .noBorder()
        .pad(0)
        .noScroll();
    lv_obj_add_flag(note_layer_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(note_layer_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_align(note_layer_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_background(note_layer_);
    lv_obj_add_event_cb(note_layer_, onGeometryChangedEvent, LV_EVENT_SIZE_CHANGED, this);
}

FLASHMEM void StepGrid::createTiles() {
    note_label_height_ = static_cast<lv_coord_t>(lv_font_get_line_height(fonts.inter_13_bold));

    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        const uint8_t col = i % 4;
        const uint8_t row = i / 4;

        lv_obj_t* tile = lv_obj_create(grid_);
        tiles_[i] = tile;
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

        lv_obj_t* btnWrap = lv_obj_create(tile);
        lv_obj_remove_style_all(btnWrap);
        lv_obj_clear_flag(btnWrap, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(btnWrap, LV_PCT(100));
        lv_obj_set_flex_grow(btnWrap, 1);
        lv_obj_set_layout(btnWrap, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(btnWrap, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btnWrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(btnWrap, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

        lv_obj_t* btn = lv_obj_create(btnWrap);
        step_buttons_[i] = btn;
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, LV_PCT(100));
        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(theme::color::INACTIVE), 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_add_event_cb(btn, onGeometryChangedEvent, LV_EVENT_SIZE_CHANGED, this);
        lv_obj_add_event_cb(btn, onGeometryChangedEvent, LV_EVENT_LAYOUT_CHANGED, this);

        for (uint8_t g = 0; g < STEP_GUIDE_COUNT; ++g) {
            lv_obj_t* guide = lv_obj_create(btn);
            step_guides_[i][g] = guide;
            initStepGuide(guide);
        }
        positionStepGuides(btn, step_guides_[i]);

        lv_obj_t* shape = lv_obj_create(note_layer_);
        step_shapes_[i] = shape;
        lv_obj_remove_style_all(shape);
        lv_obj_add_flag(shape, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(shape, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_size(shape, STEP_SHAPE_MIN_WIDTH, STEP_SHAPE_MIN_HEIGHT);
        lv_obj_set_style_radius(shape, STEP_SHAPE_RADIUS, 0);
        lv_obj_set_style_border_width(shape, STEP_SHAPE_STROKE_WIDTH, 0);
        lv_obj_set_style_border_side(
            shape,
            static_cast<lv_border_side_t>(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_BOTTOM),
            0
        );
        lv_obj_set_style_border_color(shape, lv_color_hex(theme::color::INACTIVE_LIGHTER), 0);
        lv_obj_set_style_border_opa(shape, STEP_SHAPE_OPA_ENABLED, 0);
        lv_obj_set_style_bg_opa(shape, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(shape, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* marker = lv_obj_create(note_layer_);
        step_markers_[i] = marker;
        lv_obj_remove_style_all(marker);
        lv_obj_add_flag(marker, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(marker, STEP_MARKER_SIZE, STEP_MARKER_SIZE);
        lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(marker, 0, 0);
        lv_obj_set_style_bg_color(marker, lv_color_hex(theme::color::INACTIVE_LIGHTER), 0);
        lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
        lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* ph = lv_obj_create(btn);
        step_indicators_[i] = ph;
        lv_obj_clear_flag(ph, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(ph, LV_PCT(100));
        lv_obj_set_height(ph, STEP_BAR_HEIGHT);
        lv_obj_set_style_radius(ph, 0, 0);
        lv_obj_set_style_border_width(ph, 0, 0);
        lv_obj_set_style_bg_color(ph, lv_color_hex(COLOR_STEP_PLAY_HEX), 0);
        lv_obj_set_style_bg_opa(ph, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(ph, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(ph, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        lv_obj_t* note = lv_label_create(note_layer_);
        note_labels_[i] = note;
        lv_label_set_text(note, "");
        lv_obj_add_flag(note, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_width(note, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_font(note, fonts.inter_13_bold, 0);
        lv_obj_set_style_text_color(note, lv_color_hex(STEP_INLINE_NOTE_COLOR), 0);
        lv_obj_set_style_text_opa(note, STEP_INLINE_NOTE_OPA, 0);
        lv_obj_set_style_pad_all(note, 0, 0);
        lv_obj_add_flag(note, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* inlineIcon = lv_label_create(note_layer_);
        step_inline_icons_[i] = inlineIcon;
        standalone::icons::set(inlineIcon, standalone::icons::NOTE_PROP_RANDOM, standalone::icons::Size::S);
        lv_obj_add_flag(inlineIcon, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_width(inlineIcon, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(inlineIcon, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_color(inlineIcon, lv_color_hex(STEP_INLINE_NOTE_COLOR), 0);
        lv_obj_set_style_text_opa(inlineIcon, STEP_INLINE_NOTE_OPA, 0);
        lv_obj_set_style_pad_all(inlineIcon, 0, 0);
        lv_obj_add_flag(inlineIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_update_layout(inlineIcon);
        inline_icon_width_[i] = lv_obj_get_width(inlineIcon);
        inline_icon_height_[i] = lv_obj_get_height(inlineIcon);

        for (uint8_t g = 0; g < STEP_GUIDE_COUNT; ++g) {
            if (step_guides_[i][g]) {
                lv_obj_move_foreground(step_guides_[i][g]);
            }
        }
    }
}

void StepGrid::invalidateTileCaches() {
    for (auto& cache : tile_render_cache_) {
        cache.initialized = false;
    }
}

void StepGrid::onGeometryChangedEvent(lv_event_t* event) {
    auto* self = static_cast<StepGrid*>(lv_event_get_user_data(event));
    if (!self) return;
    self->markGeometryDirty();
}

void StepGrid::markGeometryDirty() {
    geometry_dirty_ = true;
}

void StepGrid::refreshStaticGeometry() {
    if (!note_layer_ || !container_) return;

    lv_obj_update_layout(container_);

    lv_area_t noteLayerArea{};
    lv_obj_get_coords(note_layer_, &noteLayerArea);

    for (uint8_t i = 0; i < step_buttons_.size(); ++i) {
        lv_obj_t* button = step_buttons_[i];
        if (!button) continue;

        lv_area_t buttonArea{};
        lv_obj_get_coords(button, &buttonArea);

        const auto geometry = grid::buildTileGeometry(
            buttonArea,
            noteLayerArea,
            grid::measureRailWidth(lv_obj_get_content_width(button)),
            grid::measureButtonHeight(lv_obj_get_content_height(button))
        );
        rail_width_cache_[i] = geometry.railWidth;
        button_height_cache_[i] = geometry.buttonHeight;
        note_base_x_[i] = geometry.noteBaseX;
        note_base_y_[i] = geometry.noteBaseY;
        note_label_baseline_y_[i] = geometry.noteLabelBaselineY;

        positionStepGuides(button, step_guides_[i]);
    }

    geometry_dirty_ = false;
}

void StepGrid::renderTileGuides(uint8_t tileIndex, bool inPattern, const TileRenderDiff& diff) {
    auto& cache = tile_render_cache_[tileIndex];
    for (auto* guide : step_guides_[tileIndex]) {
        if (!guide) continue;
        if (!diff.inPatternChanged && cache.initialized) continue;
        if (inPattern) lv_obj_clear_flag(guide, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(guide, LV_OBJ_FLAG_HIDDEN);
    }
}

void StepGrid::renderTileShape(uint8_t tileIndex,
                               const sequencer::grid::StepVisualStyle& visual,
                               lv_coord_t noteBaseX,
                               lv_coord_t noteBaseY,
                               lv_opa_t strokeOpa) {
    auto& cache = tile_render_cache_[tileIndex];
    lv_obj_t* shape = step_shapes_[tileIndex];
    if (!shape) return;

    if (!cache.shapeVisible) {
        lv_obj_clear_flag(shape, LV_OBJ_FLAG_HIDDEN);
        cache.shapeVisible = true;
    }

    if (cache.shapeWidth != visual.width || cache.shapeHeight != visual.height) {
        lv_obj_set_size(shape, visual.width, visual.height);
        cache.shapeWidth = visual.width;
        cache.shapeHeight = visual.height;
    }

    if (cache.shapeX != noteBaseX || cache.shapeY != noteBaseY) {
        lv_obj_set_pos(shape, noteBaseX, noteBaseY);
        cache.shapeX = noteBaseX;
        cache.shapeY = noteBaseY;
    }

    const uint32_t nextStrokeColor = lv_color_to_int(visual.strokeColor);
    if (cache.shapeStrokeColor != nextStrokeColor) {
        lv_obj_set_style_border_color(shape, visual.strokeColor, 0);
        cache.shapeStrokeColor = nextStrokeColor;
    }

    if (cache.shapeStrokeOpa != strokeOpa) {
        lv_obj_set_style_border_opa(shape, strokeOpa, 0);
        cache.shapeStrokeOpa = strokeOpa;
    }
}

void StepGrid::renderTileMarker(uint8_t tileIndex,
                                const TileRenderState& state,
                                bool noteVisualChanged,
                                lv_coord_t noteBaseX,
                                lv_coord_t noteBaseY,
                                lv_opa_t markerOpa) {
    auto& cache = tile_render_cache_[tileIndex];
    lv_obj_t* marker = step_markers_[tileIndex];
    if (!marker) return;

    if (!cache.markerVisible) {
        lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
        cache.markerVisible = true;
    }

    if (noteVisualChanged) {
        const lv_point_t markerPosition = grid::buildMarkerPosition(noteBaseX, noteBaseY);
        if (cache.markerX != markerPosition.x || cache.markerY != markerPosition.y) {
            lv_obj_set_pos(marker, markerPosition.x, markerPosition.y);
            cache.markerX = markerPosition.x;
            cache.markerY = markerPosition.y;
        }
    }

    const lv_color_t nextMarkerColor =
        state.enabled ? grid::velocityMarkerColor(state.note, state.velocity)
                      : lv_color_hex(STEP_TEXT_DISABLED_COLOR);
    const uint32_t nextMarkerColorInt = lv_color_to_int(nextMarkerColor);
    if (cache.markerColor != nextMarkerColorInt) {
        lv_obj_set_style_bg_color(marker, nextMarkerColor, 0);
        cache.markerColor = nextMarkerColorInt;
    }

    if (cache.markerOpa != markerOpa) {
        lv_obj_set_style_bg_opa(marker, markerOpa, 0);
        cache.markerOpa = markerOpa;
    }
}

void StepGrid::renderTileBar(uint8_t tileIndex, bool visible) {
    auto& cache = tile_render_cache_[tileIndex];
    lv_obj_t* indicator = step_indicators_[tileIndex];
    if (!indicator) return;

    const lv_opa_t nextOpa = visible ? STEP_BAR_ACTIVE_OPA : LV_OPA_TRANSP;

    if (visible) {
        if (!cache.indicatorVisible) {
            lv_obj_clear_flag(indicator, LV_OBJ_FLAG_HIDDEN);
            cache.indicatorVisible = true;
        }
    } else if (cache.indicatorVisible) {
        lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
        cache.indicatorVisible = false;
    }

    if (cache.indicatorOpa != nextOpa) {
        lv_obj_set_style_bg_opa(indicator, nextOpa, 0);
        cache.indicatorOpa = nextOpa;
    }
}

void StepGrid::renderTileSelection(uint8_t tileIndex,
                                   uint8_t absoluteStep,
                                   const sequencer::grid::RangeSelectionSnapshot& selection) {
    auto& cache = tile_render_cache_[tileIndex];
    lv_obj_t* button = step_buttons_[tileIndex];
    if (!button) return;

    const bool rangeVisible = selection.active && stepInSelectionRange(absoluteStep, selection);
    const bool cursorVisible = selection.active && absoluteStep == selection.cursorStep;
    const bool pasteTarget =
        selection.kind == core::state::sequencer::RangeSelectionKind::COPY &&
        selection.phase == core::state::sequencer::RangeSelectionPhase::PASTE_TARGET;

    uint32_t rangeColor = theme::color::INACTIVE;
    if (selection.kind == core::state::sequencer::RangeSelectionKind::CLEAR) {
        rangeColor = COLOR_SELECTION_CLEAR_HEX;
    } else if (selection.kind == core::state::sequencer::RangeSelectionKind::COPY) {
        rangeColor = COLOR_SELECTION_COPY_HEX;
    }

    uint32_t cursorColor = rangeColor;
    if (pasteTarget) {
        cursorColor = COLOR_SELECTION_PASTE_HEX;
    }

    const lv_opa_t nextBgOpa = rangeVisible ? STEP_SELECTION_RANGE_OPA : LV_OPA_TRANSP;
    const lv_opa_t nextBorderOpa = cursorVisible ? STEP_SELECTION_CURSOR_OPA : LV_OPA_TRANSP;

    if (cache.buttonBgColor != rangeColor) {
        lv_obj_set_style_bg_color(button, lv_color_hex(rangeColor), 0);
        cache.buttonBgColor = rangeColor;
    }
    if (cache.buttonBgOpa != nextBgOpa) {
        lv_obj_set_style_bg_opa(button, nextBgOpa, 0);
        cache.buttonBgOpa = nextBgOpa;
    }
    if (cache.buttonBorderColor != cursorColor) {
        lv_obj_set_style_border_color(button, lv_color_hex(cursorColor), 0);
        cache.buttonBorderColor = cursorColor;
    }
    if (cache.buttonBorderOpa != nextBorderOpa) {
        lv_obj_set_style_border_opa(button, nextBorderOpa, 0);
        cache.buttonBorderOpa = nextBorderOpa;
    }
}

void StepGrid::renderTile(
    uint8_t tileIndex,
    const TileRenderState& state,
    const TileRenderDiff& diff,
    bool propertyVisualChanged,
    bool tileFeedbackChanged,
    bool selectionChanged,
    const StepGridFrameState& frameState
) {
    auto& cache = tile_render_cache_[tileIndex];
    const bool probabilityMasked =
        state.inPattern &&
        state.enabled &&
        !state.probabilityCycleActive;
    const bool noteVisualChanged =
        !cache.initialized ||
        diff.inPatternChanged ||
        diff.enabledChanged ||
        diff.noteChanged ||
        diff.velocityChanged ||
        diff.gateChanged ||
        diff.nudgeChanged;
    const bool noteLabelNeedsRender =
        !cache.initialized ||
        propertyVisualChanged ||
        tileFeedbackChanged ||
        diff.inPatternChanged ||
        diff.enabledChanged ||
        diff.noteChanged ||
        diff.velocityChanged ||
        diff.probabilityChanged ||
        diff.gateChanged ||
        diff.nudgeChanged ||
        diff.probabilityCycleActiveChanged;
    const lv_coord_t noteLabelY = note_label_baseline_y_[tileIndex];

    if (!diff.dataChanged && !diff.barChanged && !propertyVisualChanged && !tileFeedbackChanged &&
        !selectionChanged) {
        return;
    }

    if (selectionChanged || !cache.initialized) {
        renderTileSelection(tileIndex, state.absoluteStep, frameState.selection);
    }

    if (diff.dataChanged) {
        const auto visual = grid::buildStepVisualStyle(
            state.note,
            state.velocity,
            state.gate,
            state.nudge,
            state.enabled,
            rail_width_cache_[tileIndex],
            button_height_cache_[tileIndex]
        );
        const lv_coord_t noteBaseX = static_cast<lv_coord_t>(note_base_x_[tileIndex] + visual.x);
        const lv_coord_t noteBaseY = static_cast<lv_coord_t>(note_base_y_[tileIndex] + visual.y);
        const lv_opa_t shapeStrokeOpa =
            probabilityMasked ? STEP_PROBABILITY_MASKED_OPA : visual.strokeOpa;
        const lv_opa_t markerOpa =
            state.enabled
                ? (probabilityMasked ? STEP_PROBABILITY_MASKED_OPA : LV_OPA_COVER)
                : STEP_TEXT_DISABLED_OPA;

        renderTileGuides(tileIndex, state.inPattern, diff);

        if (step_shapes_[tileIndex]) {
            if (!state.inPattern) {
                if (cache.shapeVisible) {
                    lv_obj_add_flag(step_shapes_[tileIndex], LV_OBJ_FLAG_HIDDEN);
                    cache.shapeVisible = false;
                }
                if (step_markers_[tileIndex] && cache.markerVisible) {
                    lv_obj_add_flag(step_markers_[tileIndex], LV_OBJ_FLAG_HIDDEN);
                    cache.markerVisible = false;
                }
            } else {
                renderTileShape(tileIndex, visual, noteBaseX, noteBaseY, shapeStrokeOpa);
                renderTileMarker(tileIndex, state, noteVisualChanged, noteBaseX, noteBaseY, markerOpa);
            }
        }
    }

    if (noteLabelNeedsRender) {
        const auto propertyVisual = sequencer::visual::buildStepPropertyVisual(
            frameState.activeProperty,
            state.inPattern
        );

        renderTileNoteLabel(
            tileIndex,
            state,
            diff,
            propertyVisualChanged,
            tileFeedbackChanged,
            frameState,
            propertyVisual,
            note_base_x_[tileIndex],
            noteLabelY
        );
    }

    if (diff.dataChanged || diff.barChanged) {
        renderTileBar(tileIndex, state.playing && state.inPattern);
    }

    cache.initialized = true;
    cache.inPattern = state.inPattern;
    cache.enabled = state.enabled;
    cache.playing = state.playing;
    cache.probabilityCycleActive = state.probabilityCycleActive;
    cache.note = state.note;
    cache.velocity = state.velocity;
    cache.probability = state.probability;
    cache.gate = state.gate;
    cache.nudge = state.nudge;
}

void StepGrid::renderTileNoteLabel(uint8_t tileIndex,
                                   const TileRenderState& state,
                                   const TileRenderDiff& diff,
                                   bool propertyVisualChanged,
                                   bool tileFeedbackChanged,
                                   const StepGridFrameState& frameState,
                                   const sequencer::visual::StepPropertyVisualSpec& propertyVisual,
                                   lv_coord_t noteBaseX,
                                   lv_coord_t noteLabelY) {
    auto& cache = tile_render_cache_[tileIndex];
    lv_obj_t* noteLabel = note_labels_[tileIndex];
    lv_obj_t* inlineIcon = step_inline_icons_[tileIndex];
    if (!noteLabel || !inlineIcon) return;

    const auto labelPresentation = grid::buildNoteLabelPresentation(
        state,
        propertyVisual,
        frameState.activeProperty,
        grid::readInlineFeedbackSnapshot(
            frameState.feedbackVisible,
            frameState.feedbackTouchedMask,
            frameState.feedbackProperty
        )
    );

    if (!labelPresentation.showLabel) {
        if (cache.noteLabelVisible) {
            lv_obj_add_flag(noteLabel, LV_OBJ_FLAG_HIDDEN);
            cache.noteLabelVisible = false;
        }
        if (cache.inlineIconVisible) {
            lv_obj_add_flag(inlineIcon, LV_OBJ_FLAG_HIDDEN);
            cache.inlineIconVisible = false;
        }
        return;
    }

    if (!cache.noteLabelVisible) {
        lv_obj_clear_flag(noteLabel, LV_OBJ_FLAG_HIDDEN);
        cache.noteLabelVisible = true;
    }

    if (labelPresentation.showInlineIcon) {
        if (!cache.inlineIconVisible) {
            lv_obj_clear_flag(inlineIcon, LV_OBJ_FLAG_HIDDEN);
            cache.inlineIconVisible = true;
        }
    } else if (cache.inlineIconVisible) {
        lv_obj_add_flag(inlineIcon, LV_OBJ_FLAG_HIDDEN);
        cache.inlineIconVisible = false;
    }

    if (propertyVisualChanged || diff.noteChanged || diff.velocityChanged || diff.gateChanged ||
        diff.nudgeChanged || diff.probabilityChanged) {
        char buf[16];
        core::state::sequencer::formatStepPropertyValue(
            buf,
            sizeof(buf),
            labelPresentation.displayProperty,
            state.note,
            state.velocity,
            state.gate,
            state.nudge,
            state.probability
        );
        if (std::strcmp(cache.noteLabelText, buf) != 0) {
            lv_label_set_text(noteLabel, buf);
            std::strncpy(cache.noteLabelText, buf, sizeof(cache.noteLabelText) - 1);
            cache.noteLabelText[sizeof(cache.noteLabelText) - 1] = '\0';
        }
        cache.noteLabelHeight = note_label_height_;
    }

    if (propertyVisualChanged || diff.noteChanged || diff.enabledChanged || diff.velocityChanged ||
        diff.gateChanged || diff.nudgeChanged || diff.probabilityChanged || diff.probabilityCycleActiveChanged) {
        const lv_color_t nextNoteLabelColor =
            state.enabled ? grid::noteLabelColor(state.note)
                          : lv_color_hex(STEP_TEXT_DISABLED_COLOR);
        const lv_opa_t nextNoteLabelOpa =
            state.enabled
                ? (labelPresentation.probabilityMasked
                    ? STEP_PROBABILITY_MASKED_OPA
                    : (labelPresentation.showNoteStyle ? STEP_INLINE_NOTE_OPA : STEP_INLINE_VALUE_OPA))
                : STEP_TEXT_DISABLED_OPA;
        const lv_color_t nextInlineIconColor =
            state.enabled ? grid::probabilityInlineIconColor(state.note, state.probability)
                          : lv_color_hex(STEP_TEXT_DISABLED_COLOR);
        const lv_opa_t nextInlineIconOpa =
            state.enabled
                ? (labelPresentation.probabilityMasked ? STEP_PROBABILITY_MASKED_OPA : STEP_INLINE_VALUE_OPA)
                : STEP_TEXT_DISABLED_OPA;

        const uint32_t nextNoteLabelColorInt = lv_color_to_int(nextNoteLabelColor);
        const uint32_t nextInlineIconColorInt = lv_color_to_int(nextInlineIconColor);

        if (cache.noteLabelColorFull != nextNoteLabelColorInt) {
            lv_obj_set_style_text_color(noteLabel, nextNoteLabelColor, 0);
            cache.noteLabelColorFull = nextNoteLabelColorInt;
        }
        if (cache.noteLabelOpa != nextNoteLabelOpa) {
            lv_obj_set_style_text_opa(noteLabel, nextNoteLabelOpa, 0);
            cache.noteLabelOpa = nextNoteLabelOpa;
        }

        if (cache.inlineIconColorFull != nextInlineIconColorInt) {
            lv_obj_set_style_text_color(inlineIcon, nextInlineIconColor, 0);
            cache.inlineIconColorFull = nextInlineIconColorInt;
        }
        if (cache.inlineIconOpa != nextInlineIconOpa) {
            lv_obj_set_style_text_opa(inlineIcon, nextInlineIconOpa, 0);
            cache.inlineIconOpa = nextInlineIconOpa;
        }
    }

    if ((propertyVisualChanged || diff.inPatternChanged || diff.noteChanged || diff.velocityChanged ||
         diff.gateChanged || diff.nudgeChanged || diff.probabilityChanged) &&
        cache.noteLabelHeight <= 0) {
        cache.noteLabelHeight = note_label_height_;
    }

    if (propertyVisualChanged || tileFeedbackChanged || diff.inPatternChanged ||
        cache.noteLabelHeight <= 0) {
        const lv_coord_t iconHeight = labelPresentation.showInlineIcon ? inline_icon_height_[tileIndex] : 0;
        const lv_coord_t iconWidth = labelPresentation.showInlineIcon ? inline_icon_width_[tileIndex] : 0;

        const auto layout = grid::buildInlineLabelLayout(
            noteBaseX,
            noteLabelY,
            cache.noteLabelHeight,
            labelPresentation.showInlineIcon,
            iconWidth,
            iconHeight
        );

        if (labelPresentation.showInlineIcon) {
            if (cache.inlineIconX != layout.iconX || cache.inlineIconY != layout.iconY) {
                lv_obj_set_pos(inlineIcon, layout.iconX, layout.iconY);
                cache.inlineIconX = layout.iconX;
                cache.inlineIconY = layout.iconY;
            }
        }

        if (cache.noteLabelX != layout.labelX || cache.noteLabelY != layout.labelY) {
            lv_obj_set_pos(noteLabel, layout.labelX, layout.labelY);
            cache.noteLabelX = layout.labelX;
            cache.noteLabelY = layout.labelY;
        }
    }
}

void StepGrid::render(const sequencer::grid::StepGridFrameState& frameState) {
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;

    if (geometry_dirty_) {
        refreshStaticGeometry();
    }

    const bool propertyVisualChanged = frameState.activeProperty != cached_property_;
    cached_property_ = frameState.activeProperty;
    const InlineFeedbackSnapshot feedbackSnapshot = grid::readInlineFeedbackSnapshot(
        frameState.feedbackVisible,
        frameState.feedbackTouchedMask,
        frameState.feedbackProperty
    );
    const InlineFeedbackSnapshot previousFeedback = cached_feedback_;
    cached_feedback_ = feedbackSnapshot;
    const bool selectionChanged = !sameSelectionSnapshot(frameState.selection, cached_selection_);
    cached_selection_ = frameState.selection;

    std::array<TileRenderDiff, 8> diffs{};
    std::array<bool, 8> feedback_changed{};
    std::array<bool, 8> tile_dirty{};
    bool any_dirty = false;

    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        const TileRenderState state = frameState.tiles[i];
        diffs[i] = grid::diffTileRenderState(tile_render_cache_[i], state);
        feedback_changed[i] =
            tileFeedbackChanged(state.absoluteStep, previousFeedback, feedbackSnapshot);
        tile_dirty[i] =
            diffs[i].dataChanged ||
            diffs[i].barChanged ||
            propertyVisualChanged ||
            feedback_changed[i] ||
            selectionChanged;
        any_dirty = any_dirty || tile_dirty[i];
    }

    if (!any_dirty) {
        return;
    }

    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        if (!tile_dirty[i]) continue;
        const TileRenderState state = frameState.tiles[i];
        renderTile(
            i,
            state,
            diffs[i],
            propertyVisualChanged,
            feedback_changed[i],
            selectionChanged,
            frameState
        );
    }
}

}  // namespace core::ui

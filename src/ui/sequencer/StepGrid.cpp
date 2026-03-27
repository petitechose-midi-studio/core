#include "StepGrid.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <ms/ui/font/CoreFonts.hpp>

#include "state/sequencer/StepPropertyDisplay.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"
#include "ui/sequencer/StepVisualUtils.hpp"

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace core::ui {

namespace {

constexpr uint32_t COLOR_STEP_PLAY_HEX = 0x5CA8EE;
constexpr uint32_t COLOR_STEP_SELECTOR_HEX = theme::color::TEXT_PRIMARY;
constexpr size_t STEP_PROPERTY_COUNT =
    static_cast<size_t>(core::state::sequencer::StepProperty::PROBABILITY) + 1;

constexpr uint8_t CHROMATIC_NOTE_COUNT = 12;

constexpr lv_coord_t STEP_BUTTON_SIZE = 56;
constexpr lv_coord_t STEP_SHAPE_PAD_X = 0;
constexpr lv_coord_t STEP_SHAPE_PAD_Y = 1;
constexpr lv_coord_t STEP_SHAPE_RADIUS = 0;
constexpr lv_coord_t STEP_SHAPE_STROKE_WIDTH = 2;
constexpr lv_coord_t STEP_SHAPE_MIN_WIDTH = 6;
constexpr lv_coord_t STEP_SHAPE_MIN_HEIGHT = 18;
constexpr lv_coord_t STEP_GUIDE_WIDTH = 1;
constexpr lv_coord_t STEP_GUIDE_HEIGHT = 8;
constexpr uint8_t STEP_GUIDE_COUNT = 3;
constexpr std::array<float, STEP_GUIDE_COUNT> STEP_GUIDE_POSITIONS = {
    0.0f,
    1.0f / 3.0f,
    0.5f,
};

constexpr lv_coord_t STEP_BAR_WIDTH = static_cast<lv_coord_t>(STEP_BUTTON_SIZE / 2);
constexpr lv_coord_t STEP_BAR_HEIGHT = 2;
constexpr lv_coord_t STEP_NOTE_LABEL_PAD_X = 4;
constexpr lv_coord_t STEP_NOTE_LABEL_PAD_BOTTOM = STEP_BAR_HEIGHT + 4;
constexpr lv_coord_t STEP_MARKER_SIZE = 6;
constexpr lv_coord_t STEP_PROPERTY_WATERMARK_OFFSET_Y = -2;
constexpr lv_coord_t STEP_PROPERTY_VALUE_TRACK_WIDTH = 2;
constexpr lv_coord_t STEP_PROPERTY_VALUE_FILL_WIDTH = 2;
constexpr lv_coord_t STEP_PROPERTY_VALUE_TRACK_MIN_HEIGHT = 18;
constexpr lv_coord_t STEP_PROPERTY_VALUE_TRACK_INSET_X = 6;
constexpr lv_coord_t STEP_PROPERTY_VALUE_TRACK_INSET_Y = 8;
constexpr lv_coord_t STEP_PROPERTY_HORIZONTAL_ACCENT_HEIGHT = 2;
constexpr lv_coord_t STEP_PROPERTY_EDGE_TICK_WIDTH = 2;
constexpr lv_coord_t STEP_PROPERTY_EDGE_TICK_HEIGHT = 6;
constexpr lv_opa_t STEP_BAR_ACTIVE_OPA = LV_OPA_COVER;
constexpr lv_coord_t STEP_GUIDE_BOTTOM = STEP_BAR_HEIGHT + 5;

constexpr uint8_t SHAPE_DISABLED_FILL_BRIGHTNESS = 20;

constexpr lv_opa_t STEP_SHAPE_OPA_ENABLED = LV_OPA_COVER;
constexpr lv_opa_t STEP_SHAPE_OPA_DISABLED = LV_OPA_COVER;
constexpr lv_opa_t STEP_SHAPE_OPA_VELOCITY_ZERO = LV_OPA_COVER;

constexpr uint8_t VELOCITY_MAX = 127;
constexpr int8_t NUDGE_VISUAL_MAX = 50;

constexpr lv_coord_t HORIZONTAL_INSET = theme::layout::MARGIN_SM + 4;
constexpr lv_coord_t OVERLAY_PAD_X = theme::layout::MARGIN_XS;
constexpr lv_coord_t OVERLAY_PAD_Y_TOP = theme::layout::MARGIN_SM;
constexpr lv_coord_t OVERLAY_PAD_Y_BOTTOM = -theme::layout::MARGIN_SM;
constexpr lv_coord_t OVERLAY_SAFE_TOP = theme::layout::MARGIN_XS;
constexpr lv_coord_t OVERLAY_SAFE_BOTTOM = theme::layout::MARGIN_SM * 6;
constexpr uint32_t STEP_TEXT_DISABLED_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_TEXT_DISABLED_OPA = static_cast<lv_opa_t>(theme::opacity::OPA_50);
constexpr uint32_t STEP_GUIDE_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_GUIDE_OPA = LV_OPA_50;
constexpr uint32_t STEP_PROPERTY_WATERMARK_COLOR = theme::color::TEXT_PRIMARY;
constexpr uint32_t STEP_PROPERTY_TRACK_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr uint8_t VELOCITY_ZERO_FILL_BRIGHTNESS = 22;
constexpr uint32_t STEP_INLINE_NOTE_COLOR = theme::color::TEXT_PRIMARY;
constexpr lv_opa_t STEP_INLINE_NOTE_OPA = LV_OPA_COVER;
constexpr lv_opa_t STEP_NOTE_LABEL_LIGHTEN = static_cast<lv_opa_t>(48);
constexpr lv_opa_t STEP_NOTE_MARKER_LIGHTEN = static_cast<lv_opa_t>(24);

constexpr uint32_t CHROMATIC_NOTE_BASE_PALETTE_HEX[] = {
    0xF4F1DE,
    0xEAB69E,
    0xE07A5F,
    0xAA675E,
    0x73535C,
    0x3D405B,
    0x5F797A,
    0x81B29A,
    0xBABF94,
    0xF2CC8F,
    0xF3D8A9,
    0xF3E5C4,
};

uint8_t chromaIndexForNote(uint8_t note) {
    return static_cast<uint8_t>(note % CHROMATIC_NOTE_COUNT);
}

lv_color_t noteBaseColor(uint8_t note) {
    return lv_color_hex(CHROMATIC_NOTE_BASE_PALETTE_HEX[chromaIndexForNote(note)]);
}

lv_color_t noteLabelColor(uint8_t note) {
    return lv_color_lighten(noteBaseColor(note), STEP_NOTE_LABEL_LIGHTEN);
}

lv_color_t velocityAccentColor(uint8_t note, uint8_t velocity) {
    using namespace core::ui::sequencer::visual;

    const lv_color_t fullColor = noteLabelColor(note);
    const lv_color_t minColor = grayscaleColor(VELOCITY_ZERO_FILL_BRIGHTNESS);
    const uint8_t mix = mapToRangeU8(velocity, VELOCITY_MAX, 0, LV_OPA_COVER);
    return lv_color_mix(fullColor, minColor, mix);
}

lv_color_t velocityMarkerColor(uint8_t note, uint8_t velocity) {
    return lv_color_lighten(velocityAccentColor(note, velocity), STEP_NOTE_MARKER_LIGHTEN);
}

struct StepVisualStyle {
    lv_coord_t width = STEP_SHAPE_MIN_WIDTH;
    lv_coord_t height = STEP_SHAPE_MIN_HEIGHT;
    lv_coord_t x = STEP_SHAPE_PAD_X;
    lv_coord_t y = STEP_BUTTON_SIZE - STEP_BAR_HEIGHT - STEP_SHAPE_PAD_Y - STEP_SHAPE_MIN_HEIGHT;
    lv_color_t strokeColor = lv_color_hex(theme::color::INACTIVE);
    lv_opa_t strokeOpa = STEP_SHAPE_OPA_DISABLED;
};

lv_coord_t railWidthForButton(lv_obj_t* button) {
    if (!button) return STEP_BUTTON_SIZE;
    return std::max<lv_coord_t>(STEP_SHAPE_MIN_WIDTH, lv_obj_get_content_width(button));
}

lv_coord_t buttonHeightForButton(lv_obj_t* button) {
    if (!button) return STEP_BUTTON_SIZE;
    return std::max<lv_coord_t>(STEP_BUTTON_SIZE, lv_obj_get_content_height(button));
}

StepVisualStyle buildStepVisualStyle(uint8_t note,
                                     uint8_t velocity,
                                     uint16_t gate,
                                     int8_t nudge,
                                     bool enabled,
                                     lv_coord_t railWidth,
                                     lv_coord_t buttonHeight) {
    using namespace core::ui::sequencer::visual;

    StepVisualStyle style;
    const lv_coord_t shapeMaxWidth = std::max<lv_coord_t>(STEP_SHAPE_MIN_WIDTH, railWidth);
    const lv_coord_t shapeMaxHeight = std::max<lv_coord_t>(
        STEP_SHAPE_MIN_HEIGHT,
        buttonHeight - STEP_BAR_HEIGHT - STEP_SHAPE_PAD_Y
    );
    const uint32_t scaledWidth =
        (static_cast<uint32_t>(gate) * static_cast<uint32_t>(shapeMaxWidth) + 50U) / 100U;
    style.width = static_cast<lv_coord_t>(std::max<uint32_t>(STEP_SHAPE_MIN_WIDTH, scaledWidth));
    const float velocityNorm =
        std::clamp(static_cast<float>(velocity) / static_cast<float>(VELOCITY_MAX), 0.0f, 1.0f);
    style.height = static_cast<lv_coord_t>(
        STEP_SHAPE_MIN_HEIGHT +
        static_cast<lv_coord_t>(
            velocityNorm * static_cast<float>(shapeMaxHeight - STEP_SHAPE_MIN_HEIGHT)
        )
    );
    const int clampedNudge = std::clamp<int>(nudge, -NUDGE_VISUAL_MAX, NUDGE_VISUAL_MAX);
    const int availableOffset = shapeMaxWidth / 2;
    style.x = static_cast<lv_coord_t>(
        STEP_SHAPE_PAD_X + ((clampedNudge * availableOffset) / NUDGE_VISUAL_MAX)
    );
    style.y = static_cast<lv_coord_t>(buttonHeight - STEP_BAR_HEIGHT - STEP_SHAPE_PAD_Y - style.height);

    if (enabled) {
        style.strokeColor = velocityAccentColor(note, velocity);
        style.strokeOpa = (velocity == 0) ? STEP_SHAPE_OPA_VELOCITY_ZERO : STEP_SHAPE_OPA_ENABLED;
    } else {
        style.strokeColor = grayscaleColor(SHAPE_DISABLED_FILL_BRIGHTNESS);
        style.strokeOpa = STEP_SHAPE_OPA_DISABLED;
    }

    return style;
}

uint16_t divisionDenominator(uint8_t stepsPerBeat) {
    if (stepsPerBeat == 0) return 0;
    return static_cast<uint16_t>(4U * static_cast<uint16_t>(stepsPerBeat));
}

void initStepBar(lv_obj_t* bar) {
    if (!bar) return;

    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, STEP_BAR_WIDTH, STEP_BAR_HEIGHT);
    lv_obj_set_style_radius(bar, STEP_BAR_HEIGHT / 2, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
}

void initStepGuide(lv_obj_t* guide) {
    if (!guide) return;

    lv_obj_clear_flag(guide, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(guide, STEP_GUIDE_WIDTH, STEP_GUIDE_HEIGHT);
    lv_obj_set_style_radius(guide, 0, 0);
    lv_obj_set_style_border_width(guide, 0, 0);
    lv_obj_set_style_bg_color(guide, lv_color_hex(STEP_GUIDE_COLOR), 0);
    lv_obj_set_style_bg_opa(guide, STEP_GUIDE_OPA, 0);
}

void positionStepGuides(lv_obj_t* button, const std::array<lv_obj_t*, STEP_GUIDE_COUNT>& guides) {
    const lv_coord_t railWidth = railWidthForButton(button);
    const lv_coord_t buttonHeight = buttonHeightForButton(button);

    for (uint8_t g = 0; g < STEP_GUIDE_COUNT; ++g) {
        lv_obj_t* guide = guides[g];
        if (!guide) continue;

        lv_obj_set_height(guide, STEP_GUIDE_HEIGHT);
        const lv_coord_t guideX = static_cast<lv_coord_t>(
            STEP_SHAPE_PAD_X +
            std::round(STEP_GUIDE_POSITIONS[g] * static_cast<float>(std::max<lv_coord_t>(0, railWidth - 1)))
        );
        const lv_coord_t guideY = static_cast<lv_coord_t>(
            std::max<lv_coord_t>(0, buttonHeight - STEP_GUIDE_BOTTOM - STEP_GUIDE_HEIGHT)
        );
        lv_obj_align(guide, LV_ALIGN_TOP_LEFT, guideX, guideY);
    }
}

size_t propertyIndex(core::state::sequencer::StepProperty property) {
    return std::min(
        static_cast<size_t>(property),
        STEP_PROPERTY_COUNT - 1
    );
}

}  // namespace

StepGrid::StepGrid(lv_obj_t* parent, core::state::CoreState& coreState)
    : core_state_(coreState) {
    createUI(parent);
    createTiles();
}

StepGrid::~StepGrid() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        grid_ = nullptr;
        note_layer_ = nullptr;
        overlay_layer_ = nullptr;
        division_overlay_label_ = nullptr;
        total_steps_overlay_label_ = nullptr;
        track_overlay_label_ = nullptr;
    }
}

void StepGrid::forceRefresh() {
    geometry_dirty_ = true;
    invalidateTileCaches();
}

void StepGrid::createUI(lv_obj_t* parent) {
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

    overlay_layer_ = lv_obj_create(container_);
    style::apply(overlay_layer_)
        .size(LV_PCT(100), LV_PCT(100))
        .transparent()
        .noBorder()
        .pad(0)
        .noScroll();
    lv_obj_add_flag(overlay_layer_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(overlay_layer_, LV_ALIGN_CENTER, 0, 0);

    division_overlay_label_ = lv_label_create(overlay_layer_);
    lv_obj_add_flag(division_overlay_label_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(division_overlay_label_, LV_ALIGN_TOP_RIGHT, -OVERLAY_PAD_X, OVERLAY_PAD_Y_TOP);
    lv_obj_set_style_text_font(division_overlay_label_, fonts.inter_13_bold, 0);
    lv_obj_set_style_text_color(division_overlay_label_, lv_color_hex(STEP_TEXT_DISABLED_COLOR), 0);
    lv_obj_set_style_text_opa(division_overlay_label_, STEP_TEXT_DISABLED_OPA, 0);

    total_steps_overlay_label_ = lv_label_create(overlay_layer_);
    lv_obj_add_flag(total_steps_overlay_label_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(total_steps_overlay_label_, LV_ALIGN_BOTTOM_RIGHT, -OVERLAY_PAD_X, OVERLAY_PAD_Y_BOTTOM);
    lv_obj_set_style_text_font(total_steps_overlay_label_, fonts.inter_13_bold, 0);
    lv_obj_set_style_text_color(total_steps_overlay_label_, lv_color_hex(STEP_TEXT_DISABLED_COLOR), 0);
    lv_obj_set_style_text_opa(total_steps_overlay_label_, STEP_TEXT_DISABLED_OPA, 0);

    track_overlay_label_ = lv_label_create(overlay_layer_);
    lv_obj_add_flag(track_overlay_label_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_align(track_overlay_label_, LV_ALIGN_BOTTOM_LEFT, OVERLAY_PAD_X, OVERLAY_PAD_Y_BOTTOM);
    lv_obj_set_style_text_font(track_overlay_label_, fonts.inter_13_bold, 0);
    lv_obj_set_style_text_color(track_overlay_label_, lv_color_hex(STEP_TEXT_DISABLED_COLOR), 0);
    lv_obj_set_style_text_opa(track_overlay_label_, STEP_TEXT_DISABLED_OPA, 0);
    lv_label_set_text(track_overlay_label_, "Track 1");

    lv_obj_move_foreground(overlay_layer_);
}

void StepGrid::createTiles() {
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
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
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

        for (size_t p = 0; p < STEP_PROPERTY_COUNT; ++p) {
            const auto property = static_cast<core::state::sequencer::StepProperty>(p);
            lv_obj_t* watermark = lv_label_create(btn);
            step_property_watermarks_[i][p] = watermark;
            standalone::icons::set(
                watermark,
                sequencer::visual::propertyIconGlyph(property),
                standalone::icons::Size::L
            );
            lv_obj_center(watermark);
            lv_obj_set_style_text_color(watermark, lv_color_hex(STEP_PROPERTY_WATERMARK_COLOR), 0);
            lv_obj_set_style_text_opa(watermark, LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_all(watermark, 0, 0);
            lv_obj_add_flag(watermark, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(watermark, LV_ALIGN_CENTER, 0, STEP_PROPERTY_WATERMARK_OFFSET_Y);
        }

        lv_obj_t* valueTrack = lv_obj_create(btn);
        step_property_value_tracks_[i] = valueTrack;
        lv_obj_remove_style_all(valueTrack);
        lv_obj_clear_flag(valueTrack, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(
            valueTrack,
            STEP_PROPERTY_VALUE_TRACK_WIDTH,
            STEP_PROPERTY_VALUE_TRACK_MIN_HEIGHT
        );
        lv_obj_set_style_radius(valueTrack, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(valueTrack, 0, 0);
        lv_obj_set_style_bg_color(valueTrack, lv_color_hex(STEP_PROPERTY_TRACK_COLOR), 0);
        lv_obj_set_style_bg_opa(valueTrack, STEP_TEXT_DISABLED_OPA, 0);
        lv_obj_add_flag(valueTrack, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* valueFill = lv_obj_create(btn);
        step_property_value_fills_[i] = valueFill;
        lv_obj_remove_style_all(valueFill);
        lv_obj_clear_flag(valueFill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(
            valueFill,
            STEP_PROPERTY_VALUE_FILL_WIDTH,
            STEP_PROPERTY_VALUE_TRACK_WIDTH
        );
        lv_obj_set_style_radius(valueFill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(valueFill, 0, 0);
        lv_obj_set_style_bg_color(valueFill, lv_color_hex(STEP_INLINE_NOTE_COLOR), 0);
        lv_obj_set_style_bg_opa(valueFill, LV_OPA_COVER, 0);
        lv_obj_add_flag(valueFill, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* horizontalAccent = lv_obj_create(note_layer_);
        step_property_horizontal_accents_[i] = horizontalAccent;
        lv_obj_remove_style_all(horizontalAccent);
        lv_obj_add_flag(horizontalAccent, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(horizontalAccent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(
            horizontalAccent,
            STEP_SHAPE_MIN_WIDTH,
            STEP_PROPERTY_HORIZONTAL_ACCENT_HEIGHT
        );
        lv_obj_set_style_radius(horizontalAccent, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(horizontalAccent, 0, 0);
        lv_obj_set_style_bg_color(horizontalAccent, lv_color_hex(STEP_INLINE_NOTE_COLOR), 0);
        lv_obj_set_style_bg_opa(horizontalAccent, LV_OPA_COVER, 0);
        lv_obj_add_flag(horizontalAccent, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* edgeTick = lv_obj_create(note_layer_);
        step_property_edge_ticks_[i] = edgeTick;
        lv_obj_remove_style_all(edgeTick);
        lv_obj_add_flag(edgeTick, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_clear_flag(edgeTick, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(edgeTick, STEP_PROPERTY_EDGE_TICK_WIDTH, STEP_PROPERTY_EDGE_TICK_HEIGHT);
        lv_obj_set_style_radius(edgeTick, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(edgeTick, 0, 0);
        lv_obj_set_style_bg_color(edgeTick, lv_color_hex(STEP_INLINE_NOTE_COLOR), 0);
        lv_obj_set_style_bg_opa(edgeTick, LV_OPA_COVER, 0);
        lv_obj_add_flag(edgeTick, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* sel = lv_obj_create(btn);
        step_selectors_[i] = sel;
        initStepBar(sel);
        lv_obj_set_style_bg_color(sel, lv_color_hex(COLOR_STEP_SELECTOR_HEX), 0);
        lv_obj_add_flag(sel, LV_OBJ_FLAG_HIDDEN);

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

        rail_width_cache_[i] = railWidthForButton(button);
        button_height_cache_[i] = buttonHeightForButton(button);
        note_base_x_[i] = static_cast<lv_coord_t>(buttonArea.x1 - noteLayerArea.x1);
        note_base_y_[i] = static_cast<lv_coord_t>(buttonArea.y1 - noteLayerArea.y1);
        note_label_baseline_y_[i] = static_cast<lv_coord_t>(
            buttonArea.y2 - noteLayerArea.y1 - STEP_NOTE_LABEL_PAD_BOTTOM
        );

        positionStepGuides(button, step_guides_[i]);
    }

    geometry_dirty_ = false;
}

StepGrid::TileRenderState StepGrid::readTileRenderState(
    uint8_t absoluteStep,
    uint8_t length,
    uint64_t enabledMask,
    int16_t playhead
) const {
    TileRenderState state;
    state.inPattern = absoluteStep < length;
    state.enabled = state.inPattern ? ((enabledMask & (1ULL << absoluteStep)) != 0) : false;
    state.playing =
        state.inPattern && (playhead >= 0) && (absoluteStep == static_cast<uint8_t>(playhead));

    if (!state.inPattern) {
        return state;
    }

    state.note = core_state_.sequencer.note[absoluteStep];
    state.velocity = core_state_.sequencer.velocity[absoluteStep];
    state.probability = core_state_.sequencer.probability[absoluteStep];
    state.gate = core_state_.sequencer.gate[absoluteStep];
    state.nudge = core_state_.sequencer.nudge[absoluteStep];
    return state;
}

StepGrid::TileRenderDiff StepGrid::diffTileRenderState(
    const TileRenderCache& cache,
    const TileRenderState& state
) {
    TileRenderDiff diff;
    diff.initialized = cache.initialized;
    diff.inPatternChanged = !diff.initialized || cache.inPattern != state.inPattern;
    diff.enabledChanged = !diff.initialized || cache.enabled != state.enabled;
    diff.noteChanged = !diff.initialized || cache.note != state.note;
    diff.velocityChanged = !diff.initialized || cache.velocity != state.velocity;
    diff.probabilityChanged = !diff.initialized || cache.probability != state.probability;
    diff.gateChanged = !diff.initialized || cache.gate != state.gate;
    diff.nudgeChanged = !diff.initialized || cache.nudge != state.nudge;
    diff.velocityZeroChanged =
        !diff.initialized || ((cache.velocity == 0) != (state.velocity == 0));

    const bool baseChanged = diff.inPatternChanged || diff.enabledChanged;
    diff.dataChanged =
        baseChanged ||
        (state.inPattern &&
         (diff.noteChanged || diff.velocityChanged || diff.probabilityChanged ||
          diff.gateChanged || diff.nudgeChanged));
    diff.barChanged = !diff.initialized || diff.inPatternChanged || cache.playing != state.playing;
    return diff;
}

void StepGrid::renderTile(
    uint8_t tileIndex,
    const TileRenderState& state,
    const TileRenderDiff& diff,
    bool propertyVisualChanged
) {
    auto& cache = tile_render_cache_[tileIndex];

    if (!diff.dataChanged && !diff.barChanged && !propertyVisualChanged) {
        return;
    }

    const StepVisualStyle visual = buildStepVisualStyle(
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
    const lv_coord_t noteLabelY = note_label_baseline_y_[tileIndex];
    const auto propertyVisual = sequencer::visual::buildStepPropertyVisual(
        core_state_.sequencer.activeStepProperty.get(),
        {
            .inPattern = state.inPattern,
            .enabled = state.enabled,
            .note = state.note,
            .velocity = state.velocity,
            .probability = state.probability,
            .gate = state.gate,
            .nudge = state.nudge,
        }
    );

    renderTileNoteLabel(
        tileIndex,
        state,
        diff,
        propertyVisualChanged,
        propertyVisual,
        noteBaseX,
        noteLabelY
    );
    renderTilePropertyVisual(
        tileIndex,
        state,
        diff,
        propertyVisualChanged,
        propertyVisual,
        visual.width,
        visual.height,
        noteBaseX,
        noteBaseY
    );

    if (diff.dataChanged) {
        if (step_shapes_[tileIndex]) {
            if (!state.inPattern) {
                if (!diff.initialized || cache.inPattern) {
                    lv_obj_add_flag(step_shapes_[tileIndex], LV_OBJ_FLAG_HIDDEN);
                }
                if (step_markers_[tileIndex]) {
                    lv_obj_add_flag(step_markers_[tileIndex], LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                if (diff.inPatternChanged) {
                    lv_obj_clear_flag(step_shapes_[tileIndex], LV_OBJ_FLAG_HIDDEN);
                }

                if (diff.inPatternChanged || diff.gateChanged || diff.velocityChanged) {
                    lv_obj_set_size(step_shapes_[tileIndex], visual.width, visual.height);
                }

                if (diff.inPatternChanged || diff.nudgeChanged || diff.velocityChanged) {
                    lv_obj_set_pos(step_shapes_[tileIndex], noteBaseX, noteBaseY);
                }

                if (diff.noteChanged || diff.enabledChanged || diff.velocityChanged || diff.velocityZeroChanged) {
                    lv_obj_set_style_border_color(step_shapes_[tileIndex], visual.strokeColor, 0);
                    lv_obj_set_style_border_opa(step_shapes_[tileIndex], visual.strokeOpa, 0);
                }

                if (step_markers_[tileIndex]) {
                    if (diff.inPatternChanged) {
                        lv_obj_clear_flag(step_markers_[tileIndex], LV_OBJ_FLAG_HIDDEN);
                    }

                    if (diff.inPatternChanged || diff.nudgeChanged || diff.velocityChanged) {
                        lv_obj_set_pos(
                            step_markers_[tileIndex],
                            static_cast<lv_coord_t>(
                                noteBaseX + STEP_SHAPE_STROKE_WIDTH / 2 - STEP_MARKER_SIZE / 2
                            ),
                            static_cast<lv_coord_t>(noteBaseY - STEP_MARKER_SIZE / 2)
                        );
                    }

                    if (diff.noteChanged || diff.enabledChanged || diff.velocityChanged || diff.velocityZeroChanged) {
                        lv_obj_set_style_bg_color(
                            step_markers_[tileIndex],
                            state.enabled ? velocityMarkerColor(state.note, state.velocity)
                                          : lv_color_hex(STEP_TEXT_DISABLED_COLOR),
                            0
                        );
                        lv_obj_set_style_bg_opa(
                            step_markers_[tileIndex],
                            state.enabled ? LV_OPA_COVER : STEP_TEXT_DISABLED_OPA,
                            0
                        );
                    }
                }
            }
        }
    }

    if (diff.dataChanged || diff.barChanged) {
        if (step_selectors_[tileIndex]) {
            lv_obj_add_flag(step_selectors_[tileIndex], LV_OBJ_FLAG_HIDDEN);
        }

        if (step_indicators_[tileIndex]) {
            if (state.playing && state.inPattern) {
                lv_obj_clear_flag(step_indicators_[tileIndex], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_opa(step_indicators_[tileIndex], STEP_BAR_ACTIVE_OPA, 0);
            } else {
                lv_obj_add_flag(step_indicators_[tileIndex], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_opa(step_indicators_[tileIndex], LV_OPA_TRANSP, 0);
            }
        }
    }

    cache.initialized = true;
    cache.inPattern = state.inPattern;
    cache.enabled = state.enabled;
    cache.playing = state.playing;
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
                                   const sequencer::visual::StepPropertyVisualSpec& propertyVisual,
                                   lv_coord_t noteBaseX,
                                   lv_coord_t noteLabelY) {
    auto& cache = tile_render_cache_[tileIndex];
    lv_obj_t* noteLabel = note_labels_[tileIndex];
    if (!noteLabel) return;

    if (!propertyVisual.showNoteLabel) {
        lv_obj_add_flag(noteLabel, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (propertyVisualChanged || diff.inPatternChanged) {
        lv_obj_clear_flag(noteLabel, LV_OBJ_FLAG_HIDDEN);
    }

    if (propertyVisualChanged || diff.noteChanged) {
        char buf[16];
        core::state::sequencer::formatStepPropertyValue(
            buf,
            sizeof(buf),
            core::state::sequencer::StepProperty::NOTE,
            state.note,
            state.velocity,
            state.gate,
            state.nudge,
            state.probability
        );
        lv_label_set_text(noteLabel, buf);
        lv_obj_update_layout(noteLabel);
        cache.noteLabelHeight = lv_obj_get_height(noteLabel);
    }

    if (propertyVisualChanged || diff.noteChanged || diff.enabledChanged) {
        lv_obj_set_style_text_color(
            noteLabel,
            state.enabled ? noteLabelColor(state.note)
                          : lv_color_hex(STEP_TEXT_DISABLED_COLOR),
            0
        );
        lv_obj_set_style_text_opa(
            noteLabel,
            state.enabled ? STEP_INLINE_NOTE_OPA : STEP_TEXT_DISABLED_OPA,
            0
        );
    }

    if ((propertyVisualChanged || diff.inPatternChanged || diff.noteChanged) && cache.noteLabelHeight <= 0) {
        lv_obj_update_layout(noteLabel);
        cache.noteLabelHeight = lv_obj_get_height(noteLabel);
    }

    if (propertyVisualChanged || diff.inPatternChanged || diff.noteChanged || diff.nudgeChanged || cache.noteLabelHeight <= 0) {
        lv_obj_set_pos(
            noteLabel,
            static_cast<lv_coord_t>(noteBaseX + STEP_NOTE_LABEL_PAD_X),
            static_cast<lv_coord_t>(noteLabelY - cache.noteLabelHeight)
        );
    }
}

void StepGrid::renderTilePropertyVisual(
    uint8_t tileIndex,
    const TileRenderState& state,
    const TileRenderDiff& diff,
    bool propertyVisualChanged,
    const sequencer::visual::StepPropertyVisualSpec& propertyVisual,
    lv_coord_t shapeWidth,
    lv_coord_t shapeHeight,
    lv_coord_t noteBaseX,
    lv_coord_t noteBaseY
) {
    const size_t activePropertyIndex = propertyIndex(core_state_.sequencer.activeStepProperty.get());
    lv_obj_t* valueTrack = step_property_value_tracks_[tileIndex];
    lv_obj_t* valueFill = step_property_value_fills_[tileIndex];
    lv_obj_t* horizontalAccent = step_property_horizontal_accents_[tileIndex];
    lv_obj_t* edgeTick = step_property_edge_ticks_[tileIndex];
    lv_obj_t* button = step_buttons_[tileIndex];
    if (!valueTrack || !valueFill || !horizontalAccent || !edgeTick || !button) return;

    if (!propertyVisual.showWatermark) {
        for (auto* watermark : step_property_watermarks_[tileIndex]) {
            if (watermark) {
                lv_obj_add_flag(watermark, LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_obj_add_flag(valueTrack, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(valueFill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(horizontalAccent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(edgeTick, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    for (size_t p = 0; p < STEP_PROPERTY_COUNT; ++p) {
        lv_obj_t* watermark = step_property_watermarks_[tileIndex][p];
        if (!watermark) continue;

        if (p == activePropertyIndex) {
            lv_obj_clear_flag(watermark, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_text_color(
                watermark,
                state.enabled ? lv_color_hex(STEP_PROPERTY_WATERMARK_COLOR)
                              : lv_color_hex(STEP_TEXT_DISABLED_COLOR),
                0
            );
            lv_obj_set_style_text_opa(
                watermark,
                state.enabled ? propertyVisual.watermarkOpa
                              : static_cast<lv_opa_t>(propertyVisual.watermarkOpa / 2),
                0
            );
        } else {
            lv_obj_add_flag(watermark, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (!propertyVisual.showHorizontalAccent) {
        lv_obj_add_flag(horizontalAccent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(edgeTick, LV_OBJ_FLAG_HIDDEN);
    } else {
        const lv_color_t accentColor = state.enabled ? noteLabelColor(state.note)
                                                     : lv_color_hex(STEP_TEXT_DISABLED_COLOR);
        const lv_opa_t accentOpa = state.enabled ? LV_OPA_COVER : STEP_TEXT_DISABLED_OPA;
        const lv_coord_t accentY = static_cast<lv_coord_t>(
            noteBaseY + shapeHeight - STEP_PROPERTY_HORIZONTAL_ACCENT_HEIGHT
        );

        lv_obj_clear_flag(horizontalAccent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(horizontalAccent, shapeWidth, STEP_PROPERTY_HORIZONTAL_ACCENT_HEIGHT);
        lv_obj_set_pos(horizontalAccent, noteBaseX, accentY);
        lv_obj_set_style_bg_color(horizontalAccent, accentColor, 0);
        lv_obj_set_style_bg_opa(horizontalAccent, accentOpa, 0);

        if (propertyVisual.edgeTickMode == sequencer::visual::PropertyEdgeTickMode::NONE) {
            lv_obj_add_flag(edgeTick, LV_OBJ_FLAG_HIDDEN);
        } else {
            const lv_coord_t tickX =
                propertyVisual.edgeTickMode == sequencer::visual::PropertyEdgeTickMode::END
                    ? static_cast<lv_coord_t>(noteBaseX + shapeWidth - STEP_PROPERTY_EDGE_TICK_WIDTH)
                    : noteBaseX;
            const lv_coord_t tickY = static_cast<lv_coord_t>(
                accentY - (STEP_PROPERTY_EDGE_TICK_HEIGHT - STEP_PROPERTY_HORIZONTAL_ACCENT_HEIGHT)
            );

            lv_obj_clear_flag(edgeTick, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(edgeTick, STEP_PROPERTY_EDGE_TICK_WIDTH, STEP_PROPERTY_EDGE_TICK_HEIGHT);
            lv_obj_set_pos(edgeTick, tickX, tickY);
            lv_obj_set_style_bg_color(edgeTick, accentColor, 0);
            lv_obj_set_style_bg_opa(edgeTick, accentOpa, 0);
        }
    }

    if (propertyVisual.valueBarMode == sequencer::visual::PropertyValueBarMode::NONE) {
        lv_obj_add_flag(valueTrack, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(valueFill, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const lv_coord_t trackHeight = std::max<lv_coord_t>(
        STEP_PROPERTY_VALUE_TRACK_MIN_HEIGHT,
        static_cast<lv_coord_t>(button_height_cache_[tileIndex] - STEP_PROPERTY_VALUE_TRACK_INSET_Y * 2)
    );
    const lv_coord_t trackX = STEP_PROPERTY_VALUE_TRACK_INSET_X;
    const lv_coord_t trackY = static_cast<lv_coord_t>((button_height_cache_[tileIndex] - trackHeight) / 2);
    const lv_color_t valueColor = state.enabled ? noteLabelColor(state.note)
                                                : lv_color_hex(STEP_TEXT_DISABLED_COLOR);
    const lv_opa_t valueOpa = state.enabled ? LV_OPA_COVER : STEP_TEXT_DISABLED_OPA;

    lv_obj_clear_flag(valueTrack, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(valueTrack, STEP_PROPERTY_VALUE_TRACK_WIDTH, trackHeight);
    lv_obj_set_pos(valueTrack, trackX, trackY);
    lv_obj_set_style_bg_color(valueTrack, lv_color_hex(STEP_PROPERTY_TRACK_COLOR), 0);
    lv_obj_set_style_bg_opa(valueTrack, STEP_TEXT_DISABLED_OPA, 0);

    const lv_coord_t fillHeight = std::max<lv_coord_t>(
        STEP_PROPERTY_VALUE_FILL_WIDTH,
        static_cast<lv_coord_t>(
            (static_cast<int32_t>(trackHeight) * static_cast<int32_t>(propertyVisual.valueBarPercent) + 99) / 100
        )
    );

    lv_obj_clear_flag(valueFill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(valueFill, valueColor, 0);
    lv_obj_set_style_bg_opa(valueFill, valueOpa, 0);

    if (propertyVisual.valueBarMode == sequencer::visual::PropertyValueBarMode::UNIPOLAR) {
        const lv_coord_t clampedHeight = std::min(fillHeight, trackHeight);
        lv_obj_set_size(valueFill, STEP_PROPERTY_VALUE_FILL_WIDTH, clampedHeight);
        lv_obj_set_pos(
            valueFill,
            trackX,
            static_cast<lv_coord_t>(trackY + trackHeight - clampedHeight)
        );
        return;
    }

    const lv_coord_t halfHeight = std::max<lv_coord_t>(1, trackHeight / 2);
    const lv_coord_t clampedHeight = std::min(fillHeight, halfHeight);
    const lv_coord_t originY = static_cast<lv_coord_t>(trackY + halfHeight);

    lv_obj_set_size(valueFill, STEP_PROPERTY_VALUE_FILL_WIDTH, clampedHeight);
    lv_obj_set_pos(
        valueFill,
        trackX,
        propertyVisual.valueBarPositive
            ? static_cast<lv_coord_t>(originY - clampedHeight)
            : originY
    );
}

void StepGrid::render() {
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;

    if (geometry_dirty_) {
        refreshStaticGeometry();
    }

    const auto property = core_state_.sequencer.activeStepProperty.get();
    const bool propertyVisualChanged = property != cached_property_;
    cached_property_ = property;

    const uint8_t len = core_state_.sequencer.length.get();
    const uint8_t page = core_state_.sequencer.normalizePage(core_state_.sequencer.page.get());
    const uint8_t startStep = core_state_.sequencer.pageStartStep(page);
    const uint64_t mask = core_state_.sequencer.enabledMask.get();
    const int16_t playhead = core_state_.sequencer.playheadStep.get();

    const uint16_t denom = divisionDenominator(core_state_.sequencer.stepsPerBeat.get());
    if (division_overlay_label_ && denom != cached_division_denom_) {
        char divisionBuf[16];
        if (denom > 0) {
            std::snprintf(divisionBuf, sizeof(divisionBuf), "1/%u", static_cast<unsigned>(denom));
        } else {
            std::snprintf(divisionBuf, sizeof(divisionBuf), "?");
        }
        lv_label_set_text(division_overlay_label_, divisionBuf);
        cached_division_denom_ = denom;
    }

    if (total_steps_overlay_label_ && len != cached_total_steps_) {
        char stepsBuf[24];
        std::snprintf(stepsBuf, sizeof(stepsBuf), "%u steps", static_cast<unsigned>(len));
        lv_label_set_text(total_steps_overlay_label_, stepsBuf);
        cached_total_steps_ = len;
    }

    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        const TileRenderState state = readTileRenderState(static_cast<uint8_t>(startStep + i), len, mask, playhead);
        const TileRenderDiff diff = diffTileRenderState(tile_render_cache_[i], state);
        renderTile(i, state, diff, propertyVisualChanged);
    }
}

}  // namespace core::ui

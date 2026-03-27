#include "SequencerView.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <ms/ui/font/CoreFonts.hpp>

#include "state/sequencer/StepPropertyDisplay.hpp"
#include "ui/sequencer/StepVisualUtils.hpp"

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace core::ui {

namespace {

constexpr uint32_t COLOR_STEP_PLAY_HEX = 0x5CA8EE;
constexpr uint32_t COLOR_STEP_SELECTOR_HEX = theme::color::TEXT_PRIMARY;

constexpr uint8_t CHROMATIC_NOTE_COUNT = 12;

constexpr lv_coord_t STEP_BUTTON_SIZE = 56;
constexpr lv_coord_t STEP_SHAPE_PAD_X = 0;
constexpr lv_coord_t STEP_SHAPE_PAD_Y = 1;
constexpr lv_coord_t STEP_SHAPE_RADIUS = 0;
constexpr lv_coord_t STEP_SHAPE_STROKE_WIDTH = 2;
constexpr lv_coord_t STEP_SHAPE_MIN_WIDTH = 6;
constexpr lv_coord_t STEP_SHAPE_MIN_HEIGHT = 18;
constexpr lv_coord_t STEP_GUIDE_WIDTH = 1;
constexpr lv_coord_t STEP_GUIDE_TOP = 7;
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
constexpr lv_opa_t STEP_BAR_ACTIVE_OPA = LV_OPA_COVER;
constexpr lv_coord_t STEP_GUIDE_BOTTOM = STEP_BAR_HEIGHT + 5;

constexpr uint8_t SHAPE_DISABLED_FILL_BRIGHTNESS = 20;

constexpr lv_opa_t STEP_SHAPE_OPA_ENABLED = LV_OPA_COVER;
constexpr lv_opa_t STEP_SHAPE_OPA_DISABLED = LV_OPA_COVER;
constexpr lv_opa_t STEP_SHAPE_OPA_VELOCITY_ZERO = LV_OPA_COVER;

constexpr uint8_t VELOCITY_MAX = 127;
constexpr int8_t NUDGE_VISUAL_MAX = 50;

constexpr lv_coord_t HORIZONTAL_INSET = theme::layout::MARGIN_XS;
constexpr lv_coord_t OVERLAY_PAD_X = theme::layout::MARGIN_XS;
constexpr lv_coord_t OVERLAY_PAD_Y_TOP = theme::layout::MARGIN_SM;
constexpr lv_coord_t OVERLAY_PAD_Y_BOTTOM = -theme::layout::MARGIN_SM;
constexpr lv_coord_t OVERLAY_SAFE_TOP = theme::layout::MARGIN_SM * 3;
constexpr lv_coord_t OVERLAY_SAFE_BOTTOM = theme::layout::MARGIN_SM * 3;
constexpr uint32_t STEP_TEXT_DISABLED_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_TEXT_DISABLED_OPA = static_cast<lv_opa_t>(theme::opacity::OPA_50);
constexpr uint32_t STEP_GUIDE_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_GUIDE_OPA = LV_OPA_50;
constexpr uint8_t VELOCITY_ZERO_FILL_BRIGHTNESS = 36;
constexpr uint32_t STEP_INLINE_NOTE_COLOR = theme::color::TEXT_PRIMARY;
constexpr lv_opa_t STEP_INLINE_NOTE_OPA = LV_OPA_COVER;

constexpr uint32_t darkenHexColor(uint32_t color, uint8_t darknessPct) {
    const uint32_t keepPct = (darknessPct >= 100U) ? 0U : (100U - darknessPct);
    const uint32_t r = ((color >> 16) & 0xFFU) * keepPct / 100U;
    const uint32_t g = ((color >> 8) & 0xFFU) * keepPct / 100U;
    const uint32_t b = (color & 0xFFU) * keepPct / 100U;
    return (r << 16) | (g << 8) | b;
}

constexpr uint32_t lightenHexColor(uint32_t color, uint8_t lightnessPct) {
    const uint32_t pct = std::min<uint32_t>(lightnessPct, 100U);
    const uint32_t r = ((color >> 16) & 0xFFU);
    const uint32_t g = ((color >> 8) & 0xFFU);
    const uint32_t b = (color & 0xFFU);
    const uint32_t outR = r + ((255U - r) * pct) / 100U;
    const uint32_t outG = g + ((255U - g) * pct) / 100U;
    const uint32_t outB = b + ((255U - b) * pct) / 100U;
    return (outR << 16) | (outG << 8) | outB;
}

constexpr uint32_t blendHexColor(uint32_t a, uint32_t b, uint8_t mixPctTowardsB) {
    const uint32_t pct = std::min<uint32_t>(mixPctTowardsB, 100U);
    const uint32_t invPct = 100U - pct;
    const uint32_t aR = (a >> 16) & 0xFFU;
    const uint32_t aG = (a >> 8) & 0xFFU;
    const uint32_t aB = a & 0xFFU;
    const uint32_t bR = (b >> 16) & 0xFFU;
    const uint32_t bG = (b >> 8) & 0xFFU;
    const uint32_t bB = b & 0xFFU;
    const uint32_t outR = (aR * invPct + bR * pct) / 100U;
    const uint32_t outG = (aG * invPct + bG * pct) / 100U;
    const uint32_t outB = (aB * invPct + bB * pct) / 100U;
    return (outR << 16) | (outG << 8) | outB;
}

constexpr uint32_t CHROMATIC_NOTE_BASE_PALETTE_HEX[] = {
    0xF4F1DE,  // C
    0xEAB69E,  // C#
    0xE07A5F,  // D
    0xAA675E,  // D#
    0x73535C,  // E
    0x3D405B,  // F
    0x5F797A,  // F#
    0x81B29A,  // G
    0xBABF94,  // G#
    0xF2CC8F,  // A
    0xF3D8A9,  // A#
    0xF3E5C4,  // B
};

// Pitch visuals: fill color by chromatic class (12 semitones).
constexpr uint32_t CHROMATIC_NOTE_PALETTE_HEX[] = {
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[0], 52),   // C
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[1], 58),   // C#
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[2], 50),   // D
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[3], 48),   // D#
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[4], 45),   // E
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[5], 30),   // F
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[6], 38),   // F#
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[7], 45),   // G
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[8], 52),   // G#
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[9], 50),   // A
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[10], 56),  // A#
    darkenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[11], 58),  // B
};

constexpr uint32_t CHROMATIC_NOTE_LABEL_PALETTE_HEX[] = {
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[0], 18),   // C
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[1], 18),   // C#
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[2], 18),   // D
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[3], 18),   // D#
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[4], 28),   // E
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[5], 45),   // F
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[6], 28),   // F#
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[7], 22),   // G
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[8], 18),   // G#
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[9], 18),   // A
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[10], 18),  // A#
    lightenHexColor(CHROMATIC_NOTE_BASE_PALETTE_HEX[11], 18),  // B
};

constexpr uint32_t CHROMATIC_NOTE_MARKER_PALETTE_HEX[] = {
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[0], CHROMATIC_NOTE_LABEL_PALETTE_HEX[0], 35),   // C
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[1], CHROMATIC_NOTE_LABEL_PALETTE_HEX[1], 35),   // C#
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[2], CHROMATIC_NOTE_LABEL_PALETTE_HEX[2], 35),   // D
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[3], CHROMATIC_NOTE_LABEL_PALETTE_HEX[3], 35),   // D#
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[4], CHROMATIC_NOTE_LABEL_PALETTE_HEX[4], 35),   // E
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[5], CHROMATIC_NOTE_LABEL_PALETTE_HEX[5], 35),   // F
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[6], CHROMATIC_NOTE_LABEL_PALETTE_HEX[6], 35),   // F#
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[7], CHROMATIC_NOTE_LABEL_PALETTE_HEX[7], 35),   // G
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[8], CHROMATIC_NOTE_LABEL_PALETTE_HEX[8], 35),   // G#
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[9], CHROMATIC_NOTE_LABEL_PALETTE_HEX[9], 35),   // A
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[10], CHROMATIC_NOTE_LABEL_PALETTE_HEX[10], 35), // A#
    blendHexColor(CHROMATIC_NOTE_PALETTE_HEX[11], CHROMATIC_NOTE_LABEL_PALETTE_HEX[11], 35), // B
};

uint8_t chromaIndexForNote(uint8_t note) {
    return static_cast<uint8_t>(note % CHROMATIC_NOTE_COUNT);
}

lv_color_t noteStrokeColor(uint8_t note) {
    return lv_color_hex(CHROMATIC_NOTE_PALETTE_HEX[chromaIndexForNote(note)]);
}

lv_color_t noteLabelColor(uint8_t note) {
    return lv_color_hex(CHROMATIC_NOTE_LABEL_PALETTE_HEX[chromaIndexForNote(note)]);
}

lv_color_t noteMarkerColor(uint8_t note) {
    return lv_color_hex(CHROMATIC_NOTE_MARKER_PALETTE_HEX[chromaIndexForNote(note)]);
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
        if (velocity == 0) {
            style.strokeColor = grayscaleColor(VELOCITY_ZERO_FILL_BRIGHTNESS);
            style.strokeOpa = STEP_SHAPE_OPA_VELOCITY_ZERO;
        } else {
            style.strokeColor = noteStrokeColor(note);
            style.strokeOpa = STEP_SHAPE_OPA_ENABLED;
        }
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
    lv_obj_set_size(guide, STEP_GUIDE_WIDTH, STEP_BUTTON_SIZE - STEP_GUIDE_TOP - STEP_GUIDE_BOTTOM);
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

        lv_obj_set_height(
            guide,
            std::max<lv_coord_t>(2, buttonHeight - STEP_GUIDE_TOP - STEP_GUIDE_BOTTOM)
        );
        const lv_coord_t guideX = static_cast<lv_coord_t>(
            STEP_SHAPE_PAD_X +
            std::round(STEP_GUIDE_POSITIONS[g] * static_cast<float>(std::max<lv_coord_t>(0, railWidth - 1)))
        );
        lv_obj_align(guide, LV_ALIGN_TOP_LEFT, guideX, STEP_GUIDE_TOP);
    }
}

}  // namespace

SequencerView::SequencerView(lv_obj_t* parent, core::state::CoreState& coreState)
    : core_state_(coreState) {
    createLayout(parent);
    createHeaderBar();
    createSteps();
    bindToState();
}

SequencerView::~SequencerView() {
    if (render_timer_) {
        lv_timer_delete(render_timer_);
        render_timer_ = nullptr;
    }

    header_bar_.reset();
    layout_.reset();
    container_ = nullptr;
    body_container_ = nullptr;
    note_layer_ = nullptr;
    overlay_layer_ = nullptr;
    division_overlay_label_ = nullptr;
    total_steps_overlay_label_ = nullptr;
    track_overlay_label_ = nullptr;
}

void SequencerView::onActivate() {
    if (container_) {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);

        if (!render_timer_) {
            render_timer_ = lv_timer_create(onRenderTimer, 16, this);
        }

        geometry_dirty_ = true;
        invalidateTileCaches();
        render();

        // Force one post-layout pass after the first visible render so note-layer
        // objects use final coordinates on activation.
        lv_obj_update_layout(body_container_);
        geometry_dirty_ = true;
        invalidateTileCaches();
        render();

        dirty_ = false;
    }
}

void SequencerView::onDeactivate() {
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }

    if (render_timer_) {
        lv_timer_delete(render_timer_);
        render_timer_ = nullptr;
    }
}

void SequencerView::onGeometryChangedEvent(lv_event_t* event) {
    auto* self = static_cast<SequencerView*>(lv_event_get_user_data(event));
    if (!self) return;
    self->markGeometryDirty();
}

void SequencerView::markGeometryDirty() {
    geometry_dirty_ = true;
    requestRender();
}

void SequencerView::invalidateTileCaches() {
    for (auto& cache : tile_render_cache_) {
        cache.initialized = false;
    }
}

void SequencerView::refreshStaticGeometry() {
    if (!note_layer_ || !body_container_) return;

    lv_obj_update_layout(body_container_);

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

void SequencerView::createLayout(lv_obj_t* parent) {
    layout_ = std::make_unique<ms::ui::LayoutView>(parent);
    container_ = layout_->getElement();
    body_container_ = layout_->content();

    // Body styling: no extra padding, full main zone usage
    style::apply(body_container_).transparent().pad(0);
    lv_obj_set_layout(body_container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body_container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(body_container_, 0, 0);
}

void SequencerView::createHeaderBar() {
    if (!layout_) return;
    header_bar_ = std::make_unique<SequencerHeaderBar>(layout_->header());
}

void SequencerView::createSteps() {
    grid_ = lv_obj_create(body_container_);
    style::apply(grid_).size(LV_PCT(100), LV_PCT(100)).transparent().noBorder();
    lv_obj_set_flex_grow(grid_, 1);

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

    note_layer_ = lv_obj_create(body_container_);
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

    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        uint8_t col = i % 4;
        uint8_t row = i / 4;

        lv_obj_t* tile = lv_obj_create(grid_);
        tiles_[i] = tile;
        style::apply(tile).transparent().noBorder().pad(0).noScroll();
        lv_obj_add_flag(tile, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_grid_cell(tile,
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);

        lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(tile, 0, 0);

        // Button wrapper stretches the step timing lane to the full tile width.
        lv_obj_t* btnWrap = lv_obj_create(tile);
        lv_obj_remove_style_all(btnWrap);
        lv_obj_clear_flag(btnWrap, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(btnWrap, LV_PCT(100));
        lv_obj_set_flex_grow(btnWrap, 1);
        lv_obj_set_layout(btnWrap, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(btnWrap, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btnWrap, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(btnWrap, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

        // Step button (transparent container + overlays)
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

        // Selector bar: below the rendered step shape
        lv_obj_t* sel = lv_obj_create(btn);
        step_selectors_[i] = sel;
        initStepBar(sel);
        lv_obj_set_style_bg_color(sel, lv_color_hex(COLOR_STEP_SELECTOR_HEX), 0);
        lv_obj_add_flag(sel, LV_OBJ_FLAG_HIDDEN);

        // Playhead bar: above the rendered step shape
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
        lv_obj_set_style_bg_color(ph, lv_color_hex(COLOR_STEP_PLAY_HEX), 0);
        lv_obj_align(ph, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        // Compact note label rendered inside the note block itself.
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

    overlay_layer_ = lv_obj_create(body_container_);
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

void SequencerView::bindToState() {
    watcher_.watchAll(
        [this]() { requestRender(); },
        core_state_.sequencer.length,
        core_state_.sequencer.stepsPerBeat,
        core_state_.sequencer.page,
        core_state_.sequencer.activeStepProperty,
        core_state_.sequencer.enabledMask,
        core_state_.sequencer.focusedStep,
        core_state_.sequencer.playheadStep,
        core_state_.sequencer.stepDataRevision
    );

    render();
    dirty_ = false;
}

void SequencerView::renderHeader(
    uint8_t len,
    uint8_t page,
    uint8_t focused,
    uint64_t enabledMask,
    int16_t playhead,
    core::state::sequencer::StepProperty property
) {
    const bool focusedInPattern = (len > 0) && (focused < len);
    const bool focusedEnabled = focusedInPattern && ((enabledMask & (1ULL << focused)) != 0);

    char focusedValue[16];
    std::snprintf(focusedValue, sizeof(focusedValue), "--");
    if (focusedInPattern) {
        core::state::sequencer::formatStepPropertyValue(
            focusedValue,
            sizeof(focusedValue),
            property,
            core_state_.sequencer.note[focused],
            core_state_.sequencer.velocity[focused],
            core_state_.sequencer.gate[focused],
            core_state_.sequencer.nudge[focused]
        );
    }

    char leftText[24];
    std::snprintf(
        leftText,
        sizeof(leftText),
        "%s",
        core::state::sequencer::stepPropertyName(property)
    );

    char rightText[16];
    if (focusedInPattern) {
        std::snprintf(rightText, sizeof(rightText), "Step %u", static_cast<unsigned>(focused) + 1);
    } else {
        std::snprintf(rightText, sizeof(rightText), "Step --");
    }

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

    if (!header_bar_) return;

    header_bar_->render({
        .length = len,
        .viewedPage = page,
        .playheadStep = playhead,
        .leftText = leftText,
        .centerText = focusedValue,
        .rightText = rightText,
        .dimmed = !focusedEnabled,
    });
}

SequencerView::TileRenderState SequencerView::readTileRenderState(
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
    state.gate = core_state_.sequencer.gate[absoluteStep];
    state.nudge = core_state_.sequencer.nudge[absoluteStep];
    return state;
}

SequencerView::TileRenderDiff SequencerView::diffTileRenderState(
    const TileRenderCache& cache,
    const TileRenderState& state
) {
    TileRenderDiff diff;
    diff.initialized = cache.initialized;
    diff.inPatternChanged = !diff.initialized || cache.inPattern != state.inPattern;
    diff.enabledChanged = !diff.initialized || cache.enabled != state.enabled;
    diff.noteChanged = !diff.initialized || cache.note != state.note;
    diff.velocityChanged = !diff.initialized || cache.velocity != state.velocity;
    diff.gateChanged = !diff.initialized || cache.gate != state.gate;
    diff.nudgeChanged = !diff.initialized || cache.nudge != state.nudge;
    diff.velocityZeroChanged =
        !diff.initialized || ((cache.velocity == 0) != (state.velocity == 0));

    const bool baseChanged = diff.inPatternChanged || diff.enabledChanged;
    diff.dataChanged =
        baseChanged ||
        (state.inPattern && (diff.noteChanged || diff.velocityChanged || diff.gateChanged || diff.nudgeChanged));
    diff.barChanged = !diff.initialized || diff.inPatternChanged || cache.playing != state.playing;
    return diff;
}

void SequencerView::renderTile(uint8_t tileIndex, const TileRenderState& state, const TileRenderDiff& diff) {
    auto& cache = tile_render_cache_[tileIndex];

    if (!diff.dataChanged && !diff.barChanged) {
        return;
    }

    if (diff.dataChanged) {
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

        if (note_labels_[tileIndex]) {
            if (state.inPattern) {
                if (diff.inPatternChanged) {
                    lv_obj_clear_flag(note_labels_[tileIndex], LV_OBJ_FLAG_HIDDEN);
                }

                if (diff.noteChanged) {
                    char buf[16];
                    core::state::sequencer::formatStepPropertyValue(
                        buf,
                        sizeof(buf),
                        core::state::sequencer::StepProperty::NOTE,
                        state.note,
                        state.velocity,
                        state.gate,
                        state.nudge
                    );
                    lv_label_set_text(note_labels_[tileIndex], buf);
                    lv_obj_update_layout(note_labels_[tileIndex]);
                    cache.noteLabelHeight = lv_obj_get_height(note_labels_[tileIndex]);
                }

                if (diff.noteChanged || diff.enabledChanged) {
                    lv_obj_set_style_text_color(
                        note_labels_[tileIndex],
                        state.enabled ? noteLabelColor(state.note)
                                      : lv_color_hex(STEP_TEXT_DISABLED_COLOR),
                        0
                    );
                    lv_obj_set_style_text_opa(
                        note_labels_[tileIndex],
                        state.enabled ? STEP_INLINE_NOTE_OPA : STEP_TEXT_DISABLED_OPA,
                        0
                    );
                }

                if ((diff.inPatternChanged || diff.noteChanged) && cache.noteLabelHeight <= 0) {
                    lv_obj_update_layout(note_labels_[tileIndex]);
                    cache.noteLabelHeight = lv_obj_get_height(note_labels_[tileIndex]);
                }

                if (diff.inPatternChanged || diff.noteChanged || diff.nudgeChanged || cache.noteLabelHeight <= 0) {
                    lv_obj_set_pos(
                        note_labels_[tileIndex],
                        static_cast<lv_coord_t>(noteBaseX + STEP_NOTE_LABEL_PAD_X),
                        static_cast<lv_coord_t>(noteLabelY - cache.noteLabelHeight)
                    );
                }
            } else if (!diff.initialized || cache.inPattern) {
                lv_obj_add_flag(note_labels_[tileIndex], LV_OBJ_FLAG_HIDDEN);
            }
        }

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

                if (diff.noteChanged || diff.enabledChanged || diff.velocityZeroChanged) {
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
                            static_cast<lv_coord_t>(noteBaseX - STEP_MARKER_SIZE / 2),
                            static_cast<lv_coord_t>(noteBaseY - STEP_MARKER_SIZE / 2)
                        );
                    }

                    if (diff.noteChanged || diff.enabledChanged || diff.velocityZeroChanged) {
                        lv_obj_set_style_bg_color(
                            step_markers_[tileIndex],
                            state.enabled ? noteMarkerColor(state.note)
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
    cache.gate = state.gate;
    cache.nudge = state.nudge;
}

void SequencerView::requestRender() {
    dirty_ = true;
}

void SequencerView::onRenderTimer(lv_timer_t* timer) {
    auto* self = static_cast<SequencerView*>(lv_timer_get_user_data(timer));
    if (!self) return;

    if (!self->dirty_) return;
    if (!self->container_) return;
    if (lv_obj_has_flag(self->container_, LV_OBJ_FLAG_HIDDEN)) return;

    self->render();
    self->dirty_ = false;
}

void SequencerView::render() {
    if (!container_) return;
    if (lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;
    if (geometry_dirty_) {
        refreshStaticGeometry();
    }

    const uint8_t len = core_state_.sequencer.length.get();
    const uint8_t page = core_state_.sequencer.normalizePage(core_state_.sequencer.page.get());
    const uint8_t startStep = core_state_.sequencer.pageStartStep(page);
    const uint64_t mask = core_state_.sequencer.enabledMask.get();
    const uint8_t focused = core_state_.sequencer.focusedStep.get();
    const int16_t playhead = core_state_.sequencer.playheadStep.get();
    const auto property = core_state_.sequencer.activeStepProperty.get();

    renderHeader(len, page, focused, mask, playhead, property);

    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        const TileRenderState state = readTileRenderState(static_cast<uint8_t>(startStep + i), len, mask, playhead);
        const TileRenderDiff diff = diffTileRenderState(tile_render_cache_[i], state);
        renderTile(i, state, diff);
    }
}

}  // namespace core::ui

#include "StepGrid.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/type/TextFormat.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/ui/lvgl/StaticSurfaceInvalidation.hpp>
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepGridGeometryLogic.hpp"
#include "ui/sequencer/StepGridLabelRenderer.hpp"
#include "ui/sequencer/StepGridRenderLogic.hpp"
#include "ui/sequencer/StepGridRenderPlanner.hpp"
#include "ui/sequencer/StepGridWidgets.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace theme = oc::ui::lvgl::base_theme;
namespace grid = core::ui::sequencer::grid;

namespace core::ui {

namespace {

constexpr uint32_t COLOR_STEP_PLAY_HEX = 0x5CA8EE;
constexpr uint32_t COLOR_STEP_PLAY_INACTIVE_HEX = theme::color::INACTIVE_LIGHTER;
constexpr lv_coord_t STEP_BAR_HEIGHT = grid::STEP_BAR_HEIGHT;
constexpr lv_opa_t STEP_BAR_ACTIVE_OPA = LV_OPA_COVER;
constexpr lv_opa_t STEP_BAR_INACTIVE_OPA = LV_OPA_70;
constexpr uint32_t STEP_INDEX_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_INDEX_OPA = LV_OPA_60;
constexpr lv_opa_t STEP_PROBABILITY_MASKED_OPA = LV_OPA_30;
constexpr uint32_t STEP_GUIDE_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_GUIDE_OPA = LV_OPA_50;
constexpr lv_coord_t STEP_GUIDE_WIDTH = 1;
constexpr uint8_t STEP_GUIDE_COUNT = 3;
constexpr uint32_t STEP_SELECTION_CURSOR_COLOR = 0xF4F1DE;
constexpr uint32_t STEP_SELECTION_SELECTED_COLOR = COLOR_STEP_PLAY_HEX;
constexpr uint32_t STEP_SELECTION_EMPTY_COLOR = 0x81B29A;
constexpr uint32_t STEP_SELECTION_GHOST_COLOR = 0xF2CC8F;
constexpr uint32_t STEP_SELECTION_OVERWRITE_COLOR = 0xEAB69E;
constexpr uint32_t STEP_SELECTION_BLOCKED_COLOR = 0xE07A5F;
constexpr lv_opa_t STEP_SELECTION_SELECTED_OPA = LV_OPA_20;
constexpr lv_opa_t STEP_SELECTION_PREVIEW_OPA = LV_OPA_30;
constexpr lv_coord_t STEP_SELECTION_CURSOR_BORDER = 2;
constexpr lv_coord_t STEP_SELECTION_SELECTED_BORDER = 1;
constexpr lv_coord_t STEP_SELECTION_BORDER_OUTSET = 2;
constexpr lv_coord_t STEP_INDEX_RIGHT_PAD = 4;
constexpr lv_coord_t STEP_INDEX_TOP_PAD = 2;
constexpr uint32_t VARIATION_RANGE_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr uint32_t VARIATION_DELTA_COLOR = COLOR_STEP_PLAY_HEX;
constexpr lv_opa_t VARIATION_RANGE_OPA = LV_OPA_70;
constexpr lv_opa_t VARIATION_DELTA_OPA = LV_OPA_COVER;
constexpr lv_coord_t VARIATION_RANGE_TICK_LENGTH = 7;
constexpr lv_coord_t VARIATION_RANGE_TICK_THICKNESS = 2;
constexpr lv_coord_t STEP_SHAPE_STROKE_WIDTH = 2;
constexpr lv_coord_t VARIATION_DELTA_THICKNESS = STEP_SHAPE_STROKE_WIDTH;
constexpr lv_coord_t STEP_BADGE_SIZE = 12;
constexpr lv_coord_t STEP_BADGE_GAP = 2;
constexpr lv_coord_t STEP_BADGE_LEFT_PAD = 4;
constexpr lv_coord_t STEP_BADGE_TOP_PAD = 3;
constexpr uint32_t MICRO_SEQUENCE_BADGE_COLOR =
    sequencer::semantic::color(sequencer::semantic::Tone::MICRO_SEQUENCE);
constexpr uint32_t CYCLE_STATE_BADGE_COLOR =
    sequencer::semantic::color(sequencer::semantic::Tone::CYCLE_STATE);
constexpr uint32_t CHORD_BADGE_COLOR =
    sequencer::semantic::color(sequencer::semantic::Tone::CHORD);
constexpr uint32_t PROBABILITY_BADGE_COLOR =
    sequencer::semantic::color(sequencer::semantic::Tone::CHANCE);
constexpr lv_opa_t STEP_BADGE_OPA = LV_OPA_COVER;
constexpr lv_opa_t STEP_BADGE_DISABLED_OPA = LV_OPA_50;

uint32_t chordBadgeColor(
    oc::note::sequencer::StepSequencerChordSource source
) {
    using Source = oc::note::sequencer::StepSequencerChordSource;
    if (source == Source::Inherited) {
        return sequencer::semantic::color(sequencer::semantic::Tone::CHORD_MODE);
    }
    return CHORD_BADGE_COLOR;
}

uint8_t clampMidiValue(int value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return static_cast<uint8_t>(value);
}

uint16_t clampGateValue(int value) {
    if (value < 0) return 0;
    if (value > static_cast<int>(core::state::sequencer::SequencerState::MAX_GATE_PERCENT)) {
        return core::state::sequencer::SequencerState::MAX_GATE_PERCENT;
    }
    return static_cast<uint16_t>(value);
}

int8_t clampNudgeValue(int value) {
    if (value < -50) return -50;
    if (value > 50) return 50;
    return static_cast<int8_t>(value);
}

void drawVariationRect(lv_layer_t* layer,
                       lv_coord_t x,
                       lv_coord_t y,
                       lv_coord_t width,
                       lv_coord_t height,
                       uint32_t colorHex,
                       lv_opa_t opa) {
    if (!layer || width <= 0 || height <= 0 || opa == LV_OPA_TRANSP) return;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(colorHex);
    dsc.bg_opa = opa;
    dsc.radius = 0;
    dsc.border_width = 0;

    const lv_area_t area{
        .x1 = x,
        .y1 = y,
        .x2 = static_cast<lv_coord_t>(x + width - 1),
        .y2 = static_cast<lv_coord_t>(y + height - 1),
    };
    lv_draw_rect(layer, &dsc, &area);
}

uint32_t stepPastePreviewColor(core::state::sequencer::SequencerStepPastePreview preview) {
    switch (preview) {
        case core::state::sequencer::SequencerStepPastePreview::EMPTY:
            return STEP_SELECTION_EMPTY_COLOR;
        case core::state::sequencer::SequencerStepPastePreview::GHOST:
            return STEP_SELECTION_GHOST_COLOR;
        case core::state::sequencer::SequencerStepPastePreview::OVERWRITE:
            return STEP_SELECTION_OVERWRITE_COLOR;
        case core::state::sequencer::SequencerStepPastePreview::BLOCKED:
            return STEP_SELECTION_BLOCKED_COLOR;
        case core::state::sequencer::SequencerStepPastePreview::NONE:
        default:
            return STEP_SELECTION_CURSOR_COLOR;
    }
}

void drawSelectionOverlay(lv_layer_t* layer,
                          const lv_area_t& buttonArea,
                          const grid::TileRenderCache& cache) {
    if (!layer || !cache.stepSelectionActive) return;

    if (cache.stepPastePreviewActive) {
        const uint32_t color = stepPastePreviewColor(cache.stepPastePreview);
        lv_draw_rect_dsc_t fillDsc;
        lv_draw_rect_dsc_init(&fillDsc);
        fillDsc.bg_color = lv_color_hex(color);
        fillDsc.bg_opa = STEP_SELECTION_PREVIEW_OPA;
        fillDsc.radius = 0;
        fillDsc.border_width = 0;
        lv_draw_rect(layer, &fillDsc, &buttonArea);
    } else if (cache.stepSelectionSelected) {
        const uint32_t color = STEP_SELECTION_SELECTED_COLOR;
        lv_draw_rect_dsc_t fillDsc;
        lv_draw_rect_dsc_init(&fillDsc);
        fillDsc.bg_color = lv_color_hex(color);
        fillDsc.bg_opa = STEP_SELECTION_SELECTED_OPA;
        fillDsc.radius = 0;
        fillDsc.border_width = 0;
        lv_draw_rect(layer, &fillDsc, &buttonArea);
    }

    lv_area_t borderArea = buttonArea;
    borderArea.x1 = static_cast<lv_coord_t>(borderArea.x1 - STEP_SELECTION_BORDER_OUTSET);
    borderArea.y1 = static_cast<lv_coord_t>(borderArea.y1 - STEP_SELECTION_BORDER_OUTSET);
    borderArea.x2 = static_cast<lv_coord_t>(borderArea.x2 + STEP_SELECTION_BORDER_OUTSET);
    borderArea.y2 = static_cast<lv_coord_t>(borderArea.y2 + STEP_SELECTION_BORDER_OUTSET);

    if (cache.stepSelectionSelected) {
        lv_draw_rect_dsc_t borderDsc;
        lv_draw_rect_dsc_init(&borderDsc);
        borderDsc.bg_opa = LV_OPA_TRANSP;
        borderDsc.radius = 0;
        borderDsc.border_width = STEP_SELECTION_SELECTED_BORDER;
        borderDsc.border_color = lv_color_hex(STEP_SELECTION_SELECTED_COLOR);
        borderDsc.border_opa = LV_OPA_80;
        lv_draw_rect(layer, &borderDsc, &borderArea);
    }

    if (cache.stepSelectionCursor) {
        const uint32_t color = cache.stepPastePreviewActive
            ? stepPastePreviewColor(cache.stepPastePreview)
            : STEP_SELECTION_CURSOR_COLOR;
        lv_draw_rect_dsc_t borderDsc;
        lv_draw_rect_dsc_init(&borderDsc);
        borderDsc.bg_opa = LV_OPA_TRANSP;
        borderDsc.radius = 0;
        borderDsc.border_width = STEP_SELECTION_CURSOR_BORDER;
        borderDsc.border_color = lv_color_hex(color);
        borderDsc.border_opa = LV_OPA_COVER;

        lv_area_t cursorArea = borderArea;
        cursorArea.y2 = static_cast<lv_coord_t>(
            std::max<lv_coord_t>(
                cursorArea.y1,
                static_cast<lv_coord_t>(buttonArea.y2 - STEP_BAR_HEIGHT - 1)
            )
        );
        lv_draw_rect(layer, &borderDsc, &cursorArea);
    }
}

lv_coord_t drawStepBadgeGlyph(lv_layer_t* layer,
                              const lv_area_t& buttonArea,
                              lv_coord_t x,
                              lv_coord_t y,
                              const char* icon,
                              uint32_t colorHex,
                              bool enabled) {
    if (!layer || icon == nullptr || icon[0] == '\0') return x;
    if (x + STEP_BADGE_SIZE - 1 > buttonArea.x2) return x;

    const lv_area_t area{
        .x1 = x,
        .y1 = y,
        .x2 = static_cast<lv_coord_t>(x + STEP_BADGE_SIZE - 1),
        .y2 = static_cast<lv_coord_t>(y + STEP_BADGE_SIZE - 1),
    };

    lv_draw_label_dsc_t labelDsc;
    lv_draw_label_dsc_init(&labelDsc);
    labelDsc.text = icon;
    labelDsc.font = standalone_fonts.icons_12;
    labelDsc.color = lv_color_hex(colorHex);
    labelDsc.opa = enabled ? STEP_BADGE_OPA : STEP_BADGE_DISABLED_OPA;
    labelDsc.align = LV_TEXT_ALIGN_CENTER;
    lv_draw_label(layer, &labelDsc, &area);
    return static_cast<lv_coord_t>(x + STEP_BADGE_SIZE + STEP_BADGE_GAP);
}

void drawSemanticBadges(lv_layer_t* layer,
                        const lv_area_t& buttonArea,
                        const grid::TileRenderCache& cache) {
    const bool probabilityBadge = cache.enabled && cache.probability < 100;
    const bool chordBadge = cache.contentBadges.chord &&
                            cache.contentBadges.chordVoiceCount > 1;
    if (!cache.contentBadges.microSequence &&
        !cache.contentBadges.cycleStates &&
        !chordBadge &&
        !probabilityBadge) {
        return;
    }

    lv_coord_t x = static_cast<lv_coord_t>(buttonArea.x1 + STEP_BADGE_LEFT_PAD);
    const lv_coord_t y = static_cast<lv_coord_t>(buttonArea.y1 + STEP_BADGE_TOP_PAD);
    if (cache.contentBadges.cycleStates) {
        x = drawStepBadgeGlyph(
            layer,
            buttonArea,
            x,
            y,
            standalone::icons::CYCLE_STATE,
            CYCLE_STATE_BADGE_COLOR,
            cache.enabled
        );
    }
    if (cache.contentBadges.microSequence) {
        x = drawStepBadgeGlyph(
            layer,
            buttonArea,
            x,
            y,
            standalone::icons::MICRO_SEQUENCE,
            MICRO_SEQUENCE_BADGE_COLOR,
            cache.enabled
        );
    }
    if (chordBadge) {
        x = drawStepBadgeGlyph(
            layer,
            buttonArea,
            x,
            y,
            standalone::icons::CHORD,
            chordBadgeColor(cache.contentBadges.chordSource),
            cache.enabled
        );
    }
    if (probabilityBadge) {
        drawStepBadgeGlyph(
            layer,
            buttonArea,
            x,
            y,
            standalone::icons::NOTE_PROP_RANDOM,
            PROBABILITY_BADGE_COLOR,
            cache.enabled
        );
    }
}

void drawHorizontalRangeTick(lv_layer_t* layer, lv_coord_t centerX, lv_coord_t y) {
    drawVariationRect(
        layer,
        static_cast<lv_coord_t>(centerX - VARIATION_RANGE_TICK_LENGTH / 2),
        y,
        VARIATION_RANGE_TICK_LENGTH,
        VARIATION_RANGE_TICK_THICKNESS,
        VARIATION_RANGE_COLOR,
        VARIATION_RANGE_OPA
    );
}

void drawVerticalRangeTick(lv_layer_t* layer, lv_coord_t x, lv_coord_t centerY) {
    drawVariationRect(
        layer,
        x,
        static_cast<lv_coord_t>(centerY - VARIATION_RANGE_TICK_LENGTH / 2),
        VARIATION_RANGE_TICK_THICKNESS,
        VARIATION_RANGE_TICK_LENGTH,
        VARIATION_RANGE_COLOR,
        VARIATION_RANGE_OPA
    );
}

using ResolvedVariation = oc::note::sequencer::StepSequencerResolvedVariation;
using StepProperty = core::state::sequencer::StepProperty;

void drawVelocityVariation(lv_layer_t* layer,
                           const lv_area_t& buttonArea,
                           lv_coord_t railWidth,
                           lv_coord_t buttonHeight,
                           const ResolvedVariation& variation,
                           bool rangeVisible,
                           bool deltaVisible) {
    if (variation.ranges.velocity == 0) return;

    const auto base = grid::buildStepVisualStyle(
        variation.base.note,
        variation.base.velocity,
        variation.base.gate,
        variation.base.nudge,
        true,
        railWidth,
        buttonHeight
    );
    const auto resolved = grid::buildStepVisualStyle(
        variation.base.note,
        variation.resolved.velocity,
        variation.resolved.gate,
        variation.resolved.nudge,
        true,
        railWidth,
        buttonHeight
    );
    const auto low = grid::buildStepVisualStyle(
        variation.base.note,
        clampMidiValue(static_cast<int>(variation.base.velocity) - variation.ranges.velocity),
        variation.base.gate,
        variation.base.nudge,
        true,
        railWidth,
        buttonHeight
    );
    const auto high = grid::buildStepVisualStyle(
        variation.base.note,
        clampMidiValue(static_cast<int>(variation.base.velocity) + variation.ranges.velocity),
        variation.base.gate,
        variation.base.nudge,
        true,
        railWidth,
        buttonHeight
    );

    const lv_coord_t x = static_cast<lv_coord_t>(buttonArea.x1 + base.x);
    const lv_coord_t rangeY = static_cast<lv_coord_t>(buttonArea.y1 + high.y);
    const lv_coord_t rangeBottom = static_cast<lv_coord_t>(buttonArea.y1 + low.y + low.height - 1);
    const lv_coord_t baseTop = static_cast<lv_coord_t>(buttonArea.y1 + base.y);
    const lv_coord_t resolvedTop = static_cast<lv_coord_t>(buttonArea.y1 + resolved.y);

    if (rangeVisible) {
        drawHorizontalRangeTick(layer, x, rangeY);
        drawHorizontalRangeTick(layer, x, rangeBottom);
    }
    if (deltaVisible && variation.velocityDelta != 0) {
        drawVariationRect(
            layer,
            x,
            std::min(baseTop, resolvedTop),
            VARIATION_DELTA_THICKNESS,
            static_cast<lv_coord_t>(std::abs(baseTop - resolvedTop) + 1),
            VARIATION_DELTA_COLOR,
            VARIATION_DELTA_OPA
        );
    }
}

void drawGateVariation(lv_layer_t* layer,
                       const lv_area_t& buttonArea,
                       lv_coord_t railWidth,
                       lv_coord_t buttonHeight,
                       const ResolvedVariation& variation,
                       bool rangeVisible,
                       bool deltaVisible) {
    if (variation.ranges.gatePercent == 0) return;

    const auto base = grid::buildStepVisualStyle(
        variation.base.note,
        variation.base.velocity,
        variation.base.gate,
        variation.base.nudge,
        true,
        railWidth,
        buttonHeight
    );
    const auto resolved = grid::buildStepVisualStyle(
        variation.base.note,
        variation.resolved.velocity,
        variation.resolved.gate,
        variation.resolved.nudge,
        true,
        railWidth,
        buttonHeight
    );
    const auto low = grid::buildStepVisualStyle(
        variation.base.note,
        variation.base.velocity,
        clampGateValue(static_cast<int>(variation.base.gate) - variation.ranges.gatePercent),
        variation.base.nudge,
        true,
        railWidth,
        buttonHeight
    );
    const auto high = grid::buildStepVisualStyle(
        variation.base.note,
        variation.base.velocity,
        clampGateValue(static_cast<int>(variation.base.gate) + variation.ranges.gatePercent),
        variation.base.nudge,
        true,
        railWidth,
        buttonHeight
    );

    const lv_coord_t y = static_cast<lv_coord_t>(
        buttonArea.y1 + base.y + base.height - STEP_SHAPE_STROKE_WIDTH
    );
    const lv_coord_t lowRight = static_cast<lv_coord_t>(buttonArea.x1 + low.x + low.width - 1);
    const lv_coord_t highRight = static_cast<lv_coord_t>(buttonArea.x1 + high.x + high.width - 1);
    const lv_coord_t baseRight = static_cast<lv_coord_t>(buttonArea.x1 + base.x + base.width - 1);
    const lv_coord_t resolvedRight =
        static_cast<lv_coord_t>(buttonArea.x1 + resolved.x + resolved.width - 1);

    if (rangeVisible) {
        drawVerticalRangeTick(layer, lowRight, y);
        drawVerticalRangeTick(layer, highRight, y);
    }
    if (deltaVisible && variation.gateDelta != 0) {
        drawVariationRect(
            layer,
            std::min(baseRight, resolvedRight),
            y,
            static_cast<lv_coord_t>(std::abs(resolvedRight - baseRight) + 1),
            VARIATION_DELTA_THICKNESS,
            VARIATION_DELTA_COLOR,
            VARIATION_DELTA_OPA
        );
    }
}

void drawNudgeVariation(lv_layer_t* layer,
                        const lv_area_t& buttonArea,
                        lv_coord_t railWidth,
                        lv_coord_t buttonHeight,
                        const ResolvedVariation& variation,
                        bool rangeVisible,
                        bool deltaVisible) {
    if (variation.ranges.nudge == 0) return;

    const auto base = grid::buildStepVisualStyle(
        variation.base.note,
        variation.base.velocity,
        variation.base.gate,
        variation.base.nudge,
        true,
        railWidth,
        buttonHeight
    );
    const auto low = grid::buildStepVisualStyle(
        variation.base.note,
        variation.base.velocity,
        variation.base.gate,
        clampNudgeValue(static_cast<int>(variation.base.nudge) - variation.ranges.nudge),
        true,
        railWidth,
        buttonHeight
    );
    const auto high = grid::buildStepVisualStyle(
        variation.base.note,
        variation.base.velocity,
        variation.base.gate,
        clampNudgeValue(static_cast<int>(variation.base.nudge) + variation.ranges.nudge),
        true,
        railWidth,
        buttonHeight
    );
    const auto resolved = grid::buildStepVisualStyle(
        variation.base.note,
        variation.resolved.velocity,
        variation.resolved.gate,
        variation.resolved.nudge,
        true,
        railWidth,
        buttonHeight
    );

    const lv_coord_t y = static_cast<lv_coord_t>(
        buttonArea.y1 + base.y + base.height - STEP_SHAPE_STROKE_WIDTH
    );
    const lv_coord_t lowX = static_cast<lv_coord_t>(buttonArea.x1 + low.x);
    const lv_coord_t highX = static_cast<lv_coord_t>(buttonArea.x1 + high.x);
    const lv_coord_t baseX = static_cast<lv_coord_t>(buttonArea.x1 + base.x);
    const lv_coord_t resolvedX = static_cast<lv_coord_t>(buttonArea.x1 + resolved.x);

    if (rangeVisible) {
        drawVerticalRangeTick(layer, lowX, y);
        drawVerticalRangeTick(layer, highX, y);
    }
    if (deltaVisible && variation.nudgeDelta != 0) {
        drawVariationRect(
            layer,
            std::min(baseX, resolvedX),
            y,
            static_cast<lv_coord_t>(std::abs(resolvedX - baseX) + 1),
            VARIATION_DELTA_THICKNESS,
            VARIATION_DELTA_COLOR,
            VARIATION_DELTA_OPA
        );
    }
}

void drawRuntimeVariation(lv_layer_t* layer,
                          const lv_area_t& buttonArea,
                          lv_coord_t railWidth,
                          lv_coord_t buttonHeight,
                          const grid::TileVariationRenderState& state) {
    const auto& variation = state.resolved;
    drawVelocityVariation(
        layer,
        buttonArea,
        railWidth,
        buttonHeight,
        variation,
        state.rangeVisible && state.rangeProperty == StepProperty::VELOCITY,
        state.deltaVisible
    );
    drawGateVariation(
        layer,
        buttonArea,
        railWidth,
        buttonHeight,
        variation,
        state.rangeVisible && state.rangeProperty == StepProperty::GATE,
        state.deltaVisible
    );
    drawNudgeVariation(
        layer,
        buttonArea,
        railWidth,
        buttonHeight,
        variation,
        state.rangeVisible && state.rangeProperty == StepProperty::NUDGE,
        state.deltaVisible
    );
}

}  // namespace

FLASHMEM StepGrid::StepGrid(lv_obj_t* parent,
                            GeometryInvalidatedCallback geometryInvalidated,
                            void* geometryInvalidatedUserData)
    : geometry_invalidated_(geometryInvalidated)
    , geometry_invalidated_user_data_(geometryInvalidatedUserData) {
    createUI(parent);
    createTiles();
}

FLASHMEM StepGrid::~StepGrid() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        grid_ = nullptr;
        note_layer_ = nullptr;
    }
}

FLASHMEM void StepGrid::createUI(lv_obj_t* parent) {
    grid::widgets::createRoot(
        parent,
        container_,
        grid_,
        note_layer_,
        onGeometryChangedEvent,
        this
    );
}

FLASHMEM void StepGrid::createTiles() {
    geometry_.noteLabelHeight = grid::widgets::noteLabelHeight();

    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        grid::widgets::createTile(
            i,
            grid_,
            note_layer_,
            tiles_[i],
            note_labels_[i],
            original_note_labels_[i],
            step_inline_icons_[i],
            step_buttons_[i],
            step_shapes_[i],
            geometry_.inlineIconWidth[i],
            geometry_.inlineIconHeight[i],
            onGeometryChangedEvent,
            this
        );
        tile_button_draw_contexts_[i] = TileButtonDrawContext{.grid = this, .tileIndex = i};
        if (step_buttons_[i]) {
            lv_obj_add_event_cb(
                step_buttons_[i],
                onTileButtonDrawEvent,
                LV_EVENT_DRAW_MAIN,
                &tile_button_draw_contexts_[i]
            );
            lv_obj_add_event_cb(
                step_buttons_[i],
                onTileButtonDrawEvent,
                LV_EVENT_DRAW_POST,
                &tile_button_draw_contexts_[i]
            );
        }
    }
}

FLASHMEM void StepGrid::invalidateTileCaches() {
    for (auto& cache : render_cache_.tiles) {
        cache.initialized = false;
    }
}

FLASHMEM void StepGrid::onGeometryChangedEvent(lv_event_t* event) {
    auto* self = static_cast<StepGrid*>(lv_event_get_user_data(event));
    if (!self) return;
    self->markGeometryDirty();
}

FLASHMEM void StepGrid::markGeometryDirty() {
    const bool wasDirty = geometry_.dirty;
    geometry_.dirty = true;
    if (!wasDirty && geometry_invalidated_) {
        geometry_invalidated_(geometry_invalidated_user_data_);
    }
}

FLASHMEM bool StepGrid::refreshStaticGeometry() {
    if (!note_layer_ || !container_) return false;
    lv_coord_t containerWidth = lv_obj_get_width(container_);
    lv_coord_t containerHeight = lv_obj_get_height(container_);
    lv_coord_t noteLayerWidth = lv_obj_get_width(note_layer_);
    lv_coord_t noteLayerHeight = lv_obj_get_height(note_layer_);
    if (!geometry_.initialized ||
        geometry_.containerWidth != containerWidth ||
        geometry_.containerHeight != containerHeight ||
        geometry_.noteLayerWidth != noteLayerWidth ||
        geometry_.noteLayerHeight != noteLayerHeight) {
        lv_obj_update_layout(container_);
        containerWidth = lv_obj_get_width(container_);
        containerHeight = lv_obj_get_height(container_);
        noteLayerWidth = lv_obj_get_width(note_layer_);
        noteLayerHeight = lv_obj_get_height(note_layer_);
    }

    lv_area_t noteLayerArea{};
    lv_obj_get_coords(note_layer_, &noteLayerArea);

    bool changed = !geometry_.initialized ||
                   geometry_.containerWidth != containerWidth ||
                   geometry_.containerHeight != containerHeight ||
                   geometry_.noteLayerWidth != noteLayerWidth ||
                   geometry_.noteLayerHeight != noteLayerHeight;

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
        changed = changed ||
                  this->geometry_.railWidth[i] != geometry.railWidth ||
                  this->geometry_.buttonHeight[i] != geometry.buttonHeight ||
                  this->geometry_.noteBaseX[i] != geometry.noteBaseX ||
                  this->geometry_.noteBaseY[i] != geometry.noteBaseY ||
                  this->geometry_.noteLabelBaselineY[i] != geometry.noteLabelBaselineY;
        this->geometry_.railWidth[i] = geometry.railWidth;
        this->geometry_.buttonHeight[i] = geometry.buttonHeight;
        this->geometry_.noteBaseX[i] = geometry.noteBaseX;
        this->geometry_.noteBaseY[i] = geometry.noteBaseY;
        this->geometry_.noteLabelBaselineY[i] = geometry.noteLabelBaselineY;
    }

    geometry_.initialized = true;
    geometry_.containerWidth = containerWidth;
    geometry_.containerHeight = containerHeight;
    geometry_.noteLayerWidth = noteLayerWidth;
    geometry_.noteLayerHeight = noteLayerHeight;
    geometry_.dirty = false;
    return changed;
}

void StepGrid::renderTileIndex(uint8_t tileIndex,
                               const TileRenderState& state,
                               const TileRenderDiff& diff) {
    if (!diff.absoluteStepChanged) {
        return;
    }

    auto& cache = render_cache_.tiles[tileIndex];

    char text[4];
    oc::type::text::formatUnsigned(text, sizeof(text), static_cast<unsigned>(state.absoluteStep) + 1U);
    if (std::strcmp(cache.stepIndexText, text) == 0) {
        return;
    }

    std::strncpy(cache.stepIndexText, text, sizeof(cache.stepIndexText) - 1);
    cache.stepIndexText[sizeof(cache.stepIndexText) - 1] = '\0';
}

void StepGrid::renderTileShape(uint8_t tileIndex,
                               const sequencer::grid::StepVisualStyle& visual,
                               lv_coord_t noteBaseX,
                               lv_coord_t noteBaseY,
                               lv_opa_t strokeOpa) {
    auto& cache = render_cache_.tiles[tileIndex];
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

void StepGrid::renderTileBar(uint8_t tileIndex, bool visible, bool active) {
    auto& cache = render_cache_.tiles[tileIndex];
    const lv_opa_t nextOpa =
        visible ? (active ? STEP_BAR_ACTIVE_OPA : STEP_BAR_INACTIVE_OPA) : LV_OPA_TRANSP;
    const uint32_t nextColor = active ? COLOR_STEP_PLAY_HEX : COLOR_STEP_PLAY_INACTIVE_HEX;
    cache.indicatorVisible = visible;
    if (cache.indicatorColorFull != nextColor) {
        cache.indicatorColorFull = nextColor;
    }

    if (cache.indicatorOpa != nextOpa) {
        cache.indicatorOpa = nextOpa;
    }

}

void StepGrid::onTileButtonDrawEvent(lv_event_t* event) {
    auto* context = static_cast<TileButtonDrawContext*>(lv_event_get_user_data(event));
    if (!context || !context->grid) return;

    StepGrid* self = context->grid;
    const uint8_t tileIndex = context->tileIndex;
    if (tileIndex >= self->step_buttons_.size()) return;

    lv_obj_t* button = lv_event_get_target_obj(event);
    if (!button) return;

    lv_layer_t* layer = lv_event_get_layer(event);
    if (!layer) return;

    const auto& cache = self->render_cache_.tiles[tileIndex];
    if (!cache.initialized) return;

    lv_area_t buttonArea{};
    lv_obj_get_coords(button, &buttonArea);
    const lv_coord_t contentWidth = lv_obj_get_content_width(button);
    const lv_coord_t contentHeight = lv_obj_get_content_height(button);
    const lv_coord_t railWidth = grid::measureRailWidth(contentWidth);
    const lv_coord_t buttonHeight = grid::measureButtonHeight(contentHeight);
    const lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_DRAW_POST) {
        if (cache.inPattern && cache.enabled && cache.variation.visible) {
            drawRuntimeVariation(
                layer,
                buttonArea,
                railWidth,
                buttonHeight,
                cache.variation
            );
        }
        return;
    }

    if (cache.inPattern) {
        lv_draw_rect_dsc_t guideDsc;
        lv_draw_rect_dsc_init(&guideDsc);
        guideDsc.bg_color = lv_color_hex(STEP_GUIDE_COLOR);
        guideDsc.bg_opa = STEP_GUIDE_OPA;
        guideDsc.radius = 0;
        guideDsc.border_width = 0;

        for (uint8_t g = 0; g < STEP_GUIDE_COUNT; ++g) {
            const auto layout = grid::buildGuideLayout(g, railWidth, buttonHeight);
            const lv_area_t guideArea{
                .x1 = static_cast<lv_coord_t>(buttonArea.x1 + layout.x),
                .y1 = static_cast<lv_coord_t>(buttonArea.y1 + layout.y),
                .x2 = static_cast<lv_coord_t>(buttonArea.x1 + layout.x + STEP_GUIDE_WIDTH - 1),
                .y2 = static_cast<lv_coord_t>(buttonArea.y1 + layout.y + layout.height - 1),
            };
            lv_draw_rect(layer, &guideDsc, &guideArea);
        }
    }

    if (cache.indicatorVisible && cache.indicatorOpa != LV_OPA_TRANSP) {
        lv_draw_rect_dsc_t indicatorDsc;
        lv_draw_rect_dsc_init(&indicatorDsc);
        indicatorDsc.bg_color = lv_color_hex(cache.indicatorColorFull);
        indicatorDsc.bg_opa = cache.indicatorOpa;
        indicatorDsc.radius = 0;
        indicatorDsc.border_width = 0;
        const lv_area_t indicatorArea{
            .x1 = buttonArea.x1,
            .y1 = static_cast<lv_coord_t>(buttonArea.y2 - STEP_BAR_HEIGHT + 1),
            .x2 = buttonArea.x2,
            .y2 = buttonArea.y2,
        };
        lv_draw_rect(layer, &indicatorDsc, &indicatorArea);
    }

    drawSelectionOverlay(layer, buttonArea, cache);

    if (cache.stepIndexText[0] != '\0') {
        lv_draw_label_dsc_t labelDsc;
        lv_draw_label_dsc_init(&labelDsc);
        labelDsc.text = cache.stepIndexText;
        labelDsc.font = fonts.inter_13_bold;
        labelDsc.color = lv_color_hex(STEP_INDEX_COLOR);
        labelDsc.opa = STEP_INDEX_OPA;
        labelDsc.align = LV_TEXT_ALIGN_RIGHT;

        lv_area_t labelArea = buttonArea;
        labelArea.x1 = buttonArea.x1;
        labelArea.x2 = static_cast<lv_coord_t>(buttonArea.x2 - STEP_INDEX_RIGHT_PAD);
        labelArea.y1 = static_cast<lv_coord_t>(buttonArea.y1 + STEP_INDEX_TOP_PAD);
        lv_draw_label(layer, &labelDsc, &labelArea);
    }

    if (cache.inPattern) {
        drawSemanticBadges(layer, buttonArea, cache);
    }
}

void StepGrid::renderTile(
    uint8_t tileIndex,
    const TileRenderState& state,
    const TileRenderDiff& diff,
    bool propertyVisualChanged,
    bool tileFeedbackChanged,
    bool geometryChanged,
    const StepGridFrameState& frameState
) {
    auto& cache = render_cache_.tiles[tileIndex];
    const bool probabilityMasked =
        state.inPattern &&
        state.enabled &&
        !state.probabilityCycleActive;
    const bool noteLabelNeedsRender =
        !cache.initialized ||
        geometryChanged ||
        propertyVisualChanged ||
        tileFeedbackChanged ||
        diff.inPatternChanged ||
        diff.enabledChanged ||
        diff.noteChanged ||
        diff.velocityChanged ||
        diff.probabilityChanged ||
        diff.gateChanged ||
        diff.nudgeChanged ||
        diff.variationChanged ||
        diff.probabilityCycleActiveChanged ||
        diff.childContentChanged ||
        diff.childPitchSummaryChanged;
    const lv_coord_t noteLabelY = geometry_.noteLabelBaselineY[tileIndex];
    if (!geometryChanged &&
        !diff.dataChanged && !diff.barChanged && !propertyVisualChanged && !tileFeedbackChanged &&
        !diff.probabilityMaskChanged && !diff.contentBadgesChanged &&
        !diff.childContentChanged && !diff.childPitchSummaryChanged) {
        return;
    }

    renderTileIndex(tileIndex, state, diff);

    if (geometryChanged || diff.dataChanged || diff.probabilityMaskChanged) {
        const auto visual = grid::buildStepVisualStyle(
            grid::runtimePitchDisplayNote(state),
            grid::runtimeVelocityDisplayValue(state),
            grid::runtimeGateDisplayValue(state),
            grid::runtimeNudgeDisplayValue(state),
            state.enabled,
            geometry_.railWidth[tileIndex],
            geometry_.buttonHeight[tileIndex]
        );
        const lv_coord_t noteBaseX = static_cast<lv_coord_t>(geometry_.noteBaseX[tileIndex] + visual.x);
        const lv_coord_t noteBaseY = static_cast<lv_coord_t>(geometry_.noteBaseY[tileIndex] + visual.y);
        const lv_opa_t shapeStrokeOpa =
            probabilityMasked ? STEP_PROBABILITY_MASKED_OPA : visual.strokeOpa;

        if (step_shapes_[tileIndex]) {
            if (!state.inPattern) {
                if (cache.shapeVisible) {
                    lv_obj_add_flag(step_shapes_[tileIndex], LV_OBJ_FLAG_HIDDEN);
                    cache.shapeVisible = false;
                }
            } else {
                renderTileShape(tileIndex, visual, noteBaseX, noteBaseY, shapeStrokeOpa);
            }
        }
    }

    if (noteLabelNeedsRender) {
        const auto propertyVisual = sequencer::visual::buildStepPropertyVisual(
            frameState.activeProperty,
            state.inPattern
        );

        grid::label_renderer::renderTileNoteLabel(
            tileIndex,
            render_cache_.tiles[tileIndex],
            note_labels_[tileIndex],
            original_note_labels_[tileIndex],
            step_inline_icons_[tileIndex],
            state,
            diff,
            propertyVisualChanged,
            tileFeedbackChanged,
            geometryChanged,
            frameState.activeProperty,
            render_cache_.feedback,
            propertyVisual,
            geometry_.noteBaseX[tileIndex],
            noteLabelY,
            geometry_.noteLabelHeight,
            geometry_.inlineIconWidth[tileIndex],
            geometry_.inlineIconHeight[tileIndex]
        );
    }

    if (diff.dataChanged || diff.barChanged) {
        renderTileBar(
            tileIndex,
            state.playheadVisible && state.inPattern,
            state.playing
        );
    }

    cache.initialized = true;
    cache.absoluteStep = state.absoluteStep;
    cache.inPattern = state.inPattern;
    cache.enabled = state.enabled;
    cache.stepSelectionActive = state.stepSelectionActive;
    cache.stepSelectionCursor = state.stepSelectionCursor;
    cache.stepSelectionSelected = state.stepSelectionSelected;
    cache.stepPastePreviewActive = state.stepPastePreviewActive;
    cache.stepPastePreview = state.stepPastePreview;
    cache.playheadVisible = state.playheadVisible;
    cache.playing = state.playing;
    cache.probabilityCycleActive = state.probabilityCycleActive;
    cache.note = state.note;
    cache.velocity = state.velocity;
    cache.probability = state.probability;
    cache.gate = state.gate;
    cache.nudge = state.nudge;
    cache.childContentContext = state.childContentContext;
    cache.childContentOffset = state.childContentOffset;
    cache.childContentNoteOffsetUsesScaleDegrees = state.childContentNoteOffsetUsesScaleDegrees;
    cache.childPitchSummaryVisible = state.childPitchSummaryVisible;
    cache.childPitchSummaryNote = state.childPitchSummaryNote;
    cache.variation = state.variation;
    cache.contentBadges = state.contentBadges;

}

void StepGrid::render(const sequencer::grid::StepGridFrameState& frameState) {
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;
    OC_PERF_SCOPE(perfRender, "ui.step-grid.render");

    bool geometryChanged = false;
    if (geometry_.dirty) {
        geometryChanged = refreshStaticGeometry();
    }

    const auto plan = grid::buildFrameRenderPlan(
        render_cache_.tiles,
        render_cache_.property,
        render_cache_.feedback,
        frameState
    );

    render_cache_.property = frameState.activeProperty;
    render_cache_.feedback = plan.nextFeedback;

    if (!plan.anyDirty && !geometryChanged) {
        return;
    }

#if OC_ENABLE_STATS
    uint32_t dirtyTileCount = 0;
#endif
    oc::ui::lvgl::StaticSurfaceInvalidationBatch<8> invalidation(container_);
    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        if (!plan.tileDirty[i] && !geometryChanged) continue;
        invalidation.include(tiles_[i]);
#if OC_ENABLE_STATS
        dirtyTileCount += 1;
#endif
        const TileRenderState& state = frameState.tiles[i];
        renderTile(
            i,
            state,
            plan.diffs[i],
            plan.propertyVisualChanged,
            plan.feedbackChanged[i],
            geometryChanged,
            frameState
        );
    }
    invalidation.flush();

#if OC_ENABLE_STATS
    OC_PERF_UNITS(perfRender, dirtyTileCount, geometryChanged ? 1U : 0U);
#endif
}

}  // namespace core::ui

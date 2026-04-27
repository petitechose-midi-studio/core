#include "StepGrid.hpp"

#include <array>
#include <cmath>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/type/TextFormat.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/sequencer/StepGridGeometryLogic.hpp"
#include "ui/sequencer/StepGridLabelRenderer.hpp"
#include "ui/sequencer/StepGridRenderLogic.hpp"
#include "ui/sequencer/StepGridRenderPlanner.hpp"
#include "ui/sequencer/StepGridWidgets.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

namespace theme = oc::ui::lvgl::base_theme;
namespace grid = core::ui::sequencer::grid;

namespace core::ui {

namespace {

constexpr uint32_t COLOR_STEP_PLAY_HEX = 0x5CA8EE;
constexpr lv_coord_t STEP_BAR_HEIGHT = grid::STEP_BAR_HEIGHT;
constexpr lv_opa_t STEP_BAR_ACTIVE_OPA = LV_OPA_COVER;
constexpr uint32_t STEP_TEXT_DISABLED_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_TEXT_DISABLED_OPA = static_cast<lv_opa_t>(theme::opacity::OPA_50);
constexpr uint32_t STEP_INDEX_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_INDEX_OPA = LV_OPA_60;
constexpr lv_opa_t STEP_PROBABILITY_MASKED_OPA = LV_OPA_30;
constexpr uint32_t STEP_GUIDE_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_GUIDE_OPA = LV_OPA_50;
constexpr lv_coord_t STEP_GUIDE_WIDTH = 1;
constexpr uint8_t STEP_GUIDE_COUNT = 3;
constexpr lv_coord_t STEP_INDEX_RIGHT_PAD = 4;
constexpr lv_coord_t STEP_INDEX_TOP_PAD = 2;

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
    geometry_.dirty = true;
    render_cache_.feedback = {};
    invalidateTileCaches();
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
            step_inline_icons_[i],
            step_buttons_[i],
            step_shapes_[i],
            step_markers_[i],
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
        }
    }
}

void StepGrid::invalidateTileCaches() {
    for (auto& cache : render_cache_.tiles) {
        cache.initialized = false;
    }
}

void StepGrid::onGeometryChangedEvent(lv_event_t* event) {
    auto* self = static_cast<StepGrid*>(lv_event_get_user_data(event));
    if (!self) return;
    self->markGeometryDirty();
}

void StepGrid::markGeometryDirty() {
    geometry_.dirty = true;
}

void StepGrid::refreshStaticGeometry() {
    if (!note_layer_ || !container_) return;

    const lv_coord_t containerWidth = lv_obj_get_width(container_);
    const lv_coord_t containerHeight = lv_obj_get_height(container_);
    const lv_coord_t noteLayerWidth = lv_obj_get_width(note_layer_);
    const lv_coord_t noteLayerHeight = lv_obj_get_height(note_layer_);
    if (!geometry_.initialized ||
        geometry_.containerWidth != containerWidth ||
        geometry_.containerHeight != containerHeight ||
        geometry_.noteLayerWidth != noteLayerWidth ||
        geometry_.noteLayerHeight != noteLayerHeight) {
        lv_obj_update_layout(container_);
    }

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
        this->geometry_.railWidth[i] = geometry.railWidth;
        this->geometry_.buttonHeight[i] = geometry.buttonHeight;
        this->geometry_.noteBaseX[i] = geometry.noteBaseX;
        this->geometry_.noteBaseY[i] = geometry.noteBaseY;
        this->geometry_.noteLabelBaselineY[i] = geometry.noteLabelBaselineY;
    }

    geometry_.initialized = true;
    geometry_.containerWidth = lv_obj_get_width(container_);
    geometry_.containerHeight = lv_obj_get_height(container_);
    geometry_.noteLayerWidth = lv_obj_get_width(note_layer_);
    geometry_.noteLayerHeight = lv_obj_get_height(note_layer_);
    geometry_.dirty = false;
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

void StepGrid::renderTileMarker(uint8_t tileIndex,
                                const TileRenderState& state,
                                bool noteVisualChanged,
                                lv_coord_t noteBaseX,
                                lv_coord_t noteBaseY,
                                lv_opa_t markerOpa) {
    auto& cache = render_cache_.tiles[tileIndex];
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
    auto& cache = render_cache_.tiles[tileIndex];
    const lv_opa_t nextOpa = visible ? STEP_BAR_ACTIVE_OPA : LV_OPA_TRANSP;
    cache.indicatorVisible = visible;

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
        indicatorDsc.bg_color = lv_color_hex(COLOR_STEP_PLAY_HEX);
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
}

void StepGrid::renderTile(
    uint8_t tileIndex,
    const TileRenderState& state,
    const TileRenderDiff& diff,
    bool propertyVisualChanged,
    bool tileFeedbackChanged,
    const StepGridFrameState& frameState
) {
    auto& cache = render_cache_.tiles[tileIndex];
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
    const lv_coord_t noteLabelY = geometry_.noteLabelBaselineY[tileIndex];
    bool buttonOverlayDirty =
        !cache.initialized || diff.absoluteStepChanged || diff.inPatternChanged || diff.barChanged;

    if (!diff.dataChanged && !diff.barChanged && !propertyVisualChanged && !tileFeedbackChanged &&
        !diff.probabilityMaskChanged) {
        return;
    }

    renderTileIndex(tileIndex, state, diff);

    if (diff.dataChanged || diff.probabilityMaskChanged) {
        const auto visual = grid::buildStepVisualStyle(
            state.note,
            state.velocity,
            state.gate,
            state.nudge,
            state.enabled,
            geometry_.railWidth[tileIndex],
            geometry_.buttonHeight[tileIndex]
        );
        const lv_coord_t noteBaseX = static_cast<lv_coord_t>(geometry_.noteBaseX[tileIndex] + visual.x);
        const lv_coord_t noteBaseY = static_cast<lv_coord_t>(geometry_.noteBaseY[tileIndex] + visual.y);
        const lv_opa_t shapeStrokeOpa =
            probabilityMasked ? STEP_PROBABILITY_MASKED_OPA : visual.strokeOpa;
        const lv_opa_t markerOpa =
            state.enabled
                ? (probabilityMasked ? STEP_PROBABILITY_MASKED_OPA : LV_OPA_COVER)
                : STEP_TEXT_DISABLED_OPA;

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

        grid::label_renderer::renderTileNoteLabel(
            tileIndex,
            render_cache_.tiles[tileIndex],
            note_labels_[tileIndex],
            step_inline_icons_[tileIndex],
            state,
            diff,
            propertyVisualChanged,
            tileFeedbackChanged,
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
        renderTileBar(tileIndex, state.playing && state.inPattern);
    }

    cache.initialized = true;
    cache.absoluteStep = state.absoluteStep;
    cache.inPattern = state.inPattern;
    cache.enabled = state.enabled;
    cache.playing = state.playing;
    cache.probabilityCycleActive = state.probabilityCycleActive;
    cache.note = state.note;
    cache.velocity = state.velocity;
    cache.probability = state.probability;
    cache.gate = state.gate;
    cache.nudge = state.nudge;

    if (buttonOverlayDirty && step_buttons_[tileIndex]) {
        lv_obj_invalidate(step_buttons_[tileIndex]);
    }
}

void StepGrid::render(const sequencer::grid::StepGridFrameState& frameState) {
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;

    if (geometry_.dirty) {
        refreshStaticGeometry();
    }

    const auto plan = grid::buildFrameRenderPlan(
        render_cache_.tiles,
        render_cache_.property,
        render_cache_.feedback,
        frameState
    );

    render_cache_.property = frameState.activeProperty;
    render_cache_.feedback = plan.nextFeedback;

    if (!plan.anyDirty) {
        return;
    }

    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        if (!plan.tileDirty[i]) continue;
        const TileRenderState state = frameState.tiles[i];
        renderTile(
            i,
            state,
            plan.diffs[i],
            plan.propertyVisualChanged,
            plan.feedbackChanged[i],
            frameState
        );
    }
}

}  // namespace core::ui

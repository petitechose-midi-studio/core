#include "StepGrid.hpp"

#include <array>
#include <cmath>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/type/TextFormat.hpp>

#include <config/PlatformCompat.hpp>

#include "ui/sequencer/StepGridGeometryLogic.hpp"
#include "ui/sequencer/StepGridLabelLogic.hpp"
#include "ui/sequencer/StepGridLabelRenderer.hpp"
#include "ui/sequencer/StepGridRenderLogic.hpp"
#include "ui/sequencer/StepGridRenderPlanner.hpp"
#include "ui/sequencer/StepGridWidgets.hpp"
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
constexpr lv_coord_t STEP_BAR_WIDTH = static_cast<lv_coord_t>(STEP_BUTTON_SIZE / 2);
constexpr lv_coord_t STEP_BAR_HEIGHT = grid::STEP_BAR_HEIGHT;
constexpr lv_opa_t STEP_BAR_ACTIVE_OPA = LV_OPA_COVER;
constexpr lv_opa_t STEP_SHAPE_OPA_ENABLED = grid::STEP_SHAPE_OPA_ENABLED;
constexpr uint32_t STEP_TEXT_DISABLED_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_TEXT_DISABLED_OPA = static_cast<lv_opa_t>(theme::opacity::OPA_50);
constexpr uint32_t STEP_INLINE_NOTE_COLOR = theme::color::TEXT_PRIMARY;
constexpr lv_opa_t STEP_INLINE_NOTE_OPA = LV_OPA_COVER;
constexpr lv_opa_t STEP_INLINE_VALUE_OPA = LV_OPA_70;
constexpr uint32_t STEP_INDEX_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_INDEX_OPA = LV_OPA_60;
constexpr lv_opa_t STEP_PROBABILITY_MASKED_OPA = LV_OPA_30;
constexpr uint32_t COLOR_SELECTION_CLEAR_HEX = 0xF28B5B;
constexpr uint32_t COLOR_SELECTION_COPY_HEX = 0x59B7C9;
constexpr uint32_t COLOR_SELECTION_PASTE_HEX = 0x79C96B;
constexpr lv_opa_t STEP_SELECTION_RANGE_OPA = LV_OPA_50;
constexpr lv_opa_t STEP_SELECTION_CURSOR_OPA = LV_OPA_COVER;

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
            step_index_labels_[i],
            step_inline_icons_[i],
            step_buttons_[i],
            step_shapes_[i],
            step_markers_[i],
            step_indicators_[i],
            step_selection_dots_[i],
            step_guides_[i],
            geometry_.inlineIconWidth[i],
            geometry_.inlineIconHeight[i],
            onGeometryChangedEvent,
            this
        );
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
        this->geometry_.railWidth[i] = geometry.railWidth;
        this->geometry_.buttonHeight[i] = geometry.buttonHeight;
        this->geometry_.noteBaseX[i] = geometry.noteBaseX;
        this->geometry_.noteBaseY[i] = geometry.noteBaseY;
        this->geometry_.noteLabelBaselineY[i] = geometry.noteLabelBaselineY;

        grid::widgets::positionStepGuides(button, step_guides_[i]);
    }

    geometry_.dirty = false;
}

void StepGrid::renderTileGuides(uint8_t tileIndex, bool inPattern, const TileRenderDiff& diff) {
    auto& cache = render_cache_.tiles[tileIndex];
    for (auto* guide : step_guides_[tileIndex]) {
        if (!guide) continue;
        if (!diff.inPatternChanged && cache.initialized) continue;
        if (inPattern) lv_obj_clear_flag(guide, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(guide, LV_OBJ_FLAG_HIDDEN);
    }
}

void StepGrid::renderTileIndex(uint8_t tileIndex,
                               const TileRenderState& state,
                               const TileRenderDiff& diff) {
    if (!diff.absoluteStepChanged) {
        return;
    }

    auto& cache = render_cache_.tiles[tileIndex];
    lv_obj_t* label = step_index_labels_[tileIndex];
    if (!label) return;

    char text[4];
    oc::type::text::formatUnsigned(text, sizeof(text), static_cast<unsigned>(state.absoluteStep) + 1U);
    if (std::strcmp(cache.stepIndexText, text) == 0) {
        return;
    }

    lv_label_set_text(label, text);
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
    auto& cache = render_cache_.tiles[tileIndex];
    lv_obj_t* selectionDot = step_selection_dots_[tileIndex];
    if (!selectionDot) return;

    const bool rangeVisible = selection.active && stepInSelectionRange(absoluteStep, selection);
    const bool cursorVisible = selection.active && absoluteStep == selection.cursorStep;
    const bool dotVisible = rangeVisible || cursorVisible;

    uint32_t dotColor = theme::color::INACTIVE;
    if (selection.kind == core::state::sequencer::RangeSelectionKind::CLEAR) {
        dotColor = COLOR_SELECTION_CLEAR_HEX;
    } else if (selection.kind == core::state::sequencer::RangeSelectionKind::COPY) {
        dotColor = COLOR_SELECTION_COPY_HEX;
    }
    if (selection.kind == core::state::sequencer::RangeSelectionKind::COPY &&
        selection.phase == core::state::sequencer::RangeSelectionPhase::PASTE_TARGET &&
        cursorVisible) {
        dotColor = COLOR_SELECTION_PASTE_HEX;
    }

    if (dotVisible) {
        if (!cache.selectionDotVisible) {
            lv_obj_clear_flag(selectionDot, LV_OBJ_FLAG_HIDDEN);
            cache.selectionDotVisible = true;
        }
    } else if (cache.selectionDotVisible) {
        lv_obj_add_flag(selectionDot, LV_OBJ_FLAG_HIDDEN);
        cache.selectionDotVisible = false;
    }

    const lv_opa_t dotOpa = cursorVisible ? STEP_SELECTION_CURSOR_OPA : STEP_SELECTION_RANGE_OPA;
    if (cache.selectionDotColor != dotColor) {
        lv_obj_set_style_bg_color(selectionDot, lv_color_hex(dotColor), 0);
        cache.selectionDotColor = dotColor;
    }
    if (cache.selectionDotOpa != dotOpa) {
        lv_obj_set_style_bg_opa(selectionDot, dotOpa, 0);
        cache.selectionDotOpa = dotOpa;
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

    if (!diff.dataChanged && !diff.barChanged && !propertyVisualChanged && !tileFeedbackChanged &&
        !selectionChanged) {
        return;
    }

    if (selectionChanged || !cache.initialized) {
        renderTileSelection(tileIndex, state.absoluteStep, frameState.selection);
    }

    renderTileIndex(tileIndex, state, diff);

    if (diff.dataChanged) {
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
        render_cache_.selection,
        frameState
    );

    render_cache_.property = frameState.activeProperty;
    render_cache_.feedback = plan.nextFeedback;
    render_cache_.selection = frameState.selection;

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
            plan.selectionChanged,
            frameState
        );
    }
}

}  // namespace core::ui

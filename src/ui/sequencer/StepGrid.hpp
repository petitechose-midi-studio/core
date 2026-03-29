#pragma once

/**
 * @file StepGrid.hpp
 * @brief Sequencer 8-step grid widget
 */

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "ui/sequencer/StepGridLabelLogic.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

namespace core::ui {

class StepGrid : public oc::ui::lvgl::IWidget {
public:
    explicit StepGrid(lv_obj_t* parent);
    ~StepGrid() override;

    StepGrid(const StepGrid&) = delete;
    StepGrid& operator=(const StepGrid&) = delete;

    lv_obj_t* getElement() const override { return container_; }

    void forceRefresh();
    void render(const sequencer::grid::StepGridFrameState& frameState);

private:
    using TileRenderState = sequencer::grid::TileRenderState;
    using TileRenderDiff = sequencer::grid::TileRenderDiff;
    using TileRenderCache = sequencer::grid::TileRenderCache;
    using StepGridFrameState = sequencer::grid::StepGridFrameState;
    using InlineFeedbackSnapshot = sequencer::grid::InlineFeedbackSnapshot;

    void createUI(lv_obj_t* parent);
    void createTiles();
    void invalidateTileCaches();
    void refreshStaticGeometry();
    static void onGeometryChangedEvent(lv_event_t* event);
    void markGeometryDirty();
    void renderTileGuides(uint8_t tileIndex, bool inPattern, const TileRenderDiff& diff);
    void renderTileIndex(uint8_t tileIndex, const TileRenderState& state, const TileRenderDiff& diff);
    void renderTileShape(uint8_t tileIndex,
                         const sequencer::grid::StepVisualStyle& visual,
                         lv_coord_t noteBaseX,
                         lv_coord_t noteBaseY,
                         lv_opa_t strokeOpa);
    void renderTileMarker(uint8_t tileIndex,
                          const TileRenderState& state,
                          bool noteVisualChanged,
                          lv_coord_t noteBaseX,
                          lv_coord_t noteBaseY,
                          lv_opa_t markerOpa);
    void renderTileSelection(uint8_t tileIndex,
                             uint8_t absoluteStep,
                             const sequencer::grid::RangeSelectionSnapshot& selection);
    void renderTileBar(uint8_t tileIndex, bool visible);
    void renderTileNoteLabel(uint8_t tileIndex,
                             const TileRenderState& state,
                             const TileRenderDiff& diff,
                             bool propertyVisualChanged,
                             bool tileFeedbackChanged,
                             const StepGridFrameState& frameState,
                             const sequencer::visual::StepPropertyVisualSpec& propertyVisual,
                             lv_coord_t noteBaseX,
                             lv_coord_t noteLabelY);
    void renderTile(uint8_t tileIndex,
                    const TileRenderState& state,
                    const TileRenderDiff& diff,
                    bool propertyVisualChanged,
                    bool tileFeedbackChanged,
                    bool selectionChanged,
                    const StepGridFrameState& frameState);

    bool geometry_dirty_ = true;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* grid_ = nullptr;
    lv_obj_t* note_layer_ = nullptr;

    std::array<lv_obj_t*, 8> tiles_{};
    std::array<lv_obj_t*, 8> note_labels_{};
    std::array<lv_obj_t*, 8> step_index_labels_{};
    std::array<lv_obj_t*, 8> step_inline_icons_{};
    std::array<lv_obj_t*, 8> step_buttons_{};
    std::array<lv_obj_t*, 8> step_shapes_{};
    std::array<lv_obj_t*, 8> step_markers_{};
    std::array<lv_obj_t*, 8> step_indicators_{};
    std::array<lv_obj_t*, 8> step_selection_dots_{};
    std::array<std::array<lv_obj_t*, 3>, 8> step_guides_{};
    std::array<lv_coord_t, 8> rail_width_cache_{};
    std::array<lv_coord_t, 8> button_height_cache_{};
    std::array<lv_coord_t, 8> note_base_x_{};
    std::array<lv_coord_t, 8> note_base_y_{};
    std::array<lv_coord_t, 8> note_label_baseline_y_{};
    std::array<lv_coord_t, 8> inline_icon_width_{};
    std::array<lv_coord_t, 8> inline_icon_height_{};
    std::array<TileRenderCache, 8> tile_render_cache_{};
    lv_coord_t note_label_height_ = 0;
    core::state::sequencer::StepProperty cached_property_ =
        core::state::sequencer::StepProperty::NOTE;
    InlineFeedbackSnapshot cached_feedback_{};
    sequencer::grid::RangeSelectionSnapshot cached_selection_{};
};

}  // namespace core::ui

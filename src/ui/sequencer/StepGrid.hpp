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
    void renderTile(uint8_t tileIndex,
                    const TileRenderState& state,
                    const TileRenderDiff& diff,
                    bool propertyVisualChanged,
                    bool tileFeedbackChanged,
                    bool selectionChanged,
                    const StepGridFrameState& frameState);

    struct GeometryCacheState {
        bool dirty = true;
        std::array<lv_coord_t, 8> railWidth{};
        std::array<lv_coord_t, 8> buttonHeight{};
        std::array<lv_coord_t, 8> noteBaseX{};
        std::array<lv_coord_t, 8> noteBaseY{};
        std::array<lv_coord_t, 8> noteLabelBaselineY{};
        std::array<lv_coord_t, 8> inlineIconWidth{};
        std::array<lv_coord_t, 8> inlineIconHeight{};
        lv_coord_t noteLabelHeight = 0;
    };

    struct RenderCacheState {
        std::array<TileRenderCache, 8> tiles{};
        core::state::sequencer::StepProperty property =
            core::state::sequencer::StepProperty::NOTE;
        InlineFeedbackSnapshot feedback{};
        sequencer::grid::RangeSelectionSnapshot selection{};
    };

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
    GeometryCacheState geometry_{};
    RenderCacheState render_cache_{};
};

}  // namespace core::ui

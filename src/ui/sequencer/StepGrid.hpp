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

namespace core::ui {

class StepGrid : public oc::ui::lvgl::IWidget {
public:
    using GeometryInvalidatedCallback = void (*)(void* userData);

    explicit StepGrid(lv_obj_t* parent,
                      GeometryInvalidatedCallback geometryInvalidated = nullptr,
                      void* geometryInvalidatedUserData = nullptr);
    ~StepGrid() override;

    StepGrid(const StepGrid&) = delete;
    StepGrid& operator=(const StepGrid&) = delete;

    lv_obj_t* getElement() const override { return container_; }

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
    bool refreshStaticGeometry();
    static void onGeometryChangedEvent(lv_event_t* event);
    static void onTileButtonDrawEvent(lv_event_t* event);
    void markGeometryDirty();
    void renderTileIndex(uint8_t tileIndex, const TileRenderState& state, const TileRenderDiff& diff);
    void renderTilePlayhead(uint8_t tileIndex, bool visible, bool active);
    void renderTile(uint8_t tileIndex,
                    const TileRenderState& state,
                    const TileRenderDiff& diff,
                    bool propertyVisualChanged,
                    bool tileFeedbackChanged,
                    bool geometryChanged,
                    const StepGridFrameState& frameState);

    struct TileButtonDrawContext {
        StepGrid* grid = nullptr;
        uint8_t tileIndex = 0;
    };

    struct GeometryCacheState {
        bool initialized = false;
        bool dirty = true;
        std::array<lv_coord_t, 8> railWidth{};
        std::array<lv_coord_t, 8> buttonHeight{};
        std::array<lv_coord_t, 8> noteBaseX{};
        std::array<lv_coord_t, 8> noteBaseY{};
        std::array<lv_coord_t, 8> noteLabelBaselineY{};
        std::array<lv_coord_t, 8> inlineIconWidth{};
        std::array<lv_coord_t, 8> inlineIconHeight{};
        lv_coord_t noteLabelHeight = 0;
        lv_coord_t containerWidth = -1;
        lv_coord_t containerHeight = -1;
        lv_coord_t noteLayerWidth = -1;
        lv_coord_t noteLayerHeight = -1;
    };

    struct RenderCacheState {
        std::array<TileRenderCache, 8> tiles{};
        sequencer::grid::StepGridPresentation presentation =
            sequencer::grid::StepGridPresentation::MELODIC;
        uint32_t accentColor = 0;
        sequencer::grid::StepPitchViewport pitchViewport{};
        oc::note::sequencer::StepSequencerScaleSettings scaleSettings{};
        bool chromaticPitchEditing = false;
        core::state::sequencer::StepProperty property =
            core::state::sequencer::StepProperty::NOTE;
        InlineFeedbackSnapshot feedback{};
    };

    lv_obj_t* container_ = nullptr;
    lv_obj_t* grid_ = nullptr;
    lv_obj_t* note_layer_ = nullptr;
    GeometryInvalidatedCallback geometry_invalidated_ = nullptr;
    void* geometry_invalidated_user_data_ = nullptr;

    std::array<lv_obj_t*, 8> tiles_{};
    std::array<lv_obj_t*, 8> note_labels_{};
    std::array<lv_obj_t*, 8> original_note_labels_{};
    std::array<lv_obj_t*, 8> step_inline_icons_{};
    std::array<lv_obj_t*, 8> step_buttons_{};
    std::array<TileButtonDrawContext, 8> tile_button_draw_contexts_{};
    GeometryCacheState geometry_{};
    RenderCacheState render_cache_{};
};

}  // namespace core::ui

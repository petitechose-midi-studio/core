#pragma once

/**
 * @file StepGrid.hpp
 * @brief Sequencer 8-step grid widget
 */

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/CoreState.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

namespace core::ui {

class StepGrid : public oc::ui::lvgl::IWidget {
public:
    StepGrid(lv_obj_t* parent, core::state::CoreState& coreState);
    ~StepGrid() override;

    StepGrid(const StepGrid&) = delete;
    StepGrid& operator=(const StepGrid&) = delete;

    lv_obj_t* getElement() const override { return container_; }

    void forceRefresh();
    void render();

private:
    struct TileRenderState {
        bool inPattern = false;
        bool enabled = false;
        bool playing = false;
        uint8_t note = 0;
        uint8_t velocity = 0;
        uint8_t probability = 0;
        uint16_t gate = 0;
        int8_t nudge = 0;
    };

    struct TileRenderDiff {
        bool initialized = false;
        bool inPatternChanged = false;
        bool enabledChanged = false;
        bool noteChanged = false;
        bool velocityChanged = false;
        bool probabilityChanged = false;
        bool gateChanged = false;
        bool nudgeChanged = false;
        bool velocityZeroChanged = false;
        bool dataChanged = false;
        bool barChanged = false;
    };

    struct TileRenderCache {
        bool initialized = false;
        bool inPattern = false;
        bool enabled = false;
        bool playing = false;
        uint8_t note = 0;
        uint8_t velocity = 0;
        uint8_t probability = 0;
        uint16_t gate = 0;
        int8_t nudge = 0;
        lv_coord_t noteLabelHeight = 0;
    };

    void createUI(lv_obj_t* parent);
    void createTiles();
    void invalidateTileCaches();
    void refreshStaticGeometry();
    static void onGeometryChangedEvent(lv_event_t* event);
    void markGeometryDirty();
    TileRenderState readTileRenderState(uint8_t absoluteStep, uint8_t length, uint64_t enabledMask, int16_t playhead) const;
    static TileRenderDiff diffTileRenderState(const TileRenderCache& cache, const TileRenderState& state);
    void renderTileNoteLabel(uint8_t tileIndex,
                             const TileRenderState& state,
                             const TileRenderDiff& diff,
                             bool propertyVisualChanged,
                             const sequencer::visual::StepPropertyVisualSpec& propertyVisual,
                             lv_coord_t noteBaseX,
                             lv_coord_t noteLabelY);
    void renderTilePropertyVisual(uint8_t tileIndex,
                                  const TileRenderState& state,
                                  const TileRenderDiff& diff,
                                  bool propertyVisualChanged,
                                  const sequencer::visual::StepPropertyVisualSpec& propertyVisual,
                                  lv_coord_t shapeWidth,
                                  lv_coord_t shapeHeight,
                                  lv_coord_t noteBaseX,
                                  lv_coord_t noteBaseY);
    void renderTile(uint8_t tileIndex,
                    const TileRenderState& state,
                    const TileRenderDiff& diff,
                    bool propertyVisualChanged);

    core::state::CoreState& core_state_;

    bool geometry_dirty_ = true;
    uint16_t cached_division_denom_ = 0xFFFF;
    uint8_t cached_total_steps_ = 0xFF;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* grid_ = nullptr;
    lv_obj_t* note_layer_ = nullptr;
    lv_obj_t* overlay_layer_ = nullptr;
    lv_obj_t* division_overlay_label_ = nullptr;
    lv_obj_t* total_steps_overlay_label_ = nullptr;
    lv_obj_t* track_overlay_label_ = nullptr;

    std::array<lv_obj_t*, 8> tiles_{};
    std::array<lv_obj_t*, 8> note_labels_{};
    std::array<lv_obj_t*, 8> step_buttons_{};
    std::array<lv_obj_t*, 8> step_shapes_{};
    std::array<lv_obj_t*, 8> step_markers_{};
    std::array<std::array<lv_obj_t*, 5>, 8> step_property_watermarks_{};
    std::array<lv_obj_t*, 8> step_property_value_tracks_{};
    std::array<lv_obj_t*, 8> step_property_value_fills_{};
    std::array<lv_obj_t*, 8> step_property_horizontal_accents_{};
    std::array<lv_obj_t*, 8> step_property_edge_ticks_{};
    std::array<lv_obj_t*, 8> step_indicators_{};
    std::array<lv_obj_t*, 8> step_selectors_{};
    std::array<std::array<lv_obj_t*, 3>, 8> step_guides_{};
    std::array<lv_coord_t, 8> rail_width_cache_{};
    std::array<lv_coord_t, 8> button_height_cache_{};
    std::array<lv_coord_t, 8> note_base_x_{};
    std::array<lv_coord_t, 8> note_base_y_{};
    std::array<lv_coord_t, 8> note_label_baseline_y_{};
    std::array<TileRenderCache, 8> tile_render_cache_{};
    core::state::sequencer::StepProperty cached_property_ =
        core::state::sequencer::StepProperty::NOTE;
};

}  // namespace core::ui

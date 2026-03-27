#pragma once

/**
 * @file SequencerView.hpp
 * @brief Placeholder sequencer view (UI-first)
 */

#include <array>
#include <memory>

#include <lvgl.h>

#include <oc/state/SignalWatcher.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include <ms/ui/component/LayoutView.hpp>

#include "state/CoreState.hpp"
#include "ui/sequencer/SequencerHeaderBar.hpp"

namespace core::ui {

class SequencerView : public oc::ui::lvgl::IView {
public:
    explicit SequencerView(lv_obj_t* parent, core::state::CoreState& coreState);
    ~SequencerView() override;

    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.sequencer"; }
    lv_obj_t* getElement() const override { return container_; }

private:
    struct TileRenderState {
        bool inPattern = false;
        bool enabled = false;
        bool playing = false;
        uint8_t note = 0;
        uint8_t velocity = 0;
        uint16_t gate = 0;
        int8_t nudge = 0;
    };

    struct TileRenderDiff {
        bool initialized = false;
        bool inPatternChanged = false;
        bool enabledChanged = false;
        bool noteChanged = false;
        bool velocityChanged = false;
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
        uint16_t gate = 0;
        int8_t nudge = 0;
        lv_coord_t noteLabelHeight = 0;
    };

    void createLayout(lv_obj_t* parent);
    void createHeaderBar();
    void createSteps();
    void bindToState();

    void requestRender();
    static void onRenderTimer(lv_timer_t* timer);
    static void onGeometryChangedEvent(lv_event_t* event);
    void markGeometryDirty();
    void invalidateTileCaches();
    void refreshStaticGeometry();
    void renderHeader(
        uint8_t len,
        uint8_t page,
        uint8_t focused,
        uint64_t enabledMask,
        int16_t playhead,
        core::state::sequencer::StepProperty property
    );
    TileRenderState readTileRenderState(uint8_t absoluteStep, uint8_t length, uint64_t enabledMask, int16_t playhead) const;
    static TileRenderDiff diffTileRenderState(const TileRenderCache& cache, const TileRenderState& state);
    void renderTile(uint8_t tileIndex, const TileRenderState& state, const TileRenderDiff& diff);
    void render();

    core::state::CoreState& core_state_;
    oc::state::SignalWatcher watcher_;

    bool dirty_ = false;
    bool geometry_dirty_ = true;
    lv_timer_t* render_timer_ = nullptr;
    uint16_t cached_division_denom_ = 0xFFFF;
    uint8_t cached_total_steps_ = 0xFF;

    std::unique_ptr<ms::ui::LayoutView> layout_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    lv_obj_t* overlay_layer_ = nullptr;
    lv_obj_t* division_overlay_label_ = nullptr;
    lv_obj_t* total_steps_overlay_label_ = nullptr;
    lv_obj_t* track_overlay_label_ = nullptr;

    std::unique_ptr<core::ui::SequencerHeaderBar> header_bar_;

    lv_obj_t* grid_ = nullptr;
    lv_obj_t* note_layer_ = nullptr;
    std::array<lv_obj_t*, 8> tiles_{};
    std::array<lv_obj_t*, 8> note_labels_{};
    std::array<lv_obj_t*, 8> step_buttons_{};
    std::array<lv_obj_t*, 8> step_shapes_{};
    std::array<lv_obj_t*, 8> step_markers_{};
    std::array<lv_obj_t*, 8> step_indicators_{};
    std::array<lv_obj_t*, 8> step_selectors_{};
    std::array<std::array<lv_obj_t*, 3>, 8> step_guides_{};
    std::array<lv_coord_t, 8> rail_width_cache_{};
    std::array<lv_coord_t, 8> button_height_cache_{};
    std::array<lv_coord_t, 8> note_base_x_{};
    std::array<lv_coord_t, 8> note_base_y_{};
    std::array<lv_coord_t, 8> note_label_baseline_y_{};
    std::array<TileRenderCache, 8> tile_render_cache_{};
};

}  // namespace core::ui

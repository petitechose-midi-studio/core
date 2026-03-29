#pragma once

/**
 * @file SequencerView.hpp
 * @brief Standalone sequencer view
 */

#include <memory>

#include <lvgl.h>

#include <oc/state/SignalWatcher.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include <ms/ui/component/LayoutView.hpp>

#include "state/CoreState.hpp"
#include "ui/sequencer/SequencerBottomControls.hpp"
#include "ui/sequencer/SequencerHeaderBar.hpp"
#include "ui/sequencer/StepPropertyStrip.hpp"
#include "ui/sequencer/StepGrid.hpp"
#include "ui/strip/ContextActionStrip.hpp"

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
    void createLayout(lv_obj_t* parent);
    void createHeaderBar();
    void createGrid();
    void createBottomControls();
    void createPropertyStrip();
    void createActionStrips();
    void bindToState();
    void bindBottomControlsState();
    void bindHeaderState();
    void bindGridState();
    void bindPropertyStripState();
    void bindQuickControlsState();

    void ensureRenderTimer();
    void scheduleRender();
    void pauseRenderTimerIfIdle();
    void requestRender(bool& dirtyFlag);
    void requestHeaderRender();
    void requestBottomControlsRender();
    void requestPropertyStripRender();
    void requestActionStripsRender();
    void requestGridRender();
    void renderTrackTint();
    static void onRenderTimer(lv_timer_t* timer);
    void markAllDirty();
    void render();

    core::state::CoreState& core_state_;
    oc::state::SignalWatcher watcher_;

    bool dirty_ = false;
    bool header_dirty_ = true;
    bool bottom_controls_dirty_ = true;
    bool property_strip_dirty_ = true;
    bool action_strips_dirty_ = true;
    bool grid_dirty_ = true;
    bool track_tint_dirty_ = true;
    lv_timer_t* render_timer_ = nullptr;

    uint8_t track_tint_cache_track_ = 0;
    uint8_t track_tint_cache_enabled_mask_ = 0xFF;
    bool track_tint_cache_selecting_ = false;

    std::unique_ptr<ms::ui::LayoutView> layout_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    lv_obj_t* interaction_container_ = nullptr;
    lv_obj_t* center_column_ = nullptr;

    std::unique_ptr<core::ui::SequencerHeaderBar> header_bar_;
    std::unique_ptr<core::ui::SequencerBottomControls> bottom_controls_;
    std::unique_ptr<core::ui::StepPropertyStrip> property_strip_;
    std::unique_ptr<core::ui::ContextActionStrip> left_action_strip_;
    std::unique_ptr<core::ui::ContextActionStrip> bottom_action_strip_;
    std::unique_ptr<core::ui::StepGrid> step_grid_;
};

}  // namespace core::ui

#pragma once

/**
 * @file SequencerView.hpp
 * @brief Standalone sequencer view
 */

#include <memory>

#include <lvgl.h>

#include <oc/state/SignalWatcher.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include "state/StatusBarState.hpp"
#include "state/DataManagerState.hpp"
#include "state/GlobalSettingsState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/ViewSelectorState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/common/TrackNavigationStrip.hpp"
#include "ui/sequencer/SequencerBottomControls.hpp"
#include "ui/sequencer/SequencerHeaderBar.hpp"
#include "ui/sequencer/SequencerViewModelBuilder.hpp"
#include "ui/sequencer/StepPropertyStrip.hpp"
#include "ui/sequencer/StepGrid.hpp"
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/view/MainViewFrame.hpp"
#include "ui/view/PausableLvglTimer.hpp"

namespace core::ui {

class SequencerView : public oc::ui::lvgl::IView {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& structureNavigationFocus;
        core::state::StructureClipboardState& structureClipboard;
        core::state::StatusBarState& statusBar;
        core::state::ViewSelectorState& viewSelector;
        core::state::GlobalSettingsState& globalSettings;
        core::state::DataManagerState& dataManager;
    };

    explicit SequencerView(lv_obj_t* parent, StateRefs stateRefs);
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
    void bindHeaderStripState();
    void bindGridState();
    void bindPropertyStripState();
    void bindOverlayVisibilityState();
    void bindLeftActionStripState();
    void bindBottomActionStripState();
    void bindQuickControlsState();
    bool hasBlockingOverlay() const;
    void handleOverlayVisibilityChanged();

    void ensureRenderTimer();
    void scheduleRender(bool ready = false);
    void pauseRenderTimerIfIdle();
    void requestRender(bool& dirtyFlag);
    void requestHeaderTopRender();
    void requestHeaderStripRender();
    void requestTrackStripRender();
    void requestBottomControlsRender();
    void requestPropertyStripRender();
    void requestLeftActionStripRender();
    void requestBottomActionStripRender();
    void requestGridRender();
    static void onRenderTimer(lv_timer_t* timer);
    void markAllDirty();
    void render();
    sequencer::SequencerViewModelSource modelSource() const;

    StateRefs state_refs_;
    oc::state::SignalWatcher watcher_;

    bool dirty_ = false;
    bool header_top_dirty_ = true;
    bool header_strip_dirty_ = true;
    bool track_strip_dirty_ = true;
    bool bottom_controls_dirty_ = true;
    bool property_strip_dirty_ = true;
    bool left_action_strip_dirty_ = true;
    bool bottom_action_strip_dirty_ = true;
    bool grid_dirty_ = true;
    std::unique_ptr<PausableLvglTimer> render_timer_;

    std::unique_ptr<core::ui::MainViewFrame> frame_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    lv_obj_t* interaction_container_ = nullptr;
    lv_obj_t* center_column_ = nullptr;
    lv_obj_t* structure_row_container_ = nullptr;

    std::unique_ptr<core::ui::SequencerHeaderBar> header_bar_;
    std::unique_ptr<core::ui::TrackNavigationStrip> track_strip_;
    std::unique_ptr<core::ui::SequencerBottomControls> bottom_controls_;
    std::unique_ptr<core::ui::StepPropertyStrip> property_strip_;
    std::unique_ptr<core::ui::ContextActionStrip> left_action_strip_;
    std::unique_ptr<core::ui::ContextActionStrip> bottom_action_strip_;
    std::unique_ptr<core::ui::StepGrid> step_grid_;
};

}  // namespace core::ui

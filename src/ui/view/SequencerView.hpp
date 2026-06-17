#pragma once

/**
 * @file SequencerView.hpp
 * @brief Standalone sequencer view
 */

#include <lvgl.h>

#include <oc/state/SignalWatcher.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/StatusBarState.hpp"
#include "state/DataManagerState.hpp"
#include "state/DeviceSettingsState.hpp"
#include "state/SequencerSettingsState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/ViewSelectorState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/sequencer/SequencerHeaderBar.hpp"
#include "ui/sequencer/SequencerViewModelBuilder.hpp"
#include "ui/sequencer/StepPropertySelectionOverlay.hpp"
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
        core::state::TrackNavigationState& trackNavigation;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& structureNavigationFocus;
        oc::state::Signal<uint8_t, 8>& sharedTrackActive;
        oc::state::Signal<uint16_t, 16>& sharedTrackEnabledMask;
        core::state::StructureClipboardState& structureClipboard;
        core::state::StatusBarState& statusBar;
        core::state::ViewSelectorState& viewSelector;
        core::state::DeviceSettingsState& deviceSettings;
        core::state::SequencerSettingsState& sequencerSettings;
        core::state::DataManagerState& dataManager;
        core::state::project::ProjectNavigationState& projectNavigation;
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
    void createPropertySelectionOverlay();
    void createActionStrips();
    void createHistoryToast();
    static void onStepGridGeometryInvalidated(void* userData);
    void bindToState();
    void bindHeaderState();
    void bindHeaderStripState();
    void bindGridState();
    void bindSelectorOverlayState();
    void bindOverlayVisibilityState();
    void bindLeftActionStripState();
    void bindBottomActionStripState();
    void bindHistoryFeedbackState();
    bool hasBlockingOverlay() const;
    void handleOverlayVisibilityChanged();

    void ensureRenderTimer();
    void scheduleRender(bool ready = false);
    void pauseRenderTimerIfIdle();
    void requestRender(bool& dirtyFlag);
    void requestHeaderTopRender();
    void requestHeaderStripRender();
    void requestSelectorOverlayRender();
    void requestLeftActionStripRender();
    void requestBottomActionStripRender();
    void requestHistoryFeedbackRender();
    void requestGridRender();
    static void onRenderTimer(lv_timer_t* timer);
    void markAllDirty();
    void render();
    void renderSelectorOverlay();
    void renderHistoryToast();
    sequencer::SequencerViewModelSource modelSource() const;

    StateRefs state_refs_;
    oc::state::SignalWatcher watcher_;

    bool dirty_ = false;
    bool header_top_dirty_ = true;
    bool header_strip_dirty_ = true;
    bool selector_overlay_dirty_ = true;
    bool left_action_strip_dirty_ = true;
    bool bottom_action_strip_dirty_ = true;
    bool history_feedback_dirty_ = true;
    bool grid_dirty_ = true;
    core::app::ExtmemUniquePtr<PausableLvglTimer> render_timer_;

    core::app::ExtmemUniquePtr<core::ui::MainViewFrame> frame_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    lv_obj_t* interaction_container_ = nullptr;
    lv_obj_t* center_column_ = nullptr;

    core::app::ExtmemUniquePtr<core::ui::SequencerHeaderBar> header_bar_;
    core::app::ExtmemUniquePtr<core::ui::StepPropertySelectionOverlay>
        property_selection_overlay_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip> left_action_strip_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip> bottom_action_strip_;
    core::app::ExtmemUniquePtr<core::ui::StepGrid> step_grid_;
    lv_obj_t* history_toast_ = nullptr;
    lv_obj_t* history_toast_line1_ = nullptr;
    lv_obj_t* history_toast_line2_ = nullptr;
    lv_obj_t* history_toast_line3_ = nullptr;
};

}  // namespace core::ui

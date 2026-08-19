#pragma once

/**
 * @file SequencerView.hpp
 * @brief Standalone sequencer view
 */

#include <lvgl.h>

#include <oc/state/StaticSignalWatcher.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/StatusBarState.hpp"
#include "state/DeviceSettingsState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/ViewSelectorState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "ui/sequencer/SequencerHeaderBar.hpp"
#include "ui/sequencer/SequencerCcLaneGrid.hpp"
#include "ui/sequencer/DrumOverviewSurface.hpp"
#include "ui/sequencer/SequencerTrackPastePreflightCard.hpp"
#include "ui/sequencer/SequencerViewModelBuilder.hpp"
#include "ui/sequencer/StepPropertySelectionOverlay.hpp"
#include "ui/sequencer/StepGrid.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
#include "ui/common/StructureSelectionInvalidation.hpp"
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/view/MainViewFrame.hpp"

namespace core::ui {

class SequencerView : public oc::ui::lvgl::IView {
public:
    struct StateRefs {
        const core::state::sequencer::SequencerState& sequencer;
        const core::state::sequencer::SequencerTrackBankState& tracks;
        const core::state::project::ProjectTrackState& projectTracks;
        const core::state::TrackNavigationState& trackNavigation;
        const oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& structureNavigationFocus;
        const oc::state::Signal<uint8_t, 8>& sharedTrackActive;
        const oc::state::Signal<uint16_t, 16>& sharedTrackEnabledMask;
        const core::state::StructureClipboardState& structureClipboard;
        const core::state::StatusBarState& statusBar;
        const core::state::ViewSelectorState& viewSelector;
        const core::state::DeviceSettingsState& deviceSettings;
        const core::state::project::ProjectNavigationState& projectNavigation;
        const core::state::sequencer::SequencerTrackActivationQueue& trackActivations;
    };

    explicit SequencerView(lv_obj_t* parent, StateRefs stateRefs);
    ~SequencerView() override;

    void onActivate() override;
    void onDeactivate() override;
    [[nodiscard]] bool valid() const { return initialized_; }
    const char* getViewId() const override { return "core.sequencer"; }
    lv_obj_t* getElement() const override { return container_; }

private:
    enum RenderFlag : uint32_t {
        RENDER_HEADER_TOP = 1U << 0,
        RENDER_HEADER_STRIP = 1U << 1,
        RENDER_SELECTOR_OVERLAY = 1U << 2,
        RENDER_LEFT_ACTION_STRIP = 1U << 3,
        RENDER_BOTTOM_ACTION_STRIP = 1U << 4,
        RENDER_HISTORY_FEEDBACK = 1U << 5,
        RENDER_GRID = 1U << 6,
        RENDER_TRACK_PASTE_PREFLIGHT = 1U << 7,
    };
    static constexpr uint32_t RENDER_ALL =
        RENDER_HEADER_TOP | RENDER_HEADER_STRIP | RENDER_SELECTOR_OVERLAY |
        RENDER_LEFT_ACTION_STRIP | RENDER_BOTTOM_ACTION_STRIP |
        RENDER_HISTORY_FEEDBACK | RENDER_GRID | RENDER_TRACK_PASTE_PREFLIGHT;

    void createLayout(lv_obj_t* parent);
    void createHeaderBar();
    void createGrid();
    void createPropertySelectionOverlay();
    void createActionStrips();
    void createHistoryToast();
    void createTrackPastePreflightCard();
    static void onStepGridGeometryInvalidated(void* userData);
    bool bindToState();
    void bindHeaderState();
    void bindHeaderStripState();
    bool bindStructureSelectionState();
    void bindGridState();
    void bindSelectorOverlayState();
    void bindOverlayVisibilityState();
    void bindLeftActionStripState();
    void bindBottomActionStripState();
    void bindHistoryFeedbackState();
    void bindTrackSwitchReadyState();
    void bindTrackPastePreflightState();
    void bindClipboardState();
    bool hasBlockingOverlay() const;
    void handleOverlayVisibilityChanged();

    void ensureRenderScheduler();
    void requestRender(uint32_t flags, bool ready = false);
    void resumePendingRender();
    void requestHeaderTopRender();
    void requestHeaderStripRender();
    void requestHeaderAndLeftRender();
    void requestHeaderStripAndLeftRender();
    void requestStructureSelectionRender();
    void requestSelectorOverlayRender();
    void requestLeftActionStripRender();
    void requestBottomActionStripRender();
    void requestHistoryFeedbackRender();
    void requestGridRender();
    void requestGridTickRender();
    void requestTrackPastePreflightRender();
    void requestClipboardDependentRenders();
    static bool canDrainRender(void* context);
    static void drainRender(void* context, uint32_t flags);
    void markAllDirty();
    void render(uint32_t flags);
    void renderSelectorOverlay();
    void renderHistoryToast();
    sequencer::SequencerViewModelSource modelSource() const;

    StateRefs state_refs_;
    oc::state::StaticWatchGroup<15> header_watcher_;
    oc::state::StaticWatchGroup<14> header_strip_watcher_;
    oc::state::StaticWatchGroup<
        2U * core::ui::STRUCTURE_SELECTION_INVALIDATION_SIGNAL_COUNT>
        structure_selection_watcher_;
    oc::state::StaticWatchGroup<45> grid_watcher_;
    oc::state::StaticWatchGroup<1> grid_tick_watcher_;
    oc::state::StaticWatchGroup<26> selector_overlay_watcher_;
    oc::state::StaticWatchGroup<5> overlay_visibility_watcher_;
    oc::state::StaticWatchGroup<11> left_action_strip_watcher_;
    oc::state::StaticWatchGroup<24> bottom_action_strip_watcher_;
    oc::state::StaticWatchGroup<2> history_feedback_watcher_;
    oc::state::StaticWatchGroup<1> track_switch_ready_watcher_;
    oc::state::StaticWatchGroup<9> track_paste_preflight_watcher_;
    oc::state::StaticWatchGroup<1> clipboard_watcher_;

    core::app::ExtmemUniquePtr<core::ui::CoalescedLvglRenderScheduler>
        render_scheduler_;

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
    core::app::ExtmemUniquePtr<core::ui::SequencerCcLaneGrid> cc_lane_grid_;
    core::app::ExtmemUniquePtr<core::ui::sequencer::DrumOverviewSurface>
        drum_overview_surface_;
    core::app::ExtmemUniquePtr<
        core::ui::sequencer::SequencerTrackPastePreflightCard>
        track_paste_preflight_card_;
    lv_obj_t* history_toast_ = nullptr;
    lv_obj_t* history_toast_line1_ = nullptr;
    lv_obj_t* history_toast_line2_ = nullptr;
    lv_obj_t* history_toast_line3_ = nullptr;
    bool pitch_feedback_header_visible_ = false;
    bool initialized_ = false;
};

}  // namespace core::ui

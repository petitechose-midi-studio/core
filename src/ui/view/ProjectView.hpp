#pragma once

#include <array>

#include <lvgl.h>

#include <ms/ui/widget/MenuListView.hpp>
#include <oc/state/SignalWatcher.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/MidiSyncState.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/StatusBarState.hpp"
#include "ui/view/MainViewFrame.hpp"
#include "ui/view/PausableLvglTimer.hpp"

namespace core::ui {

class ProjectView : public oc::ui::lvgl::IView {
public:
    struct StateRefs {
        core::state::project::ProjectNavigationState& navigation;
        core::state::project::ProjectState& project;
        core::state::sequencer::SequencerTrackBankState& sequencerTracks;
        core::state::StatusBarState& statusBar;
        core::state::MidiSyncState& midiSync;
    };

    ProjectView(lv_obj_t* parent, StateRefs stateRefs);
    ~ProjectView() override;

    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.project"; }
    lv_obj_t* getElement() const override { return container_; }

private:
    void createLayout(lv_obj_t* parent);
    void bindToState();
    void requestRender();
    void scheduleRender(bool ready = false);
    void pauseRenderTimerIfIdle();
    void render();
    void renderTabs();
    static void onRenderTimer(lv_timer_t* timer);

    StateRefs state_refs_;
    oc::state::SignalWatcher watcher_;
    bool dirty_ = true;
    core::app::ExtmemUniquePtr<core::ui::PausableLvglTimer> render_timer_;

    core::app::ExtmemUniquePtr<core::ui::MainViewFrame> frame_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    lv_obj_t* tab_strip_ = nullptr;
    struct TabWidgets {
        lv_obj_t* container = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* label = nullptr;
        bool contentInitialized = false;
        bool styleInitialized = false;
        bool active = false;
        bool holdActive = false;
    };
    std::array<TabWidgets, core::state::project::projectTabCount()> tab_widgets_{};
    bool tabs_rendered_ = false;
    core::state::project::ProjectTab rendered_active_tab_ =
        core::state::project::ProjectTab::COUNT;
    bool rendered_hold_active_ = false;
    core::app::ExtmemUniquePtr<ms::ui::MenuListView> menu_;
    std::array<ms::ui::MenuRow, core::state::project::ProjectMenuPage::MAX_ROWS> rows_{};
};

}  // namespace core::ui

#pragma once

#include <array>
#include <optional>

#include <lvgl.h>

#include <ms/ui/widget/MenuListView.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <oc/state/StaticSignalWatcher.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/MidiSyncState.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectState.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/StatusBarState.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
#include "ui/project/ProjectModulatorWorkspace.hpp"
#include "ui/interaction/TextKeyboardView.hpp"
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/view/MainViewFrame.hpp"

namespace core::ui {

class ProjectView : public oc::ui::lvgl::IView {
public:
    struct StateRefs {
        const core::state::project::ProjectNavigationState& navigation;
        const core::state::project::ProjectState& project;
        const core::state::macro::MacroPagesState& pages;
        const core::state::macro::MacroUiState& macroUi;
        const core::state::project::ProjectTrackState& projectTracks;
        const core::state::sequencer::SequencerTrackBankState& sequencerTracks;
        const core::state::StatusBarState& statusBar;
        const core::state::MidiSyncState& midiSync;
    };

    ProjectView(lv_obj_t* parent, StateRefs stateRefs);
    ~ProjectView() override;

    void onActivate() override;
    void onDeactivate() override;
    [[nodiscard]] bool valid() const { return initialized_; }
    const char* getViewId() const override { return "core.project"; }
    lv_obj_t* getElement() const override { return container_; }

private:
    enum RenderFlag : uint32_t {
        RENDER_CONTENT = 1U << 0,
        RENDER_MODULATOR_CAPTURE = 1U << 1,
    };

    void createLayout(lv_obj_t* parent);
    bool bindToState();
    void requestRender();
    void requestModulatorCaptureRender();
    void render();
    void renderModulatorCapture();
    void renderTabs(bool visible);
    void renderProjectActionStrips(bool keyboardActive);
    void renderModulators();
    void renderModulatorActionStrips(
        const core::state::modulation::ModulatorSourceState* source
    );
    static void populateModulatorRow(
        void* context,
        int index,
        ms::ui::KeyValueRowBuffer& out
    );
    static bool canDrainRender(void* context);
    static void drainRender(void* context, uint32_t flags);

    StateRefs state_refs_;
    oc::state::StaticWatchGroup<12> watcher_;
    oc::state::StaticWatchGroup<1> modulator_capture_watcher_;
    core::app::ExtmemUniquePtr<core::ui::CoalescedLvglRenderScheduler>
        render_scheduler_;

    core::app::ExtmemUniquePtr<core::ui::MainViewFrame> frame_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    lv_obj_t* interaction_container_ = nullptr;
    lv_obj_t* center_column_ = nullptr;
    lv_obj_t* tab_strip_ = nullptr;
    struct TabWidgets {
        lv_obj_t* container = nullptr;
        lv_obj_t* icon = nullptr;
        bool contentInitialized = false;
        bool styleInitialized = false;
        bool active = false;
        bool holdActive = false;
    };
    std::array<TabWidgets, core::state::project::projectRootTabCount()> tab_widgets_{};
    bool tabs_rendered_ = false;
    core::state::project::ProjectTab rendered_active_tab_ =
        core::state::project::ProjectTab::COUNT;
    bool rendered_hold_active_ = false;
    core::app::ExtmemUniquePtr<ms::ui::MenuListView> menu_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay>
        modulator_registry_;
    core::app::ExtmemUniquePtr<core::ui::project::ProjectModulatorWorkspace>
        modulator_workspace_;
    std::optional<core::ui::interaction::TextKeyboardView>
        project_name_keyboard_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip> left_action_strip_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip> bottom_action_strip_;
    std::array<ms::ui::MenuRow, core::state::project::ProjectMenuPage::MAX_ROWS> rows_{};
    bool initialized_ = false;
};

static_assert(
    sizeof(void*) != 4U || sizeof(ProjectView) <= 1088U,
    "Project view exceeds its Teensy PSRAM owner budget"
);

}  // namespace core::ui

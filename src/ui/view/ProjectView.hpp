#pragma once

#include <array>

#include <lvgl.h>

#include <ms/ui/widget/MenuListView.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <oc/state/StaticSignalWatcher.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/MidiSyncState.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectNameKeyboard.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/StatusBarState.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/view/MainViewFrame.hpp"

namespace core::ui {

class ProjectView : public oc::ui::lvgl::IView {
public:
    struct StateRefs {
        core::state::project::ProjectNavigationState& navigation;
        core::state::project::ProjectState& project;
        core::state::macro::MacroPagesState& pages;
        core::state::sequencer::SequencerTrackBankState& sequencerTracks;
        core::state::StatusBarState& statusBar;
        core::state::MidiSyncState& midiSync;
    };

    ProjectView(lv_obj_t* parent, StateRefs stateRefs);
    ~ProjectView() override;

    void onActivate() override;
    void onDeactivate() override;
    [[nodiscard]] bool valid() const { return initialized_; }
    const char* getViewId() const override { return "core.project"; }
    lv_obj_t* getElement() const override { return container_; }

private:
    void createLayout(lv_obj_t* parent);
    bool bindToState();
    void requestRender();
    void render();
    void renderTabs();
    void renderKeyboardActionStrips(bool visible);
    void renderModulators();
    void renderModulatorActionStrips(
        const core::state::modulation::ModulatorSourceState* source
    );
    static void populateModulatorRow(
        void* context,
        int index,
        ms::ui::KeyValueRowBuffer& out
    );
    void createKeyboardLayout();
    void renderKeyboard();
    void renderKeyboardKey(uint8_t index, bool selected, bool force = false);
    void applyKeyboardShiftVisibility(bool shiftActive);
    void setKeyboardVisible(bool visible);
    static bool canDrainRender(void* context);
    static void drainRender(void* context, uint32_t flags);

    StateRefs state_refs_;
    oc::state::StaticWatchGroup<12> watcher_;
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
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay>
        modulator_registry_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip> left_action_strip_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip> bottom_action_strip_;
    std::array<ms::ui::MenuRow, core::state::project::ProjectMenuPage::MAX_ROWS> rows_{};
    lv_obj_t* keyboard_container_ = nullptr;
    lv_obj_t* keyboard_title_ = nullptr;
    lv_obj_t* keyboard_meta_ = nullptr;
    lv_obj_t* keyboard_name_box_ = nullptr;
    lv_obj_t* keyboard_name_label_ = nullptr;
    struct KeyboardKeyWidgets {
        lv_obj_t* container = nullptr;
        lv_obj_t* label = nullptr;
        lv_obj_t* shiftLabel = nullptr;
        bool styleInitialized = false;
        bool selected = false;
        bool shiftVisible = false;
    };
    std::array<
        KeyboardKeyWidgets,
        core::state::project::PROJECT_NAME_KEYBOARD_CELL_COUNT
    > keyboard_keys_{};
    bool keyboard_visible_ = false;
    uint8_t rendered_keyboard_selected_ =
        core::state::project::PROJECT_NAME_KEYBOARD_CELL_COUNT;
    bool rendered_keyboard_shift_ = false;
    bool initialized_ = false;
};

}  // namespace core::ui

#pragma once

#include <array>

#include <lvgl.h>
#include <ms/ui/widget/MenuListView.hpp>
#include <oc/state/StaticSignalWatcher.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/DeviceSettingsState.hpp"
#include "state/MidiNoteDisplayState.hpp"
#include "state/MidiSyncState.hpp"
#include "state/settings/DeviceSettingsMenuModel.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
#include "ui/view/MainViewFrame.hpp"

namespace core::ui {

class DeviceSettingsView : public oc::ui::lvgl::IView {
public:
    struct StateRefs {
        const core::state::DeviceSettingsState& settings;
        const core::state::MidiSyncState& midiSync;
        const core::state::MidiNoteDisplayState& midiNoteDisplay;
    };

    DeviceSettingsView(lv_obj_t* parent, StateRefs stateRefs);
    ~DeviceSettingsView() override;

    void onActivate() override;
    void onDeactivate() override;
    [[nodiscard]] bool valid() const { return initialized_; }
    const char* getViewId() const override { return "core.device_settings"; }
    lv_obj_t* getElement() const override { return container_; }

private:
    void createLayout(lv_obj_t* parent);
    bool bindToState();
    void requestRender();
    void render();
    static bool canDrainRender(void* context);
    static void drainRender(void* context, uint32_t flags);

    StateRefs state_refs_;
    oc::state::StaticWatchGroup<9> watcher_;
    core::app::ExtmemUniquePtr<core::ui::CoalescedLvglRenderScheduler>
        render_scheduler_;

    core::app::ExtmemUniquePtr<core::ui::MainViewFrame> frame_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    core::app::ExtmemUniquePtr<ms::ui::MenuListView> menu_;
    std::array<ms::ui::MenuRow, core::state::settings::DeviceSettingsMenuPage::MAX_ROWS> rows_{};
    bool initialized_ = false;
};

}  // namespace core::ui

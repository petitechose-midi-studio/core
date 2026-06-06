#pragma once

#include <array>

#include <lvgl.h>
#include <ms/ui/widget/MenuListView.hpp>
#include <oc/state/SignalWatcher.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/DeviceSettingsState.hpp"
#include "state/MidiSyncState.hpp"
#include "state/settings/DeviceSettingsMenuModel.hpp"
#include "ui/view/MainViewFrame.hpp"
#include "ui/view/PausableLvglTimer.hpp"

namespace core::ui {

class DeviceSettingsView : public oc::ui::lvgl::IView {
public:
    struct StateRefs {
        core::state::DeviceSettingsState& settings;
        core::state::MidiSyncState& midiSync;
    };

    DeviceSettingsView(lv_obj_t* parent, StateRefs stateRefs);
    ~DeviceSettingsView() override;

    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.device_settings"; }
    lv_obj_t* getElement() const override { return container_; }

private:
    void createLayout(lv_obj_t* parent);
    void bindToState();
    void requestRender();
    void scheduleRender(bool ready = false);
    void pauseRenderTimerIfIdle();
    void render();
    static void onRenderTimer(lv_timer_t* timer);

    StateRefs state_refs_;
    oc::state::SignalWatcher watcher_;
    bool dirty_ = true;
    core::app::ExtmemUniquePtr<core::ui::PausableLvglTimer> render_timer_;

    core::app::ExtmemUniquePtr<core::ui::MainViewFrame> frame_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    core::app::ExtmemUniquePtr<ms::ui::MenuListView> menu_;
    std::array<ms::ui::MenuRow, core::state::settings::DeviceSettingsMenuPage::MAX_ROWS> rows_{};
};

}  // namespace core::ui

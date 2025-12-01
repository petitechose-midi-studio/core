#include "ViewManager.hpp"

#include <lvgl.h>

#include "adapter/display/ui/LVGLBridge.hpp"
#include "config/System.hpp"
#include "log/Macros.hpp"
#include "interface/IView.hpp"
#include "core/event/Events.hpp"
#include "core/event/IEventBus.hpp"

ViewManager::ViewManager(LVGLBridge& displayBridge, IEventBus& eventBus)
    : display_bridge_(displayBridge), event_bus_(eventBus) {}

void ViewManager::initScreens() {
    if (screens_initialized_) return;

    LOGLN("[ViewManager] Creating screens...");

    core_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(core_screen_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(core_screen_, 0, 0);

    plugin_screen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(plugin_screen_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(plugin_screen_, 0, 0);

    lv_scr_load(core_screen_);
    screens_initialized_ = true;
}

void ViewManager::initSplash() {
    if (!screens_initialized_ || splash_view_.has_value()) return;

    LOGLN("[ViewManager] Creating splash...");
    splash_view_.emplace(core_screen_);
    splash_view_->init();
    showCoreSplash();
}

void ViewManager::emitBootComplete() {
    if (boot_complete_emitted_) return;
    LOGLN("[ViewManager] BootComplete");
    event_bus_.emit(SystemBootCompleteEvent());
    boot_complete_emitted_ = true;
}

void ViewManager::update() {
    if (!System::UI::ENABLE_FULL_UI || !screens_initialized_) return;

    if (current_plugin_view_) {
        display_bridge_.refresh();
    } else if (splash_view_ && splash_view_->isActive()) {
        splash_view_->update();
        if (!boot_complete_emitted_ && splash_view_->isSplashScreenCompleted()) {
            emitBootComplete();
        }
        display_bridge_.refresh();
    }
}

void ViewManager::showCoreSplash() {
    if (splash_view_) splash_view_->setActive(true);
}

void ViewManager::hideCoreSplash() {
    if (splash_view_) splash_view_->setActive(false);
}

lv_obj_t* ViewManager::getPluginContainer() {
    return plugin_screen_;
}

void ViewManager::showPluginView(IView& view) {
    hideCoreSplash();
    current_plugin_view_ = &view;
    view.onActivate();
    lv_scr_load(plugin_screen_);
}

void ViewManager::hidePluginView() {
    if (current_plugin_view_) {
        current_plugin_view_->onDeactivate();
        current_plugin_view_ = nullptr;
    }
    showCoreSplash();
    lv_scr_load(core_screen_);
}

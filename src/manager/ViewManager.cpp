#include "ViewManager.hpp"

#include <lvgl.h>

#include "adapter/display/ui/LVGLBridge.hpp"
#include "config/System.hpp"
#include "log/Macros.hpp"
#include "interface/IView.hpp"
#include "core/event/Events.hpp"
#include "core/event/IEventBus.hpp"

ViewManager::ViewManager(LVGLBridge& displayBridge, IEventBus& eventBus)
    : displayBridge_(displayBridge), eventBus_(eventBus) {}

void ViewManager::initScreens() {
    if (screensInitialized_) return;

    LOGLN("[ViewManager] Creating screens...");

    coreScreen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(coreScreen_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(coreScreen_, 0, 0);

    pluginScreen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(pluginScreen_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(pluginScreen_, 0, 0);

    lv_scr_load(coreScreen_);
    screensInitialized_ = true;
}

void ViewManager::initSplash() {
    if (!screensInitialized_ || splashView_.has_value()) return;

    LOGLN("[ViewManager] Creating splash...");
    splashView_.emplace(coreScreen_);
    splashView_->init();
    showCoreSplash();
}

void ViewManager::emitBootComplete() {
    if (bootCompleteEmitted_) return;
    LOGLN("[ViewManager] BootComplete");
    eventBus_.emit(SystemBootCompleteEvent());
    bootCompleteEmitted_ = true;
}

void ViewManager::update() {
    if (!System::UI::ENABLE_FULL_UI || !screensInitialized_) return;

    if (currentPluginView_) {
        displayBridge_.refresh();
    } else if (splashView_ && splashView_->isActive()) {
        splashView_->update();
        if (!bootCompleteEmitted_ && splashView_->isSplashScreenCompleted()) {
            emitBootComplete();
        }
        displayBridge_.refresh();
    }
}

void ViewManager::showCoreSplash() {
    if (splashView_) splashView_->setActive(true);
}

void ViewManager::hideCoreSplash() {
    if (splashView_) splashView_->setActive(false);
}

lv_obj_t* ViewManager::getPluginContainer() {
    return pluginScreen_;
}

void ViewManager::showPluginView(UI::IView& view) {
    hideCoreSplash();
    currentPluginView_ = &view;
    view.onActivate();
    lv_scr_load(pluginScreen_);
}

void ViewManager::hidePluginView() {
    if (currentPluginView_) {
        currentPluginView_->onDeactivate();
        currentPluginView_ = nullptr;
    }
    showCoreSplash();
    lv_scr_load(coreScreen_);
}

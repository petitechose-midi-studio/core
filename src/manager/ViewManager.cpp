#include "ViewManager.hpp"

#include <lvgl.h>

#include "adapter/display/ui/LVGLBridge.hpp"
#include "config/System.hpp"
#include "log/Macros.hpp"
#include "resource/common/ui/interface/IView.hpp"
#include "resource/common/ui/font/binary_font_buffer.hpp"
#include "core/event/Events.hpp"
#include "core/event/IEventBus.hpp"

ViewManager::ViewManager(LVGLBridge& displayBridge, IEventBus& eventBus)
    : displayBridge_(displayBridge), eventBus_(eventBus) {

    load_fonts();

    // Create 2 screens managed by Core
    coreScreen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(coreScreen_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(coreScreen_, 0, 0);

    pluginScreen_ = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(pluginScreen_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(pluginScreen_, 0, 0);

    // Create and show splash view on coreScreen_
    splashView_.emplace(coreScreen_);
    if (splashView_) {
        splashView_->init();
        showCoreSplash();
    }

    // Load coreScreen at boot
    lv_scr_load(coreScreen_);
}

void ViewManager::update() {
    if (!System::UI::ENABLE_FULL_UI) {
        return;
    }

    if (currentPluginView_) {
        // Plugin view is active
        displayBridge_.refresh();
    } else if (splashView_ && splashView_->isActive()) {
        // Core splash is active
        splashView_->update();

        // Emit BootComplete once when splash is complete
        if (!bootCompleteEmitted_ && splashView_->isSplashScreenCompleted()) {
            LOGLN("[ViewManager] Splash complete - Emitting BootComplete event");
            eventBus_.emit(SystemBootCompleteEvent());
            bootCompleteEmitted_ = true;
        }

        displayBridge_.refresh();
    }
}

void ViewManager::showCoreSplash() {
    splashView_->setActive(true);
}

void ViewManager::hideCoreSplash() {
    splashView_->setActive(false);
}

/*
 * Plugin View Management
 */
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
    // Deactivate current plugin view if any
    if (currentPluginView_) {
        currentPluginView_->onDeactivate();
        currentPluginView_ = nullptr;
    }

    // Return to Core screen and show splash
    showCoreSplash();
    lv_scr_load(coreScreen_);
}

/*
 * ViewManager - 2-Screen Architecture
 *
 * Manages display of Core and Plugin views using two dedicated LVGL screens:
 * - coreScreen_: For Core views (splash, menus, settings)
 * - pluginScreen_: For all Plugin views
 *
 * Architecture:
 * - ViewManager owns both screens (created at boot, never destroyed)
 * - Plugins receive pluginScreen_ via getPluginContainer()
 * - Screen switching is done via lv_scr_load()
 */

#pragma once

#include <etl/optional.h>

#include "ui/view/SplashScreenView.hpp"

class LVGLBridge;
class IEventBus;

namespace UI {
class IView;
}

class ViewManager {
public:
    explicit ViewManager(LVGLBridge& displayBridge, IEventBus& eventBus);

    void update();

    /**
     * @brief Get plugin screen where plugins should create their UI
     * @return LVGL screen for plugin views
     */
    lv_obj_t* getPluginContainer();

    /**
     * @brief Show a plugin view (loads pluginScreen_)
     * @param view Reference to IView implementation (plugin keeps ownership)
     */
    void showPluginView(UI::IView& view);

    /**
     * @brief Hide current plugin view and return to Core (loads coreScreen_)
     */
    void hidePluginView();

private:
    void showCoreSplash();
    void hideCoreSplash();

    bool bootCompleteEmitted_ = false;

    LVGLBridge& displayBridge_;
    IEventBus& eventBus_;

    lv_obj_t* coreScreen_;    // Screen for Core views (splash, menus, etc.)
    lv_obj_t* pluginScreen_;  // Screen for Plugin views

    etl::optional<SplashScreenView> splashView_;

    UI::IView* currentPluginView_ = nullptr;
};
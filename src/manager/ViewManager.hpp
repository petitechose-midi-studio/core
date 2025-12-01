#pragma once

#include <optional>

#include "ui/view/SplashScreenView.hpp"

class LVGLBridge;
class IEventBus;

class IView;

class ViewManager {
public:
    explicit ViewManager(LVGLBridge& displayBridge, IEventBus& eventBus);

    void initScreens();
    void initSplash();
    void update();

    bool isInitialized() const { return screens_initialized_; }
    bool isSplashInitialized() const { return splash_view_.has_value(); }

    lv_obj_t* getPluginContainer();
    void showPluginView(IView& view);
    void hidePluginView();

    SplashScreenView* getSplashView() {
        return splash_view_.has_value() ? &splash_view_.value() : nullptr;
    }

    void emitBootComplete();

private:
    void showCoreSplash();
    void hideCoreSplash();

    bool screens_initialized_ = false;
    bool boot_complete_emitted_ = false;

    LVGLBridge& display_bridge_;
    IEventBus& event_bus_;

    lv_obj_t* core_screen_ = nullptr;
    lv_obj_t* plugin_screen_ = nullptr;

    std::optional<SplashScreenView> splash_view_;
    IView* current_plugin_view_ = nullptr;
};

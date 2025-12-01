#pragma once

#include <optional>

#include "ui/view/SplashScreenView.hpp"

class LVGLBridge;
class IEventBus;

namespace UI { class IView; }

class ViewManager {
public:
    explicit ViewManager(LVGLBridge& displayBridge, IEventBus& eventBus);

    void initScreens();
    void initSplash();
    void update();

    bool isInitialized() const { return screensInitialized_; }
    bool isSplashInitialized() const { return splashView_.has_value(); }

    lv_obj_t* getPluginContainer();
    void showPluginView(UI::IView& view);
    void hidePluginView();

    SplashScreenView* getSplashView() {
        return splashView_.has_value() ? &splashView_.value() : nullptr;
    }

    void emitBootComplete();

private:
    void showCoreSplash();
    void hideCoreSplash();

    bool screensInitialized_ = false;
    bool bootCompleteEmitted_ = false;

    LVGLBridge& displayBridge_;
    IEventBus& eventBus_;

    lv_obj_t* coreScreen_ = nullptr;
    lv_obj_t* pluginScreen_ = nullptr;

    std::optional<SplashScreenView> splashView_;
    UI::IView* currentPluginView_ = nullptr;
};

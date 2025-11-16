#pragma once

#include <Arduino.h>
#include <lvgl.h>

class LVGLBridge;

/**
 * @brief Core splash screen view
 *
 * Displays application logo, title, version and progress bar during boot.
 * Created on coreScreen_ by ViewManager.
 */
class SplashScreenView {
public:
    struct Config {
        String title;
        String subtitle;
        String version;
        unsigned long duration;
        lv_color_t bg_color;
        lv_color_t text_color;
        lv_color_t progress_color;

        Config();
    };

    explicit SplashScreenView(lv_obj_t* parentScreen, const Config& config = Config());
    ~SplashScreenView();

    bool init();
    void update();

    bool isActive() const;
    void setActive(bool active);
    bool isSplashScreenCompleted() const;

private:
    Config config_;

    bool initialized_;
    bool active_;
    unsigned long start_time_;

    lv_obj_t* parentScreen_;  // Parent screen provided by ViewManager (non-owned)
    lv_obj_t* container_;     // Container created on parentScreen_
    lv_obj_t* title_label_;
    lv_obj_t* subtitle_label_;
    lv_obj_t* version_label_;
    lv_obj_t* progress_bar_;

    void setupContainer();
    void setupLabels();
    void setupProgressBar();
    void updateProgressBar();
    void cleanupLvglObjects();
};

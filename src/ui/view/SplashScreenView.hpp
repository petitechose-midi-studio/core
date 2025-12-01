#pragma once

#include <Arduino.h>
#include <lvgl.h>

class SplashScreenView {
public:
    struct Config {
        String title;
        String version;
        lv_color_t bg_color;
        lv_color_t text_color;
        lv_color_t progress_color;

        Config();
    };

    explicit SplashScreenView(lv_obj_t* parentScreen, const Config& config = Config());
    ~SplashScreenView();

    bool init();
    void update();

    bool isActive() const { return active_; }
    void setActive(bool active);
    bool isSplashScreenCompleted() const { return boot_complete_; }

    void setBootMode(bool enabled);
    void setBootProgress(uint8_t progress);
    void setBootStatus(const char* status);
    void markBootComplete();

private:
    Config config_;
    bool initialized_ = false;
    bool active_ = false;
    bool boot_mode_ = false;
    bool boot_complete_ = false;

    lv_obj_t* parent_screen_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* version_label_ = nullptr;
    lv_obj_t* progress_bar_ = nullptr;
    lv_obj_t* status_label_ = nullptr;

    void setupContainer();
    void setupLabels();
    void setupProgressBar();
    void setupStatusLabel();
    void cleanupLvglObjects();
};

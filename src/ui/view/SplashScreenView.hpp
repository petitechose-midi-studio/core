#pragma once

#include <string>

#include <lvgl.h>

#include <oc/ui/lvgl/IView.hpp>

class SplashScreenView : public oc::ui::lvgl::IView {
public:
    struct Config {
        std::string title;
        std::string version;
        lv_color_t bg_color;
        lv_color_t text_color;
        lv_color_t progress_color;

        Config();
    };

    explicit SplashScreenView(lv_obj_t* parent, const Config& config = Config());
    ~SplashScreenView() override;

    // IView interface
    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.splash"; }
    lv_obj_t* getElement() const override { return container_; }

    void setProgress(uint8_t progress);
    void fadeOut(uint32_t durationMs);

private:
    Config config_;
    lv_obj_t* container_;
    lv_obj_t* title_label_;
    lv_obj_t* version_label_;
    lv_obj_t* progress_bar_;

    void createContainer(lv_obj_t* parent);
    void createLogo();
    void createLabels();
    void createProgressBar();
};

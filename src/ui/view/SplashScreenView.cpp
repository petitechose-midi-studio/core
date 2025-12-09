#include "SplashScreenView.hpp"

#include "config/App.hpp"
#include "ui/font/FontLoader.hpp"
#include "ui/theme/BaseTheme.hpp"

namespace App = Config::App;

SplashScreenView::Config::Config()
    : title(App::NAME), version(App::VERSION),
      bg_color(lv_color_hex(BaseTheme::Color::BACKGROUND)),
      text_color(lv_color_hex(BaseTheme::Color::TEXT_PRIMARY)),
      progress_color(lv_color_hex(BaseTheme::Color::TEXT_PRIMARY)) {}

SplashScreenView::SplashScreenView(lv_obj_t* parent, const Config& config)
    : config_(config) {
    createContainer(parent);
    createLogo();
    createLabels();
    createProgressBar();
}

SplashScreenView::~SplashScreenView() {
    lv_obj_delete(container_);
}

void SplashScreenView::onActivate() {
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void SplashScreenView::onDeactivate() {
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void SplashScreenView::setProgress(uint8_t progress) {
    lv_bar_set_value(progress_bar_, progress > 100 ? 100 : progress, LV_ANIM_OFF);
}

void SplashScreenView::fadeOut(uint32_t durationMs) {
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, container_);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&anim, durationMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, [](void* obj, int32_t v) {
        lv_obj_set_style_opa_layered(static_cast<lv_obj_t*>(obj), v, 0);
    });
    lv_anim_start(&anim);
}

void SplashScreenView::createContainer(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container_, config_.bg_color, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
}

void SplashScreenView::createLogo() {
    lv_obj_t* logo = lv_obj_create(container_);
    lv_obj_set_size(logo, 159, 159);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -28);
    lv_obj_set_style_bg_opa(logo, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logo, 0, 0);
    lv_obj_set_style_pad_all(logo, 0, 0);

    // Ring
    lv_obj_t* ring = lv_obj_create(logo);
    lv_obj_set_size(ring, 100, 100);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(ring, config_.text_color, 0);
    lv_obj_set_style_border_width(ring, 14, 0);

    // Tail mask
    static lv_point_precise_t mask_pts[] = {{79, 79}, {114, 114}};
    lv_obj_t* mask = lv_line_create(logo);
    lv_line_set_points(mask, mask_pts, 2);
    lv_obj_set_style_line_width(mask, 22, 0);
    lv_obj_set_style_line_color(mask, config_.bg_color, 0);
    lv_obj_set_style_line_rounded(mask, true, 0);

    // Tail
    static lv_point_precise_t tail_pts[] = {{79, 79}, {114, 114}};
    lv_obj_t* tail = lv_line_create(logo);
    lv_line_set_points(tail, tail_pts, 2);
    lv_obj_set_style_line_width(tail, 14, 0);
    lv_obj_set_style_line_color(tail, config_.text_color, 0);
    lv_obj_set_style_line_rounded(tail, true, 0);

    // Dot
    lv_obj_t* dot = lv_obj_create(logo);
    lv_obj_set_size(dot, 24, 24);
    lv_obj_center(dot);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, config_.text_color, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
}

void SplashScreenView::createLabels() {
    title_label_ = lv_label_create(container_);
    lv_label_set_text(title_label_, config_.title.c_str());
    lv_obj_set_style_text_color(title_label_, config_.text_color, 0);
    if (fonts.splash_title) {
        lv_obj_set_style_text_font(title_label_, fonts.splash_title, 0);
    }
    lv_obj_align(title_label_, LV_ALIGN_CENTER, 0, 47);

    version_label_ = lv_label_create(container_);
    lv_label_set_text(version_label_, config_.version.c_str());
    lv_obj_set_style_text_color(version_label_, config_.text_color, 0);
    if (fonts.splash_version) {
        lv_obj_set_style_text_font(version_label_, fonts.splash_version, 0);
    }
    lv_obj_align(version_label_, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

void SplashScreenView::createProgressBar() {
    lv_obj_t* container = lv_obj_create(container_);
    lv_obj_set_size(container, 200, 12);
    lv_obj_set_pos(container, (320 - 200) / 2, 195);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(container, config_.progress_color, 0);
    lv_obj_set_style_border_width(container, 1, 0);
    lv_obj_set_style_radius(container, 8, 0);
    lv_obj_set_style_pad_all(container, 1, 0);

    progress_bar_ = lv_bar_create(container);
    lv_obj_set_size(progress_bar_, LV_PCT(100), LV_PCT(100));
    lv_obj_center(progress_bar_);
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(progress_bar_, 0, 0);
    lv_obj_set_style_radius(progress_bar_, 6, 0);
    lv_obj_set_style_bg_color(progress_bar_, config_.progress_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_bar_, 6, LV_PART_INDICATOR);

    lv_bar_set_range(progress_bar_, 0, 100);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
}

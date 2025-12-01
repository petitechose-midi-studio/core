#include "SplashScreenView.hpp"

#include "log/Macros.hpp"
#include "ui/shared/font/FontLoader.hpp"
#include "theme/BaseTheme.hpp"
#include "config/System.hpp"

SplashScreenView::Config::Config()
    : title(System::Application::NAME),
      version(System::Application::VERSION),
      bg_color(lv_color_hex(BaseTheme::Color::BACKGROUND)),
      text_color(lv_color_hex(BaseTheme::Color::TEXT_PRIMARY)),
      progress_color(lv_color_hex(BaseTheme::Color::TEXT_PRIMARY)) {}

SplashScreenView::SplashScreenView(lv_obj_t* parentScreen, const Config& config)
    : config_(config), parent_screen_(parentScreen) {}

SplashScreenView::~SplashScreenView() {
    cleanup();
}

bool SplashScreenView::init() {
    if (initialized_) return true;

    LOGLN("[Splash] Container...");
    createContainer();
    LOGLN("[Splash] Labels...");
    createLabels();
    LOGLN("[Splash] Progress...");
    createProgressBar();
    LOGLN("[Splash] Status...");
    createStatusLabel();

    LOGLN("[Splash] Init OK");
    initialized_ = true;
    return true;
}

void SplashScreenView::update() {
    // Nothing to do - progress is updated via setBootProgress
}

void SplashScreenView::onActivate() {
    if (active_) return;
    active_ = true;

    if (container_) {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SplashScreenView::onDeactivate() {
    if (!active_) return;
    active_ = false;

    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }
}

void SplashScreenView::setBootMode(bool enabled) {
    boot_mode_ = enabled;
    boot_complete_ = false;

    if (status_label_) {
        if (enabled) {
            lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(status_label_, "Starting...");
        } else {
            lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void SplashScreenView::setBootProgress(uint8_t progress) {
    if (!boot_mode_ || !progress_bar_) return;
    lv_bar_set_value(progress_bar_, progress > 100 ? 100 : progress, LV_ANIM_OFF);
}

void SplashScreenView::setBootStatus(const char* status) {
    if (!boot_mode_ || !status_label_) return;
    lv_label_set_text(status_label_, status ? status : "");
}

void SplashScreenView::markBootComplete() {
    boot_complete_ = true;
    if (progress_bar_) lv_bar_set_value(progress_bar_, 100, LV_ANIM_OFF);
    if (status_label_) lv_label_set_text(status_label_, "Ready");
}

void SplashScreenView::createContainer() {
    container_ = lv_obj_create(parent_screen_);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container_, config_.bg_color, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(container_, 0, LV_STATE_DEFAULT);
}

void SplashScreenView::createLabels() {
    // Logo container
    lv_obj_t* logo_container = lv_obj_create(container_);
    lv_obj_set_size(logo_container, 159, 159);
    lv_obj_align(logo_container, LV_ALIGN_CENTER, 0, -28);
    lv_obj_set_style_bg_opa(logo_container, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(logo_container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(logo_container, 0, LV_STATE_DEFAULT);

    // Q ring
    lv_obj_t* logo_ring = lv_obj_create(logo_container);
    lv_obj_set_size(logo_ring, 100, 100);
    lv_obj_center(logo_ring);
    lv_obj_set_style_radius(logo_ring, LV_RADIUS_CIRCLE, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(logo_ring, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(logo_ring, config_.text_color, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(logo_ring, 14, LV_STATE_DEFAULT);

    // Q tail mask
    static lv_point_precise_t tail_mask_points[] = {{79, 79}, {114, 114}};
    lv_obj_t* q_tail_mask = lv_line_create(logo_container);
    lv_line_set_points(q_tail_mask, tail_mask_points, 2);
    lv_obj_set_style_line_width(q_tail_mask, 22, LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(q_tail_mask, config_.bg_color, LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(q_tail_mask, true, LV_STATE_DEFAULT);

    // Q tail
    static lv_point_precise_t tail_points[] = {{79, 79}, {114, 114}};
    lv_obj_t* q_tail = lv_line_create(logo_container);
    lv_line_set_points(q_tail, tail_points, 2);
    lv_obj_set_style_line_width(q_tail, 14, LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(q_tail, config_.text_color, LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(q_tail, true, LV_STATE_DEFAULT);

    // Center dot
    lv_obj_t* center_dot = lv_obj_create(logo_container);
    lv_obj_set_size(center_dot, 24, 24);
    lv_obj_center(center_dot);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(center_dot, config_.text_color, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(center_dot, 0, LV_STATE_DEFAULT);

    // Title
    title_label_ = lv_label_create(container_);
    lv_label_set_text(title_label_, config_.title.c_str());
    lv_obj_set_style_text_color(title_label_, config_.text_color, LV_STATE_DEFAULT);
    if (fonts.splash_title) {
        lv_obj_set_style_text_font(title_label_, fonts.splash_title, LV_STATE_DEFAULT);
    }
    lv_obj_align(title_label_, LV_ALIGN_CENTER, 0, 47);

    // Version
    version_label_ = lv_label_create(container_);
    lv_label_set_text(version_label_, config_.version.c_str());
    lv_obj_set_style_text_color(version_label_, config_.text_color, LV_STATE_DEFAULT);
    if (fonts.splash_version) {
        lv_obj_set_style_text_font(version_label_, fonts.splash_version, LV_STATE_DEFAULT);
    }
    lv_obj_align(version_label_, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
}

void SplashScreenView::createProgressBar() {
    lv_obj_t* progress_container = lv_obj_create(container_);
    lv_obj_set_size(progress_container, 200, 12);
    lv_obj_set_pos(progress_container, (320 - 200) / 2, 195);
    lv_obj_set_style_bg_opa(progress_container, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(progress_container, config_.progress_color, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(progress_container, 1, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(progress_container, 8, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(progress_container, 1, LV_STATE_DEFAULT);

    progress_bar_ = lv_bar_create(progress_container);
    lv_obj_set_size(progress_bar_, lv_pct(100), lv_pct(100));
    lv_obj_center(progress_bar_);

    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(progress_bar_, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(progress_bar_, 6, LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(progress_bar_, config_.progress_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_bar_, 6, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(progress_bar_, config_.bg_color, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(progress_bar_, 2, LV_PART_INDICATOR);

    lv_bar_set_range(progress_bar_, 0, 100);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
}

void SplashScreenView::createStatusLabel() {
    status_label_ = lv_label_create(container_);
    lv_label_set_text(status_label_, "");
    lv_obj_set_style_text_color(status_label_, config_.text_color, LV_STATE_DEFAULT);
    if (fonts.splash_version) {
        lv_obj_set_style_text_font(status_label_, fonts.splash_version, LV_STATE_DEFAULT);
    }
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 100);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
}

void SplashScreenView::cleanup() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        title_label_ = nullptr;
        version_label_ = nullptr;
        progress_bar_ = nullptr;
        status_label_ = nullptr;
    }
}

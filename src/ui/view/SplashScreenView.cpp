#include "SplashScreenView.hpp"

#include "resource/common/ui/font/binary_font_buffer.hpp"
#include "resource/common/ui/theme/BaseTheme.hpp"
#include "config/System.hpp"

SplashScreenView::Config::Config()
    : title(System::Application::NAME),
      version(System::Application::VERSION),
      duration(1000),
      bg_color(lv_color_hex(BaseTheme::Color::BACKGROUND)),
      text_color(lv_color_hex(BaseTheme::Color::TEXT_PRIMARY)),
      progress_color(lv_color_hex(BaseTheme::Color::TEXT_PRIMARY)) {}

SplashScreenView::SplashScreenView(lv_obj_t* parentScreen, const Config& config)
    : config_(config),
      initialized_(false),
      active_(false),
      start_time_(0),
      parentScreen_(parentScreen),
      container_(nullptr),
      title_label_(nullptr),
      subtitle_label_(nullptr),
      version_label_(nullptr),
      progress_bar_(nullptr) {}

SplashScreenView::~SplashScreenView() {
    setActive(false);
    cleanupLvglObjects();
}

bool SplashScreenView::init() {
    if (initialized_) {
        return true;
    }

    setupContainer();
    setupLabels();
    setupProgressBar();

    initialized_ = true;
    return true;
}

void SplashScreenView::update() {
    if (!active_ || !initialized_) {
        return;
    }

    if (start_time_ == 0) {
        start_time_ = millis();
    }

    updateProgressBar();
}

bool SplashScreenView::isActive() const {
    return active_;
}

void SplashScreenView::setActive(bool active) {
    if (active && !active_) {
        active_ = true;
        start_time_ = millis();

        if (container_) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (!active && active_) {
        active_ = false;
        start_time_ = 0;

        if (container_) {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

bool SplashScreenView::isSplashScreenCompleted() const {
    if (!active_) return true;

    unsigned long current_time = millis();
    return (current_time - start_time_ >= config_.duration);
}

void SplashScreenView::setupContainer() {
    // Create container on parent screen
    container_ = lv_obj_create(parentScreen_);
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(container_, config_.bg_color, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_all(container_, 0, 0);
}

void SplashScreenView::setupLabels() {
    lv_obj_t* logo_container = lv_obj_create(container_);
    lv_obj_set_size(logo_container, 159, 159);

    lv_obj_align(logo_container, LV_ALIGN_CENTER, 0, -28);
    lv_obj_set_style_bg_opa(logo_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(logo_container, 0, 0);
    lv_obj_set_style_pad_all(logo_container, 0, 0);

    lv_obj_t* logo_ring = lv_obj_create(logo_container);
    lv_obj_set_size(logo_ring, 100, 100);
    lv_obj_center(logo_ring);
    lv_obj_set_style_radius(logo_ring, LV_RADIUS_CIRCLE, 0);

    lv_obj_set_style_bg_opa(logo_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(logo_ring, config_.text_color, 0);
    lv_obj_set_style_border_width(logo_ring, 14, 0);
    lv_obj_set_style_border_opa(logo_ring, LV_OPA_COVER, 0);

    static lv_point_precise_t tail_mask_points[] = {{79, 79}, {114, 114}};
    lv_obj_t* q_tail_mask = lv_line_create(logo_container);
    lv_line_set_points(q_tail_mask, tail_mask_points, 2);
    lv_obj_set_style_line_width(q_tail_mask, 22, 0);
    lv_obj_set_style_line_color(q_tail_mask, config_.bg_color, 0);
    lv_obj_set_style_line_opa(q_tail_mask, LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(q_tail_mask, true, 0);

    static lv_point_precise_t tail_points[] = {{79, 79}, {114, 114}};
    lv_obj_t* q_tail = lv_line_create(logo_container);
    lv_line_set_points(q_tail, tail_points, 2);
    lv_obj_set_style_line_width(q_tail, 14, 0);
    lv_obj_set_style_line_color(q_tail, config_.text_color, 0);
    lv_obj_set_style_line_opa(q_tail, LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(q_tail, true, 0);

    lv_obj_t* center_dot = lv_obj_create(logo_container);
    lv_obj_set_size(center_dot, 24, 24);
    lv_obj_center(center_dot);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_dot, config_.text_color, 0);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center_dot, 0, 0);

    title_label_ = lv_label_create(container_);
    lv_label_set_text(title_label_, config_.title.c_str());
    lv_obj_set_style_text_color(title_label_, config_.text_color, 0);
    lv_obj_set_style_text_font(title_label_, fonts.splash_title, 0);

    lv_obj_align(title_label_, LV_ALIGN_CENTER, 0, 47);

    version_label_ = lv_label_create(container_);
    lv_label_set_text(version_label_, config_.version.c_str());
    lv_obj_set_style_text_color(version_label_, config_.text_color, 0);
    lv_obj_set_style_text_font(version_label_, fonts.splash_version, 0);

    // Align to bottom-right with 10px padding
    lv_obj_align(version_label_, LV_ALIGN_BOTTOM_RIGHT, -10, -10);

    if (config_.subtitle.length() > 0) {
        subtitle_label_ = lv_label_create(container_);
        lv_label_set_text(subtitle_label_, config_.subtitle.c_str());
        lv_obj_set_style_text_color(subtitle_label_, config_.text_color, 0);

        lv_obj_align_to(subtitle_label_, title_label_, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    } else {
        subtitle_label_ = nullptr;
    }
}

void SplashScreenView::setupProgressBar() {
    lv_obj_t* progress_container = lv_obj_create(container_);
    lv_obj_set_size(progress_container, 200, 12);
    lv_obj_set_pos(progress_container, (320 - 200) / 2, 195);
    lv_obj_set_style_bg_opa(progress_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(progress_container, config_.progress_color, 0);
    lv_obj_set_style_border_width(progress_container, 1, 0);
    lv_obj_set_style_border_opa(progress_container, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(progress_container, 8, 0);
    lv_obj_set_style_pad_all(progress_container, 1, 0);

    progress_bar_ = lv_bar_create(progress_container);
    lv_obj_set_size(progress_bar_, lv_pct(100), lv_pct(100));
    lv_obj_center(progress_bar_);

    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(progress_bar_, 0, 0);
    lv_obj_set_style_radius(progress_bar_, 6, 0);

    lv_obj_set_style_bg_color(progress_bar_, config_.progress_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(progress_bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(progress_bar_, 6, LV_PART_INDICATOR);

    lv_obj_set_style_border_color(progress_bar_, config_.bg_color, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(progress_bar_, 2, LV_PART_INDICATOR);
    lv_obj_set_style_border_opa(progress_bar_, LV_OPA_COVER, LV_PART_INDICATOR);

    lv_bar_set_range(progress_bar_, 0, 100);
    lv_bar_set_value(progress_bar_, 0, LV_ANIM_OFF);
}

void SplashScreenView::updateProgressBar() {
    if (!progress_bar_ || start_time_ == 0) {
        return;
    }

    unsigned long current_time = millis();
    unsigned long elapsed = current_time - start_time_;

    if (elapsed <= config_.duration) {
        int progress = (elapsed * 100) / config_.duration;
        lv_bar_set_value(progress_bar_, progress, LV_ANIM_OFF);
    } else {
        lv_bar_set_value(progress_bar_, 100, LV_ANIM_OFF);
    }
}

void SplashScreenView::cleanupLvglObjects() {
    if (container_) {
        lv_obj_del(container_);
        container_ = nullptr;
        title_label_ = nullptr;
        subtitle_label_ = nullptr;
        version_label_ = nullptr;
        progress_bar_ = nullptr;
    }
}

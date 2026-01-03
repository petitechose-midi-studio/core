#include "TopBar.hpp"

#include <oc/state/Bind.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

#include "ui/font/CoreFonts.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace ui {

namespace Theme = standalone::theme;

TopBar::TopBar(lv_obj_t* parent, state::StatusBarState& state)
    : state_(state) {
    createLayout(parent);
    setupBindings();
}

TopBar::~TopBar() {
    if (container_) {
        lv_obj_delete(container_);
    }
}

void TopBar::createLayout(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, LV_PCT(100), Theme::Layout::TOP_BAR_HEIGHT);
    lv_obj_set_style_bg_color(container_, lv_color_hex(Theme::Color::BACKGROUND), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);

    label_ = lv_label_create(container_);
    lv_obj_set_style_text_font(label_, fonts.inter_14_medium, 0);
    lv_obj_set_style_text_color(label_, lv_color_hex(Theme::Color::TEXT_SECONDARY), 0);
    lv_obj_center(label_);
    lv_label_set_text(label_, state_.pageName.get());
}

void TopBar::setupBindings() {
    using oc::state::bind;
    bind(subs_).on(state_.pageName, [this](const char* name) {
        if (label_) {
            lv_label_set_text(label_, name);
        }
    });
}

}  // namespace ui

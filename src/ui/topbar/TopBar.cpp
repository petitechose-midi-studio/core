#include "TopBar.hpp"

#include <oc/state/Bind.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "ui/font/CoreFonts.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace style = oc::ui::lvgl::style;

TopBar::TopBar(lv_obj_t* parent, core::state::StatusBarState& state)
    : state_(state) {
    createLayout(parent);
    setupBindings();
    render();
}

TopBar::~TopBar() {
    if (container_) {
        lv_obj_delete(container_);
    }
}

void TopBar::createLayout(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, LV_PCT(100), theme::layout::TOP_BAR_HEIGHT);
    style::apply(container_).bgColor(theme::color::BACKGROUND);

    label_ = lv_label_create(container_);
    style::apply(label_).textFont(fonts.inter_14_medium).textColor(theme::color::TEXT_SECONDARY);
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

void TopBar::render() {
    if (label_) {
        lv_label_set_text(label_, state_.pageName.get());
    }
}

void TopBar::show() {
    if (container_) { lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN); }
}

void TopBar::hide() {
    if (container_) { lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN); }
}

bool TopBar::isVisible() const {
    return container_ && !lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace core::ui

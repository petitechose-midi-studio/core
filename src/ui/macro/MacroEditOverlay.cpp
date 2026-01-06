#include "MacroEditOverlay.hpp"

#include <cstdio>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace Theme = oc::ui::lvgl::BaseTheme;
namespace style = oc::ui::lvgl::style;

MacroEditOverlay::MacroEditOverlay(lv_obj_t* parent) {
    createLayout(parent);
}

MacroEditOverlay::~MacroEditOverlay() {
    if (overlay_) {
        lv_obj_delete(overlay_);
    }
}

void MacroEditOverlay::createLayout(lv_obj_t* parent) {
    // Fullscreen overlay background (semi-transparent)
    overlay_ = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay_);
    style::apply(overlay_).fullSize().bgColor(0x000000, LV_OPA_70);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    // Center container (dialog box)
    container_ = lv_obj_create(overlay_);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, 160, 80);
    lv_obj_center(container_);
    style::apply(container_)
        .bgColor(Theme::Color::BACKGROUND)
        .radius(6)
        .border(1, Theme::Color::INACTIVE)
        .pad(8);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container_, 4, 0);

    // Title
    titleLabel_ = std::make_unique<oc::ui::lvgl::Label>(container_);
    titleLabel_->setText("Edit Macro 1");
    style::apply(titleLabel_->getElement()).textColor(Theme::Color::TEXT_PRIMARY);

    // Channel row
    chRow_ = lv_obj_create(container_);
    lv_obj_remove_style_all(chRow_);
    lv_obj_set_size(chRow_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(chRow_).flexRow(LV_FLEX_ALIGN_START, 8);

    channelPrefixLabel_ = std::make_unique<oc::ui::lvgl::Label>(chRow_);
    channelPrefixLabel_->setText("CH:");
    style::apply(channelPrefixLabel_->getElement()).textColor(Theme::Color::TEXT_SECONDARY);

    channelValueLabel_ = std::make_unique<oc::ui::lvgl::Label>(chRow_);
    channelValueLabel_->setText("1");
    style::apply(channelValueLabel_->getElement()).textColor(Theme::Color::TEXT_PRIMARY);

    // CC row
    ccRow_ = lv_obj_create(container_);
    lv_obj_remove_style_all(ccRow_);
    lv_obj_set_size(ccRow_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(ccRow_).flexRow(LV_FLEX_ALIGN_START, 8);

    ccPrefixLabel_ = std::make_unique<oc::ui::lvgl::Label>(ccRow_);
    ccPrefixLabel_->setText("CC:");
    style::apply(ccPrefixLabel_->getElement()).textColor(Theme::Color::TEXT_SECONDARY);

    ccValueLabel_ = std::make_unique<oc::ui::lvgl::Label>(ccRow_);
    ccValueLabel_->setText("0");
    style::apply(ccValueLabel_->getElement()).textColor(Theme::Color::TEXT_PRIMARY);

    // Initial focus indicator
    updateFocusIndicator(0);
}

void MacroEditOverlay::render(const MacroEditOverlayProps& props) {
    // Early exit if no change
    if (props == currentProps_) {
        return;
    }

    // Handle visibility changes
    if (props.visible && !currentProps_.visible) {
        lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    } else if (!props.visible && currentProps_.visible) {
        lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    }

    // Skip content updates if not visible
    if (!props.visible) {
        currentProps_ = props;
        return;
    }

    // Update title when editing index changes
    if (props.editingIndex != currentProps_.editingIndex) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Edit Macro %d", props.editingIndex + 1);
        titleLabel_->setText(buf);
    }

    // Update channel value
    if (props.channel != currentProps_.channel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", props.channel);
        channelValueLabel_->setText(buf);
    }

    // Update CC value
    if (props.cc != currentProps_.cc) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", props.cc);
        ccValueLabel_->setText(buf);
    }

    // Update focus indicator
    if (props.focusedRow != currentProps_.focusedRow) {
        updateFocusIndicator(props.focusedRow);
    }

    currentProps_ = props;
}

void MacroEditOverlay::updateFocusIndicator(uint8_t focusedRow) {
    // Highlight focused row with accent color
    lv_color_t focusColor = lv_color_hex(Theme::Color::ACTIVE);
    lv_color_t normalColor = lv_color_hex(Theme::Color::TEXT_PRIMARY);

    if (channelValueLabel_ && ccValueLabel_) {
        lv_obj_set_style_text_color(channelValueLabel_->getElement(),
            focusedRow == 0 ? focusColor : normalColor, 0);
        lv_obj_set_style_text_color(ccValueLabel_->getElement(),
            focusedRow == 1 ? focusColor : normalColor, 0);
    }
}

}  // namespace core::ui

#include "MacroEditOverlay.hpp"

#include <cstdio>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = oc::ui::lvgl::base_theme;
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
        .bgColor(theme::color::BACKGROUND)
        .radius(6)
        .border(1, theme::color::INACTIVE)
        .pad(8);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container_, 4, 0);

    // Title
    title_label_ = std::make_unique<oc::ui::lvgl::Label>(container_);
    title_label_->setText("Edit Macro 1");
    style::apply(title_label_->getElement()).textColor(theme::color::TEXT_PRIMARY);

    // Channel row
    ch_row_ = lv_obj_create(container_);
    lv_obj_remove_style_all(ch_row_);
    lv_obj_set_size(ch_row_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(ch_row_).flexRow(LV_FLEX_ALIGN_START, 8);

    channel_prefix_label_ = std::make_unique<oc::ui::lvgl::Label>(ch_row_);
    channel_prefix_label_->setText("CH:");
    style::apply(channel_prefix_label_->getElement()).textColor(theme::color::TEXT_SECONDARY);

    channel_value_label_ = std::make_unique<oc::ui::lvgl::Label>(ch_row_);
    channel_value_label_->setText("1");
    style::apply(channel_value_label_->getElement()).textColor(theme::color::TEXT_PRIMARY);

    // CC row
    cc_row_ = lv_obj_create(container_);
    lv_obj_remove_style_all(cc_row_);
    lv_obj_set_size(cc_row_, LV_PCT(100), LV_SIZE_CONTENT);
    style::apply(cc_row_).flexRow(LV_FLEX_ALIGN_START, 8);

    cc_prefix_label_ = std::make_unique<oc::ui::lvgl::Label>(cc_row_);
    cc_prefix_label_->setText("CC:");
    style::apply(cc_prefix_label_->getElement()).textColor(theme::color::TEXT_SECONDARY);

    cc_value_label_ = std::make_unique<oc::ui::lvgl::Label>(cc_row_);
    cc_value_label_->setText("0");
    style::apply(cc_value_label_->getElement()).textColor(theme::color::TEXT_PRIMARY);

    // Initial focus indicator
    updateFocusIndicator(0);
}

void MacroEditOverlay::render(const MacroEditOverlayProps& props) {
    // Early exit if no change
    if (props == current_props_) {
        return;
    }

    // Handle visibility changes
    if (props.visible && !current_props_.visible) {
        lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    } else if (!props.visible && current_props_.visible) {
        lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    }

    // Skip content updates if not visible
    if (!props.visible) {
        current_props_ = props;
        return;
    }

    // Update title when editing index changes
    if (props.editingIndex != current_props_.editingIndex) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Edit Macro %d", props.editingIndex + 1);
        title_label_->setText(buf);
    }

    // Update channel value
    if (props.channel != current_props_.channel) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", static_cast<int>(props.channel) + 1);
        channel_value_label_->setText(buf);
    }

    // Update CC value
    if (props.cc != current_props_.cc) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", props.cc);
        cc_value_label_->setText(buf);
    }

    // Update focus indicator
    if (props.focusedRow != current_props_.focusedRow) {
        updateFocusIndicator(props.focusedRow);
    }

    current_props_ = props;
}

void MacroEditOverlay::updateFocusIndicator(uint8_t focusedRow) {
    // Highlight focused row with accent color
    lv_color_t focusColor = lv_color_hex(theme::color::ACTIVE);
    lv_color_t normalColor = lv_color_hex(theme::color::TEXT_PRIMARY);

    if (channel_value_label_ && cc_value_label_) {
        lv_obj_set_style_text_color(channel_value_label_->getElement(),
            focusedRow == 0 ? focusColor : normalColor, 0);
        lv_obj_set_style_text_color(cc_value_label_->getElement(),
            focusedRow == 1 ? focusColor : normalColor, 0);
    }
}

}  // namespace core::ui

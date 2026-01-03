#include "MacroEditOverlay.hpp"

#include <cstdio>

#include "ui/theme/StandaloneTheme.hpp"

namespace ui {

namespace Theme = oc::ui::lvgl::BaseTheme;

MacroEditOverlay::MacroEditOverlay(lv_obj_t* parent, state::MacroEditState& state)
    : state_(state) {
    createLayout(parent);
    bindToState();
}

MacroEditOverlay::~MacroEditOverlay() {
    subs_.clear();
    if (overlay_) {
        lv_obj_delete(overlay_);
    }
}

void MacroEditOverlay::createLayout(lv_obj_t* parent) {
    // Fullscreen overlay background (semi-transparent)
    overlay_ = lv_obj_create(parent);
    lv_obj_remove_style_all(overlay_);
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_70, 0);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    // Center container (dialog box)
    container_ = lv_obj_create(overlay_);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, 160, 80);
    lv_obj_center(container_);
    lv_obj_set_style_bg_color(container_, lv_color_hex(Theme::Color::BACKGROUND), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(container_, 6, 0);
    lv_obj_set_style_border_width(container_, 1, 0);
    lv_obj_set_style_border_color(container_, lv_color_hex(Theme::Color::INACTIVE), 0);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 8, 0);
    lv_obj_set_style_pad_row(container_, 4, 0);

    // Title
    titleLabel_ = std::make_unique<oc::ui::lvgl::Label>(container_);
    titleLabel_->setText("Edit Macro 1");
    lv_obj_set_style_text_color(titleLabel_->getElement(), lv_color_hex(Theme::Color::TEXT_PRIMARY), 0);

    // Channel row
    chRow_ = lv_obj_create(container_);
    lv_obj_remove_style_all(chRow_);
    lv_obj_set_size(chRow_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chRow_, 8, 0);

    channelPrefixLabel_ = std::make_unique<oc::ui::lvgl::Label>(chRow_);
    channelPrefixLabel_->setText("CH:");
    lv_obj_set_style_text_color(channelPrefixLabel_->getElement(), lv_color_hex(Theme::Color::TEXT_SECONDARY), 0);

    channelValueLabel_ = std::make_unique<oc::ui::lvgl::Label>(chRow_);
    channelValueLabel_->setText("1");
    lv_obj_set_style_text_color(channelValueLabel_->getElement(), lv_color_hex(Theme::Color::TEXT_PRIMARY), 0);

    // CC row
    ccRow_ = lv_obj_create(container_);
    lv_obj_remove_style_all(ccRow_);
    lv_obj_set_size(ccRow_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ccRow_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ccRow_, 8, 0);

    ccPrefixLabel_ = std::make_unique<oc::ui::lvgl::Label>(ccRow_);
    ccPrefixLabel_->setText("CC:");
    lv_obj_set_style_text_color(ccPrefixLabel_->getElement(), lv_color_hex(Theme::Color::TEXT_SECONDARY), 0);

    ccValueLabel_ = std::make_unique<oc::ui::lvgl::Label>(ccRow_);
    ccValueLabel_->setText("0");
    lv_obj_set_style_text_color(ccValueLabel_->getElement(), lv_color_hex(Theme::Color::TEXT_PRIMARY), 0);

    // Initial focus indicator
    updateFocusIndicator();
}

void MacroEditOverlay::bindToState() {
    // Update title when editing index changes
    subs_.push_back(state_.editingIndex.subscribe([this](uint8_t idx) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Edit Macro %d", idx + 1);
        titleLabel_->setText(buf);
    }));

    // Update channel value display
    subs_.push_back(state_.tempChannel.subscribe([this](uint8_t ch) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", ch);
        channelValueLabel_->setText(buf);
    }));

    // Update CC value display
    subs_.push_back(state_.tempCC.subscribe([this](uint8_t cc) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", cc);
        ccValueLabel_->setText(buf);
    }));
}

void MacroEditOverlay::show() {
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    focusedRow_ = 0;  // Start on channel
    updateFocusIndicator();
}

void MacroEditOverlay::hide() {
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
}

bool MacroEditOverlay::isVisible() const {
    return overlay_ && !lv_obj_has_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
}

void MacroEditOverlay::setFocusedRow(uint8_t row) {
    focusedRow_ = row % 2;
    updateFocusIndicator();
}

void MacroEditOverlay::updateFocusIndicator() {
    // Highlight focused row with accent color
    lv_color_t focusColor = lv_color_hex(Theme::Color::ACTIVE);
    lv_color_t normalColor = lv_color_hex(Theme::Color::TEXT_PRIMARY);

    if (channelValueLabel_ && ccValueLabel_) {
        lv_obj_set_style_text_color(channelValueLabel_->getElement(),
            focusedRow_ == 0 ? focusColor : normalColor, 0);
        lv_obj_set_style_text_color(ccValueLabel_->getElement(),
            focusedRow_ == 1 ? focusColor : normalColor, 0);
    }
}

}  // namespace ui

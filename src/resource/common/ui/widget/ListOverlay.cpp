#include "ListOverlay.hpp"

#include <cstring>

#include "resource/common/ui/font/binary_font_buffer.hpp"
#include "resource/common/ui/theme/BaseTheme.hpp"

using namespace BaseTheme;

ListOverlay::ListOverlay(lv_obj_t* parent) : parent_(parent) {
    // Create UI immediately (hidden by default) to support scoped bindings
    createOverlay();
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    ui_created_ = true;
    visible_ = false;
}

ListOverlay::~ListOverlay() {
    cleanup();
}

void ListOverlay::setTitle(const std::string& title) {
    title_ = title;

    if (ui_created_ && title_label_) {
        if (title_.empty()) {
            lv_obj_add_flag(title_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(title_label_, title_.c_str());
            lv_obj_clear_flag(title_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ListOverlay::setItems(const std::vector<std::string>& items) {
    items_ = items;

    if (selected_index_ >= static_cast<int>(items_.size())) {
        selected_index_ = items_.empty() ? 0 : items_.size() - 1;
    }
    if (selected_index_ < 0) {
        selected_index_ = 0;
    }

    if (ui_created_ && list_) {
        destroyList();
        createList();
        populateList();
        updateHighlight();
        scrollToSelected();  // Restore scroll position after list recreation
    }
}

void ListOverlay::setSelectedIndex(int index) {
    if (items_.empty()) {
        selected_index_ = 0;
        return;
    }

    int size = static_cast<int>(items_.size());
    index = ((index % size) + size) % size;  // Handle negative wrapping too

    if (selected_index_ != index) {
        selected_index_ = index;

        if (ui_created_ && visible_) {
            updateHighlight();
            scrollToSelected();
        }
    }
}

void ListOverlay::show() {
    // UI is now pre-created in constructor, just show it
    if (overlay_) {
        lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
        visible_ = true;

        updateHighlight();
        scrollToSelected();
    }
}

void ListOverlay::hide() {
    if (overlay_) {
        lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
        visible_ = false;
    }
}

bool ListOverlay::isVisible() const {
    return visible_ && ui_created_;
}

int ListOverlay::getSelectedIndex() const {
    return items_.empty() ? -1 : selected_index_;
}

int ListOverlay::getItemCount() const {
    return items_.size();
}

lv_obj_t* ListOverlay::getButton(size_t index) const {
    return (index < buttons_.size()) ? buttons_[index] : nullptr;
}

void ListOverlay::createOverlay() {
    overlay_ = lv_obj_create(parent_);
    lv_obj_add_flag(overlay_,
                    LV_OBJ_FLAG_FLOATING);  // Remove from parent layout (no scrollbar trigger)
    lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(overlay_, LV_ALIGN_CENTER, 0, 0);  // Center in parent
    lv_obj_set_style_bg_color(overlay_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_90, 0);  // 90% opaque for better text contrast
    lv_obj_set_style_border_width(overlay_, 0, 0);
    lv_obj_set_style_pad_all(overlay_, 0, 0);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);  // No scrollbar on overlay

    container_ = lv_obj_create(overlay_);
    lv_obj_set_size(container_, LV_PCT(80), LV_PCT(70));
    lv_obj_align(container_, LV_ALIGN_CENTER, 0, 0);  // Explicit center alignment
    lv_obj_set_style_bg_color(container_, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(container_, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(container_, 2, 0);
    lv_obj_set_style_radius(container_, 8, 0);
    lv_obj_set_style_pad_all(container_, 12, 0);  // Inner padding for border spacing
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);  // No scrollbar on container

    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container_, 8, 0);  // Gap between title and list

    createTitleLabel();
    createList();
    populateList();
}

void ListOverlay::createTitleLabel() {
    title_label_ = lv_label_create(container_);
    lv_obj_set_width(title_label_, LV_PCT(100));
    lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_white(), 0);

    if (fonts.tempo_label) {
        lv_obj_set_style_text_font(title_label_, fonts.tempo_label, 0);
    }

    if (title_.empty()) {
        lv_obj_add_flag(title_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(title_label_, title_.c_str());
    }
}

void ListOverlay::createList() {
    list_ = lv_list_create(container_);
    lv_obj_set_size(list_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);  // Take remaining space
    lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(list_, 0, 0);
    lv_obj_set_style_pad_all(list_, 5, 0);
    lv_obj_set_style_pad_row(list_, 6, 0);  // Gap between items

    lv_obj_add_event_cb(
        list_,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_SCROLL_BEGIN) {
                lv_anim_t* anim = lv_event_get_scroll_anim(e);
                if (anim) {
                    anim->duration = 100;  // 100ms for responsive scroll
                }
            }
        },
        LV_EVENT_SCROLL_BEGIN,
        nullptr);
}

void ListOverlay::populateList() {
    if (!list_) return;

    buttons_.clear();
    bullets_.clear();

    for (const auto& item : items_) {
        lv_obj_t* btn = lv_obj_create(list_);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, LV_SIZE_CONTENT);

        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_STATE_CHECKED);

        lv_obj_set_style_pad_left(btn, 8, 0);
        lv_obj_set_style_pad_right(btn, 16, 0);
        lv_obj_set_style_pad_top(btn, 6, 0);
        lv_obj_set_style_pad_bottom(btn, 6, 0);
        lv_obj_set_style_pad_column(btn, 8, 0);  // Gap between bullet and label

        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(btn, 0, 0);

        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, item.c_str());
        lv_obj_set_style_text_color(label, lv_color_hex(Color::INACTIVE_LIGHTER), 0);

        if (fonts.list_item_label) {
            lv_obj_set_style_text_font(label, fonts.list_item_label, 0);
        }

        buttons_.push_back(btn);
        bullets_.push_back(nullptr);  // No bullet anymore, but keep array size consistent
    }

    updateHighlight();
}

void ListOverlay::updateHighlight() {
    if (buttons_.empty() || selected_index_ < 0 ||
        selected_index_ >= static_cast<int>(buttons_.size())) {
        return;
    }

    for (size_t i = 0; i < buttons_.size(); i++) {
        lv_obj_clear_state(buttons_[i], LV_STATE_CHECKED);

        lv_obj_t* label = lv_obj_get_child(buttons_[i], 0);  // Label is now first child (no bullet)
        if (label) {
            lv_obj_set_style_text_color(label, lv_color_hex(Color::INACTIVE_LIGHTER), 0);
        }
    }

    lv_obj_add_state(buttons_[selected_index_], LV_STATE_CHECKED);

    lv_obj_t* selected_label = lv_obj_get_child(buttons_[selected_index_], 0);  // Label is now first child (no bullet)
    if (selected_label) {
        lv_obj_set_style_text_color(selected_label, lv_color_hex(Color::TEXT_PRIMARY), 0);
    }
}

void ListOverlay::scrollToSelected() {
    if (buttons_.empty() || selected_index_ < 0 ||
        selected_index_ >= static_cast<int>(buttons_.size()) || !list_) {
        return;
    }

    lv_obj_scroll_to_view(buttons_[selected_index_], LV_ANIM_ON);
}

void ListOverlay::destroyList() {
    if (list_) {
        lv_obj_del(list_);
        list_ = nullptr;
    }
    buttons_.clear();
    bullets_.clear();
}

void ListOverlay::cleanup() {
    if (overlay_) {
        lv_obj_del(overlay_);
        overlay_ = nullptr;
        container_ = nullptr;
        title_label_ = nullptr;
        list_ = nullptr;
    }
    buttons_.clear();
    bullets_.clear();
    ui_created_ = false;
    visible_ = false;
}

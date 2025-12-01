#include "ListOverlay.hpp"

#include <cstring>

#include "font/FontLoader.hpp"
#include "theme/BaseTheme.hpp"

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
    // Skip if items are the same (avoid unnecessary list recreation)
    if (items_ == items) {
        return;
    }

    items_ = items;

    if (selected_index_ >= static_cast<int>(items_.size())) {
        selected_index_ = items_.empty() ? 0 : items_.size() - 1;
    }
    if (selected_index_ < 0) {
        selected_index_ = 0;
    }

    if (ui_created_ && list_) {
        destroyList();       // Clear children only
        populateList();      // Repopulate with new items
        updateHighlight();
        scrollToSelected();
    }
}

void ListOverlay::setSelectedIndex(int index) {
    if (items_.empty()) {
        selected_index_ = 0;
        return;
    }

    int size = static_cast<int>(items_.size());
    int prev_index = selected_index_;
    index = ((index % size) + size) % size;  // Handle negative wrapping too

    if (selected_index_ != index) {
        selected_index_ = index;

        if (ui_created_ && visible_) {
            updateHighlight();

            // Detect wrap-around: if we jumped more than 1 item, it's a wrap
            int delta = index - prev_index;
            bool isWrap = (delta > 1) || (delta < -1);
            scrollToSelected(!isWrap);  // No animation for wrap
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

void ListOverlay::setItemFont(size_t index, lv_font_t* font) {
    lv_obj_t* btn = getButton(index);
    if (!btn || !font) return;

    // Find the label child (first label in the button)
    uint32_t childCount = lv_obj_get_child_cnt(btn);
    for (uint32_t i = 0; i < childCount; i++) {
        lv_obj_t* child = lv_obj_get_child(btn, i);
        if (child && lv_obj_check_type(child, &lv_label_class)) {
            lv_obj_set_style_text_font(child, font, 0);
            break;
        }
    }
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
    lv_obj_set_size(container_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(container_, LV_ALIGN_CENTER, 0, 0);  // Explicit center alignment
    lv_obj_set_style_bg_color(container_, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(container_, 0, 0);  // No padding - children manage their own spacing
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);  // No scrollbar on container

    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container_,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container_, 4, 0);  // Gap between elements (no padding)

    createTitleLabel();
    createList();
    populateList();
}

void ListOverlay::createTitleLabel() {
    title_label_ = lv_label_create(container_);
    lv_obj_set_width(title_label_, lv_pct(100) - 16);  // Account for 8px margin on each side
    lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_white(), 0);
    // Spacing managed by child: 8px margin on sides, 8px top (gap handles bottom)
    lv_obj_set_style_margin_left(title_label_, 8, 0);
    lv_obj_set_style_margin_right(title_label_, 8, 0);
    lv_obj_set_style_margin_top(title_label_, 8, 0);

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
    lv_obj_set_style_pad_all(list_, 4, 0);
    lv_obj_set_style_pad_row(list_, 2, 0);  // Gap between items
    // Spacing managed by child: 8px margin on sides
    lv_obj_set_style_margin_left(list_, 8, 0);
    lv_obj_set_style_margin_right(list_, 8, 0);

    // Discrete scrollbar styling
    lv_obj_set_style_width(list_, 3, LV_PART_SCROLLBAR);  // Thin scrollbar
    lv_obj_set_style_bg_color(list_, lv_color_hex(Color::INACTIVE_LIGHTER), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list_, LV_OPA_30, LV_PART_SCROLLBAR);  // Very transparent

    lv_obj_add_event_cb(
        list_,
        [](lv_event_t* e) {
            if (lv_event_get_code(e) == LV_EVENT_SCROLL_BEGIN) {
                lv_anim_t* anim = lv_event_get_scroll_anim(e);
                if (anim) {
                    anim->duration = 50;  // 100ms for responsive scroll
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

        // Configure text styles for different states
        lv_obj_set_style_text_color(label, lv_color_hex(Color::INACTIVE_LIGHTER), LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_STATE_DEFAULT);

        lv_obj_set_style_text_color(label, lv_color_hex(Color::TEXT_PRIMARY), LV_STATE_FOCUSED);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_STATE_FOCUSED);

        lv_obj_set_style_text_color(label, lv_color_white(), LV_STATE_PRESSED);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_STATE_PRESSED);

        lv_obj_set_style_text_color(label, lv_color_hex(Color::INACTIVE_LIGHTER), LV_STATE_DISABLED);
        lv_obj_set_style_text_opa(label, LV_OPA_50, LV_STATE_DISABLED);

        if (fonts.list_item_label) {
            lv_obj_set_style_text_font(label, fonts.list_item_label, 0);
        }

        buttons_.push_back(btn);
        bullets_.push_back(nullptr);  // No bullet anymore, but keep array size consistent
    }

    updateHighlight();
}

// Helper: recursively apply/clear state on an object and all descendants
static void applyStateRecursive(lv_obj_t* obj, lv_state_t state, bool apply) {
    if (!obj) return;

    if (apply) {
        lv_obj_add_state(obj, state);
    } else {
        lv_obj_clear_state(obj, state);
    }

    // Apply to all children recursively
    uint32_t child_count = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(obj, i);
        applyStateRecursive(child, state, apply);
    }
}

void ListOverlay::updateHighlight() {
    if (buttons_.empty() || selected_index_ < 0 ||
        selected_index_ >= static_cast<int>(buttons_.size())) {
        return;
    }

    // Clear focused state on all buttons and their descendants
    for (size_t i = 0; i < buttons_.size(); i++) {
        applyStateRecursive(buttons_[i], LV_STATE_FOCUSED, false);
    }

    // Set focused state on selected button and all its descendants
    applyStateRecursive(buttons_[selected_index_], LV_STATE_FOCUSED, true);
}

void ListOverlay::scrollToSelected(bool animate) {
    if (buttons_.empty() || selected_index_ < 0 ||
        selected_index_ >= static_cast<int>(buttons_.size()) || !list_) {
        return;
    }

    lv_obj_scroll_to_view(buttons_[selected_index_], animate ? LV_ANIM_ON : LV_ANIM_OFF);
}

void ListOverlay::destroyList() {
    // Only clear children, don't destroy list_ to preserve flex order
    if (list_) {
        lv_obj_clean(list_);  // Remove all children but keep list_
    }
    buttons_.clear();
    bullets_.clear();
}

void ListOverlay::cleanup() {
    if (overlay_) {
        lv_obj_delete(overlay_);
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

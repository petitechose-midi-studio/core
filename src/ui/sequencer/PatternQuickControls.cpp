#include "PatternQuickControls.hpp"

#include <algorithm>
#include <array>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/type/TextFormat.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace core::ui {

namespace {

using Item = core::state::sequencer::PatternQuickControlItem;

constexpr lv_coord_t STRIP_HEIGHT = 24;
constexpr lv_coord_t ITEM_HEIGHT = 18;
constexpr lv_coord_t VALUE_PAD_X = 4;
constexpr lv_coord_t CURSOR_HEIGHT = 2;
constexpr lv_coord_t CURSOR_WIDTH = 18;
constexpr lv_coord_t CURSOR_OFFSET_Y = 2;
constexpr lv_coord_t HORIZONTAL_INSET = theme::layout::MARGIN_SM + 4;

constexpr uint32_t TEXT_COLOR = theme::color::TEXT_PRIMARY;
constexpr lv_opa_t LABEL_OPA = LV_OPA_50;
constexpr lv_opa_t VALUE_OPA = LV_OPA_70;
constexpr lv_opa_t HIGHLIGHT_OPA = LV_OPA_COVER;

constexpr std::array<Item, 3> ITEMS = {
    Item::CHANNEL,
    Item::DIVISION,
    Item::LENGTH,
};

constexpr std::array<lv_flex_align_t, 3> ITEM_MAIN_ALIGN = {
    LV_FLEX_ALIGN_START,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_END,
};

size_t itemIndex(Item item) {
    return std::min(static_cast<size_t>(item), ITEMS.size() - 1);
}

void formatValue(char* buffer, size_t size, const PatternQuickControlsProps& props, Item item) {
    if (!buffer || size == 0) return;

    switch (item) {
        case Item::CHANNEL:
            oc::type::text::copy(buffer, size, "1");
            return;
        case Item::DIVISION:
            oc::type::text::formatFraction(
                buffer,
                size,
                1U,
                static_cast<unsigned>(4U * static_cast<uint16_t>(props.stepsPerBeat))
            );
            return;
        case Item::LENGTH:
        default:
            oc::type::text::formatUnsigned(buffer, size, static_cast<unsigned>(props.length));
            return;
    }
}

}  // namespace

PatternQuickControls::PatternQuickControls(lv_obj_t* parent) {
    createUI(parent);
}

PatternQuickControls::~PatternQuickControls() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        selection_cursor_ = nullptr;
    }
}

FLASHMEM void PatternQuickControls::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_)
        .size(LV_PCT(100), STRIP_HEIGHT)
        .transparent()
        .noBorder()
        .noScroll();
    lv_obj_set_style_pad_top(container_, 0, 0);
    lv_obj_set_style_pad_bottom(container_, theme::layout::MARGIN_XS, 0);
    lv_obj_set_style_pad_left(container_, HORIZONTAL_INSET, 0);
    lv_obj_set_style_pad_right(container_, HORIZONTAL_INSET, 0);
    lv_obj_set_style_pad_column(container_, theme::layout::MARGIN_XS, 0);
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    selection_cursor_ = lv_obj_create(container_);
    lv_obj_remove_style_all(selection_cursor_);
    lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(selection_cursor_, CURSOR_WIDTH, CURSOR_HEIGHT);
    lv_obj_set_style_radius(selection_cursor_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(selection_cursor_, lv_color_hex(TEXT_COLOR), 0);
    lv_obj_set_style_bg_opa(selection_cursor_, VALUE_OPA, 0);
    lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);

    for (size_t i = 0; i < ITEMS.size(); ++i) {
        lv_obj_t* item = lv_obj_create(container_);
        items_[i] = item;
        lv_obj_remove_style_all(item);
        lv_obj_set_width(item, 0);
        lv_obj_set_height(item, ITEM_HEIGHT);
        lv_obj_set_flex_grow(item, 1);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_layout(item, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, ITEM_MAIN_ALIGN[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* content = lv_obj_create(item);
        contents_[i] = content;
        lv_obj_remove_style_all(content);
        lv_obj_set_width(content, LV_SIZE_CONTENT);
        lv_obj_set_height(content, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(content, 0, 0);
        lv_obj_set_layout(content, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(content, VALUE_PAD_X, 0);

        lv_obj_t* label = lv_label_create(content);
        labels_[i] = label;
        if (ITEMS[i] == Item::CHANNEL) {
            lv_label_set_text(label, "Track");
        } else if (ITEMS[i] == Item::DIVISION) {
            lv_label_set_text(label, "Division");
        } else {
            lv_label_set_text(label, "Length");
        }
        lv_obj_set_style_text_font(label, fonts.inter_13_bold, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(TEXT_COLOR), 0);
        lv_obj_set_style_text_opa(label, LABEL_OPA, 0);

        lv_obj_t* value = lv_label_create(content);
        values_[i] = value;
        lv_obj_set_style_text_font(value, fonts.inter_13_medium, 0);
        lv_obj_set_style_text_color(value, lv_color_hex(TEXT_COLOR), 0);
        lv_obj_set_style_text_opa(value, VALUE_OPA, 0);
    }
}

void PatternQuickControls::render(const PatternQuickControlsProps& props) {
    if (!container_) return;

    const size_t focusedIndex = itemIndex(props.focusedItem);

    for (size_t i = 0; i < ITEMS.size(); ++i) {
        char buffer[12];
        formatValue(buffer, sizeof(buffer), props, ITEMS[i]);
        lv_label_set_text(values_[i], buffer);

        const bool highlighted = props.selecting && i == focusedIndex;
        lv_obj_set_style_text_opa(labels_[i], highlighted ? VALUE_OPA : LABEL_OPA, 0);
        lv_obj_set_style_text_opa(values_[i], highlighted ? HIGHLIGHT_OPA : VALUE_OPA, 0);
    }

    lv_obj_update_layout(container_);

    if (!props.selecting || !selection_cursor_ || !items_[focusedIndex] || !contents_[focusedIndex]) {
        lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(items_[focusedIndex]);
    const lv_coord_t itemX = lv_obj_get_x(items_[focusedIndex]);
    const lv_coord_t contentX = lv_obj_get_x(contents_[focusedIndex]);
    const lv_coord_t contentWidth = lv_obj_get_width(contents_[focusedIndex]);
    lv_obj_set_pos(
        selection_cursor_,
        static_cast<lv_coord_t>(itemX + contentX + (contentWidth - CURSOR_WIDTH) / 2),
        static_cast<lv_coord_t>(
            std::max<lv_coord_t>(0, lv_obj_get_y(items_[focusedIndex]) - CURSOR_HEIGHT - CURSOR_OFFSET_Y)
        )
    );
}

}  // namespace core::ui

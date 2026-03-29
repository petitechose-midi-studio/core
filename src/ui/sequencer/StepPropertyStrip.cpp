#include "StepPropertyStrip.hpp"

#include <algorithm>
#include <array>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>

#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace style = oc::ui::lvgl::style;
namespace theme = standalone::theme;

namespace core::ui {

namespace {

constexpr lv_coord_t STRIP_WIDTH = 20;
constexpr lv_coord_t ITEM_WIDTH = 20;
constexpr lv_coord_t SELECTION_CURSOR_WIDTH = 1;
constexpr lv_coord_t SELECTION_CURSOR_HEIGHT = 14;
constexpr lv_coord_t SELECTION_CURSOR_OFFSET_X = 1;

constexpr uint32_t ACTIVE_COLOR = theme::color::TEXT_PRIMARY;
constexpr lv_opa_t HIGHLIGHTED_OPA = LV_OPA_80;
constexpr lv_opa_t ACTIVE_OPA = LV_OPA_60;
constexpr lv_opa_t INACTIVE_OPA = LV_OPA_30;

constexpr std::array<core::state::sequencer::StepProperty, 5> STRIP_PROPERTIES = {
    core::state::sequencer::StepProperty::NOTE,
    core::state::sequencer::StepProperty::VELOCITY,
    core::state::sequencer::StepProperty::GATE,
    core::state::sequencer::StepProperty::NUDGE,
    core::state::sequencer::StepProperty::PROBABILITY,
};

size_t propertyStripIndex(core::state::sequencer::StepProperty property) {
    const size_t index = static_cast<size_t>(property);
    return std::min(index, STRIP_PROPERTIES.size() - 1);
}

}  // namespace

StepPropertyStrip::StepPropertyStrip(lv_obj_t* parent) {
    createUI(parent);
}

StepPropertyStrip::~StepPropertyStrip() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        selection_cursor_ = nullptr;
    }
}

FLASHMEM void StepPropertyStrip::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(container_, STRIP_WIDTH, LV_PCT(100));
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container_,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_left(container_, 0, 0);
    lv_obj_set_style_pad_right(container_, 2, 0);
    lv_obj_set_style_pad_top(container_, 6, 0);
    lv_obj_set_style_pad_bottom(container_, 6, 0);
    lv_obj_set_style_pad_row(container_, 2, 0);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    selection_cursor_ = lv_obj_create(container_);
    lv_obj_remove_style_all(selection_cursor_);
    lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(selection_cursor_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(selection_cursor_, SELECTION_CURSOR_WIDTH, SELECTION_CURSOR_HEIGHT);
    lv_obj_set_style_radius(selection_cursor_, 0, 0);
    lv_obj_set_style_border_width(selection_cursor_, 0, 0);
    lv_obj_set_style_bg_color(selection_cursor_, lv_color_hex(ACTIVE_COLOR), 0);
    lv_obj_set_style_bg_opa(selection_cursor_, LV_OPA_70, 0);
    lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);

    for (size_t i = 0; i < items_.size(); ++i) {
        lv_obj_t* item = lv_obj_create(container_);
        items_[i] = item;
        lv_obj_remove_style_all(item);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(item, ITEM_WIDTH);
        lv_obj_set_height(item, 18);
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(item, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

        lv_obj_t* icon = lv_label_create(item);
        icons_[i] = icon;
        standalone::icons::set(
            icon,
            sequencer::visual::propertyIconGlyph(STRIP_PROPERTIES[i]),
            standalone::icons::Size::M
        );
        lv_obj_center(icon);
        lv_obj_set_style_text_color(icon, lv_color_hex(ACTIVE_COLOR), 0);
        lv_obj_set_style_text_opa(icon, INACTIVE_OPA, 0);
    }
}

void StepPropertyStrip::render(const StepPropertyStripProps& props) {
    if (!container_) return;

    lv_obj_update_layout(container_);

    const int selectedIndex =
        std::clamp(props.selectedIndex, 0, static_cast<int>(STRIP_PROPERTIES.size()) - 1);
    const size_t activeIndex = propertyStripIndex(props.activeProperty);
    const size_t highlightedIndex =
        props.selecting ? static_cast<size_t>(selectedIndex) : activeIndex;

    for (size_t i = 0; i < icons_.size(); ++i) {
        lv_obj_t* icon = icons_[i];
        if (!icon) continue;

        const bool highlighted = i == highlightedIndex;
        const bool active = i == activeIndex;
        const bool visibleActive = props.selecting && active && !highlighted;

        lv_obj_set_style_text_opa(
            icon,
            highlighted ? HIGHLIGHTED_OPA : (visibleActive ? ACTIVE_OPA : INACTIVE_OPA),
            0
        );
    }

    if (!props.selecting) {
        lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_t* highlightedItem = items_[highlightedIndex];
    if (!highlightedItem) {
        lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(
        selection_cursor_,
        static_cast<lv_coord_t>(
            lv_obj_get_x(highlightedItem) + lv_obj_get_width(highlightedItem) - SELECTION_CURSOR_WIDTH + SELECTION_CURSOR_OFFSET_X
        ),
        static_cast<lv_coord_t>(
            lv_obj_get_y(highlightedItem) +
            (lv_obj_get_height(highlightedItem) - SELECTION_CURSOR_HEIGHT) / 2
        )
    );
}

}  // namespace core::ui

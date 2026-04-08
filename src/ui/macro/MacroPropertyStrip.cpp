#include "ui/macro/MacroPropertyStrip.hpp"

#include <algorithm>
#include <array>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>

#include "ui/font/StandaloneIcons.hpp"
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

constexpr std::array<const char*, 3> STRIP_ICONS = {
    standalone::icons::KNOB,
    standalone::icons::MIDI_CC,
    standalone::icons::MIDI_CHANNEL,
};

size_t propertyStripIndex(core::state::macro::MacroPerformanceProperty property) {
    return static_cast<size_t>(std::clamp(core::state::macro::performancePropertyIndex(property), 0, 2));
}

bool sameProps(const MacroPropertyStripProps& lhs, const MacroPropertyStripProps& rhs) {
    return lhs.activeProperty == rhs.activeProperty && lhs.clutchActive == rhs.clutchActive;
}

}  // namespace

MacroPropertyStrip::MacroPropertyStrip(lv_obj_t* parent) {
    createUI(parent);
}

MacroPropertyStrip::~MacroPropertyStrip() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        selection_cursor_ = nullptr;
    }
}

FLASHMEM void MacroPropertyStrip::createUI(lv_obj_t* parent) {
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
    lv_obj_set_style_pad_row(container_, 4, 0);
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
        standalone::icons::set(icon, STRIP_ICONS[i], standalone::icons::Size::M);
        lv_obj_center(icon);
        lv_obj_set_style_text_color(icon, lv_color_hex(ACTIVE_COLOR), 0);
        lv_obj_set_style_text_opa(icon, INACTIVE_OPA, 0);
    }
}

void MacroPropertyStrip::ensureCursorGeometry() {
    if (!container_) return;

    const lv_coord_t width = lv_obj_get_width(container_);
    const lv_coord_t height = lv_obj_get_height(container_);
    if (geometry_cache_initialized_ && geometry_cache_width_ == width &&
        geometry_cache_height_ == height) {
        return;
    }

    lv_obj_update_layout(container_);

    for (size_t i = 0; i < items_.size(); ++i) {
        lv_obj_t* item = items_[i];
        if (!item) continue;

        cursor_positions_[i].x = static_cast<lv_coord_t>(
            lv_obj_get_x(item) + lv_obj_get_width(item) - SELECTION_CURSOR_WIDTH +
            SELECTION_CURSOR_OFFSET_X
        );
        cursor_positions_[i].y = static_cast<lv_coord_t>(
            lv_obj_get_y(item) + (lv_obj_get_height(item) - SELECTION_CURSOR_HEIGHT) / 2
        );
    }

    geometry_cache_width_ = width;
    geometry_cache_height_ = height;
    geometry_cache_initialized_ = true;
}

void MacroPropertyStrip::render(const MacroPropertyStripProps& props) {
    if (!container_) return;
    if (has_rendered_ && sameProps(rendered_props_, props)) return;

    const size_t activeIndex = propertyStripIndex(props.activeProperty);

    for (size_t i = 0; i < icons_.size(); ++i) {
        lv_obj_t* icon = icons_[i];
        if (!icon) continue;

        const bool highlighted = i == activeIndex;
        const lv_opa_t nextOpa = highlighted
            ? (props.clutchActive ? HIGHLIGHTED_OPA : ACTIVE_OPA)
            : INACTIVE_OPA;

        if (!icon_render_cache_[i].initialized || icon_render_cache_[i].textOpa != nextOpa) {
            lv_obj_set_style_text_opa(icon, nextOpa, 0);
            icon_render_cache_[i].textOpa = nextOpa;
            icon_render_cache_[i].initialized = true;
        }
    }

    if (!props.clutchActive) {
        if (cursor_visible_cache_) {
            lv_obj_add_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
            cursor_visible_cache_ = false;
        }
        rendered_props_ = props;
        has_rendered_ = true;
        return;
    }

    ensureCursorGeometry();

    if (!cursor_visible_cache_) {
        lv_obj_clear_flag(selection_cursor_, LV_OBJ_FLAG_HIDDEN);
        cursor_visible_cache_ = true;
    }

    const lv_coord_t nextX = cursor_positions_[activeIndex].x;
    const lv_coord_t nextY = cursor_positions_[activeIndex].y;
    if (cursor_x_cache_ != nextX || cursor_y_cache_ != nextY) {
        lv_obj_set_pos(selection_cursor_, nextX, nextY);
        cursor_x_cache_ = nextX;
        cursor_y_cache_ = nextY;
    }

    rendered_props_ = props;
    has_rendered_ = true;
}

}  // namespace core::ui

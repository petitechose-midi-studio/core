#include "ui/macro/MacroBottomControls.hpp"

#include <cstring>

#include <oc/type/TextFormat.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace core::ui {

namespace {

using QuickItem = core::state::macro::MacroQuickControlItem;

constexpr lv_coord_t CONTAINER_HEIGHT = 24;
constexpr lv_coord_t TOP_ROW_HEIGHT = 20;
constexpr lv_coord_t SIDE_SLOT_WIDTH = 84;
constexpr lv_coord_t QUICK_CURSOR_WIDTH = 24;
constexpr lv_coord_t QUICK_CURSOR_HEIGHT = 2;
constexpr lv_coord_t QUICK_CURSOR_OFFSET_Y = 2;
constexpr lv_coord_t HORIZONTAL_INSET = theme::layout::MARGIN_SM + 4;
constexpr lv_coord_t CONTROL_PAD_TOP = 3;
constexpr lv_coord_t CONTROL_PAD_BOTTOM = 1;

constexpr uint32_t TEXT_COLOR = standalone::theme::color::TEXT_SECONDARY;
constexpr uint32_t TEXT_HIGHLIGHT_COLOR = standalone::theme::color::TEXT_PRIMARY;
constexpr lv_opa_t VALUE_OPA = LV_OPA_60;
constexpr lv_opa_t VALUE_HIGHLIGHT_OPA = LV_OPA_COVER;
constexpr lv_opa_t LABEL_OPA = LV_OPA_30;

lv_area_t relativeBounds(lv_obj_t* ancestor, lv_obj_t* descendant) {
    lv_area_t ancestorArea{};
    lv_area_t descendantArea{};
    lv_obj_get_coords(ancestor, &ancestorArea);
    lv_obj_get_coords(descendant, &descendantArea);

    return {
        static_cast<lv_coord_t>(descendantArea.x1 - ancestorArea.x1),
        static_cast<lv_coord_t>(descendantArea.y1 - ancestorArea.y1),
        static_cast<lv_coord_t>(descendantArea.x2 - ancestorArea.x1),
        static_cast<lv_coord_t>(descendantArea.y2 - ancestorArea.y1),
    };
}

void formatQuickValue(
    char* buffer,
    size_t size,
    const MacroBottomControlsProps& props,
    QuickItem item
) {
    if (!buffer || size == 0) return;

    switch (item) {
        case QuickItem::CC_OFFSET:
            oc::type::text::formatSigned(buffer, size, props.ccOffset, true);
            return;
        case QuickItem::GLOBAL_CHANNEL:
        default:
            oc::type::text::formatUnsigned(buffer, size, static_cast<unsigned>(props.globalChannel + 1U));
            return;
    }
}

void applyValueStyle(lv_obj_t* label, bool highlighted) {
    if (!label) return;
    lv_obj_set_style_text_color(
        label,
        lv_color_hex(highlighted ? TEXT_HIGHLIGHT_COLOR : TEXT_COLOR),
        0
    );
    lv_obj_set_style_text_opa(label, highlighted ? VALUE_HIGHLIGHT_OPA : VALUE_OPA, 0);
}

bool sameProps(const MacroBottomControlsProps& lhs, const MacroBottomControlsProps& rhs) {
    return lhs.selectingQuickControls == rhs.selectingQuickControls &&
           lhs.focusedQuickControl == rhs.focusedQuickControl &&
           lhs.globalChannel == rhs.globalChannel &&
           lhs.ccOffset == rhs.ccOffset;
}

size_t quickItemIndex(QuickItem item) {
    switch (item) {
        case QuickItem::CC_OFFSET:
            return 1;
        case QuickItem::GLOBAL_CHANNEL:
        default:
            return 0;
    }
}

}  // namespace

FLASHMEM MacroBottomControls::MacroBottomControls(lv_obj_t* parent) {
    createUI(parent);
}

FLASHMEM MacroBottomControls::~MacroBottomControls() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}

FLASHMEM void MacroBottomControls::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_)
        .size(LV_PCT(100), CONTAINER_HEIGHT)
        .transparent()
        .noBorder()
        .noScroll();
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_style_pad_top(container_, CONTROL_PAD_TOP, 0);
    lv_obj_set_style_pad_bottom(container_, CONTROL_PAD_BOTTOM, 0);
    lv_obj_set_style_pad_left(container_, HORIZONTAL_INSET, 0);
    lv_obj_set_style_pad_right(container_, HORIZONTAL_INSET, 0);
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    top_row_ = lv_obj_create(container_);
    style::apply(top_row_)
        .size(LV_PCT(100), TOP_ROW_HEIGHT)
        .transparent()
        .noBorder()
        .noScroll()
        .pad(0);
    lv_obj_set_style_bg_opa(top_row_, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(top_row_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_layout(top_row_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    createQuickControl(
        channel_,
        top_row_,
        QuickItem::GLOBAL_CHANNEL,
        "Channel",
        SIDE_SLOT_WIDTH,
        LV_FLEX_ALIGN_START,
        fonts.inter_14_medium
    );
    createQuickControl(
        offset_,
        top_row_,
        QuickItem::CC_OFFSET,
        "CC Offset",
        SIDE_SLOT_WIDTH,
        LV_FLEX_ALIGN_END,
        fonts.inter_13_medium
    );

    quick_cursor_ = lv_obj_create(container_);
    lv_obj_remove_style_all(quick_cursor_);
    lv_obj_add_flag(quick_cursor_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(quick_cursor_, QUICK_CURSOR_WIDTH, QUICK_CURSOR_HEIGHT);
    lv_obj_set_style_radius(quick_cursor_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(quick_cursor_, lv_color_hex(TEXT_HIGHLIGHT_COLOR), 0);
    lv_obj_set_style_bg_opa(quick_cursor_, VALUE_OPA, 0);
    lv_obj_add_flag(quick_cursor_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM void MacroBottomControls::createQuickControl(
    QuickControlWidgets& widgets,
    lv_obj_t* parent,
    QuickItem item,
    const char* labelText,
    lv_coord_t slotWidth,
    lv_flex_align_t align,
    const lv_font_t* valueFont
) {
    widgets.item = item;
    widgets.slot = lv_obj_create(parent);
    lv_obj_remove_style_all(widgets.slot);
    lv_obj_set_width(widgets.slot, slotWidth);
    lv_obj_set_height(widgets.slot, TOP_ROW_HEIGHT);
    lv_obj_set_style_pad_all(widgets.slot, 0, 0);
    lv_obj_set_style_bg_opa(widgets.slot, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(widgets.slot, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(widgets.slot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(widgets.slot, align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(widgets.slot, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    widgets.content = lv_obj_create(widgets.slot);
    lv_obj_remove_style_all(widgets.content);
    lv_obj_set_width(widgets.content, LV_SIZE_CONTENT);
    lv_obj_set_height(widgets.content, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(widgets.content, 0, 0);
    lv_obj_set_style_pad_column(widgets.content, 4, 0);
    lv_obj_set_style_bg_opa(widgets.content, LV_OPA_TRANSP, 0);
    lv_obj_set_layout(widgets.content, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(widgets.content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(widgets.content, align, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(widgets.content, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    widgets.label = lv_label_create(widgets.content);
    lv_label_set_text(widgets.label, labelText);
    lv_obj_set_style_text_font(widgets.label, fonts.inter_13_bold, 0);
    lv_obj_set_style_text_color(widgets.label, lv_color_hex(TEXT_COLOR), 0);
    lv_obj_set_style_text_opa(widgets.label, LABEL_OPA, 0);

    widgets.value = lv_label_create(widgets.content);
    lv_obj_set_style_text_font(widgets.value, valueFont, 0);
    lv_obj_set_style_text_color(widgets.value, lv_color_hex(TEXT_COLOR), 0);
    lv_obj_set_style_text_opa(widgets.value, VALUE_OPA, 0);
}

lv_obj_t* MacroBottomControls::quickControlAnchor(QuickItem item) const {
    switch (item) {
        case QuickItem::CC_OFFSET:
            return offset_.content;
        case QuickItem::GLOBAL_CHANNEL:
        default:
            return channel_.content;
    }
}

void MacroBottomControls::ensureCursorGeometry() {
    if (!container_) return;

    const lv_coord_t width = lv_obj_get_width(container_);
    const lv_coord_t height = lv_obj_get_height(container_);
    if (geometry_cache_initialized_ && geometry_cache_width_ == width &&
        geometry_cache_height_ == height) {
        return;
    }

    lv_obj_update_layout(container_);

    const auto cacheAnchorBounds = [this](QuickItem item) {
        lv_obj_t* anchor = quickControlAnchor(item);
        if (!anchor) return;

        const lv_area_t bounds = relativeBounds(container_, anchor);
        const lv_coord_t anchorWidth =
            static_cast<lv_coord_t>(bounds.x2 - bounds.x1 + 1);
        const lv_coord_t anchorHeight =
            static_cast<lv_coord_t>(bounds.y2 - bounds.y1 + 1);
        const lv_coord_t x = static_cast<lv_coord_t>(
            bounds.x1 + (anchorWidth - QUICK_CURSOR_WIDTH) / 2
        );
        const lv_coord_t y = static_cast<lv_coord_t>(
            bounds.y1 + anchorHeight - (QUICK_CURSOR_HEIGHT / 2) + QUICK_CURSOR_OFFSET_Y
        );

        cursor_positions_[quickItemIndex(item)] = {x, y};
    };

    cacheAnchorBounds(QuickItem::GLOBAL_CHANNEL);
    cacheAnchorBounds(QuickItem::CC_OFFSET);

    geometry_cache_width_ = width;
    geometry_cache_height_ = height;
    geometry_cache_initialized_ = true;
}

void MacroBottomControls::positionQuickControlCursor(const MacroBottomControlsProps& props) {
    if (!props.selectingQuickControls) {
        if (cursor_visible_cache_) {
            lv_obj_add_flag(quick_cursor_, LV_OBJ_FLAG_HIDDEN);
            cursor_visible_cache_ = false;
        }
        return;
    }

    lv_obj_t* anchor = quickControlAnchor(props.focusedQuickControl);
    if (!anchor) {
        if (cursor_visible_cache_) {
            lv_obj_add_flag(quick_cursor_, LV_OBJ_FLAG_HIDDEN);
            cursor_visible_cache_ = false;
        }
        return;
    }

    ensureCursorGeometry();

    if (!cursor_visible_cache_) {
        lv_obj_clear_flag(quick_cursor_, LV_OBJ_FLAG_HIDDEN);
        cursor_visible_cache_ = true;
    }

    const lv_point_t nextPos = cursor_positions_[quickItemIndex(props.focusedQuickControl)];
    if (cursor_x_cache_ != nextPos.x || cursor_y_cache_ != nextPos.y) {
        lv_obj_set_pos(quick_cursor_, nextPos.x, nextPos.y);
        cursor_x_cache_ = nextPos.x;
        cursor_y_cache_ = nextPos.y;
    }
}

void MacroBottomControls::renderQuickControl(
    QuickControlWidgets& widgets,
    const MacroBottomControlsProps& props
) {
    char buffer[16];
    formatQuickValue(buffer, sizeof(buffer), props, widgets.item);
    if (!widgets.valueInitialized ||
        std::strcmp(widgets.renderedValue.data(), buffer) != 0) {
        lv_label_set_text(widgets.value, buffer);
        std::strncpy(widgets.renderedValue.data(), buffer, widgets.renderedValue.size());
        widgets.renderedValue.back() = '\0';
        widgets.valueInitialized = true;
    }

    const bool highlighted =
        props.selectingQuickControls && props.focusedQuickControl == widgets.item;
    if (!widgets.highlightedInitialized || widgets.highlighted != highlighted) {
        lv_obj_set_style_text_opa(widgets.label, highlighted ? VALUE_OPA : LABEL_OPA, 0);
        applyValueStyle(widgets.value, highlighted);
        widgets.highlighted = highlighted;
        widgets.highlightedInitialized = true;
    }
}

void MacroBottomControls::render(const MacroBottomControlsProps& props) {
    if (!container_) return;
    if (has_rendered_ && sameProps(rendered_props_, props)) return;

    renderQuickControl(channel_, props);
    renderQuickControl(offset_, props);
    positionQuickControlCursor(props);

    rendered_props_ = props;
    has_rendered_ = true;
}

}  // namespace core::ui

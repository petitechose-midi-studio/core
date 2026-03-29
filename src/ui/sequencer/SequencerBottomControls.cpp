#include "SequencerBottomControls.hpp"

#include <oc/type/TextFormat.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "state/sequencer/SequencerQuickControls.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace core::ui {

namespace {

using QuickItem = core::state::sequencer::PatternQuickControlItem;

constexpr lv_coord_t CONTAINER_HEIGHT = 24;
constexpr lv_coord_t TOP_ROW_HEIGHT = 20;
constexpr lv_coord_t SIDE_SLOT_WIDTH = 84;
constexpr lv_coord_t QUICK_CURSOR_WIDTH = 18;
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
    const SequencerBottomControlsProps& props,
    QuickItem item
) {
    if (!buffer || size == 0) return;

    switch (item) {
        case QuickItem::OFFSET:
            oc::type::text::formatSigned(buffer, size, props.offsetSteps, true);
            return;
        case QuickItem::DIVISION:
            oc::type::text::formatFraction(
                buffer,
                size,
                1U,
                static_cast<unsigned>(4U * static_cast<uint16_t>(props.stepsPerBeat))
            );
            return;
        case QuickItem::LENGTH:
        default:
            oc::type::text::formatUnsigned(buffer, size, static_cast<unsigned>(props.length));
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

}  // namespace

SequencerBottomControls::SequencerBottomControls(lv_obj_t* parent) {
    createUI(parent);
}

SequencerBottomControls::~SequencerBottomControls() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}

FLASHMEM void SequencerBottomControls::createUI(lv_obj_t* parent) {
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
        length_,
        top_row_,
        QuickItem::LENGTH,
        core::state::sequencer::quickControlLabel(QuickItem::LENGTH),
        SIDE_SLOT_WIDTH,
        LV_FLEX_ALIGN_START,
        fonts.inter_14_medium
    );
    createQuickControl(
        offset_,
        top_row_,
        QuickItem::OFFSET,
        core::state::sequencer::quickControlLabel(QuickItem::OFFSET),
        LV_SIZE_CONTENT,
        LV_FLEX_ALIGN_CENTER,
        fonts.inter_13_medium
    );
    createQuickControl(
        division_,
        top_row_,
        QuickItem::DIVISION,
        core::state::sequencer::quickControlLabel(QuickItem::DIVISION),
        SIDE_SLOT_WIDTH,
        LV_FLEX_ALIGN_END,
        fonts.inter_14_medium
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

void SequencerBottomControls::createQuickControl(
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

lv_obj_t* SequencerBottomControls::quickControlAnchor(QuickItem item) const {
    switch (item) {
        case QuickItem::LENGTH:
            return length_.content;
        case QuickItem::DIVISION:
            return division_.content;
        case QuickItem::OFFSET:
        default:
            return offset_.content;
    }
}

void SequencerBottomControls::positionQuickControlCursor(const SequencerBottomControlsProps& props) {
    if (!props.selectingQuickControls) {
        lv_obj_add_flag(quick_cursor_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_t* anchor = quickControlAnchor(props.focusedQuickControl);

    if (!anchor) {
        lv_obj_add_flag(quick_cursor_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(quick_cursor_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(container_);
    const lv_area_t bounds = relativeBounds(container_, anchor);
    const lv_coord_t width = static_cast<lv_coord_t>(bounds.x2 - bounds.x1 + 1);
    const lv_coord_t height = static_cast<lv_coord_t>(bounds.y2 - bounds.y1 + 1);
    const lv_coord_t x = static_cast<lv_coord_t>(
        bounds.x1 + (width - QUICK_CURSOR_WIDTH) / 2
    );
    const lv_coord_t y = static_cast<lv_coord_t>(
        bounds.y1 + height - (QUICK_CURSOR_HEIGHT / 2) + QUICK_CURSOR_OFFSET_Y
    );
    lv_obj_set_pos(quick_cursor_, x, y);
}

void SequencerBottomControls::renderQuickControl(
    QuickControlWidgets& widgets,
    const SequencerBottomControlsProps& props
) {
    char buffer[16];
    formatQuickValue(buffer, sizeof(buffer), props, widgets.item);
    lv_label_set_text(widgets.value, buffer);

    const bool highlighted =
        props.selectingQuickControls && props.focusedQuickControl == widgets.item;
    lv_obj_set_style_text_opa(widgets.label, highlighted ? VALUE_OPA : LABEL_OPA, 0);
    applyValueStyle(widgets.value, highlighted);
}

void SequencerBottomControls::renderQuickControls(const SequencerBottomControlsProps& props) {
    renderQuickControl(length_, props);
    renderQuickControl(offset_, props);
    renderQuickControl(division_, props);
    positionQuickControlCursor(props);
}

void SequencerBottomControls::render(const SequencerBottomControlsProps& props) {
    if (!container_) return;
    renderQuickControls(props);
}

}  // namespace core::ui

#include "ui/sequencer/StepPropertySelectionOverlay.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "state/sequencer/StepPropertyDisplay.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace style = oc::ui::lvgl::style;
namespace theme = standalone::theme;

namespace core::ui {
namespace {

constexpr lv_coord_t PANEL_PAD_H = theme::layout::PAD_MD + theme::layout::PAD_SM;
constexpr lv_coord_t PANEL_PAD_TOP = theme::layout::PAD_MD;
constexpr lv_coord_t PANEL_PAD_BOTTOM = theme::layout::PAD_SM;
constexpr lv_coord_t CONTENT_GAP = theme::layout::GAP_MD;
constexpr lv_coord_t TEXT_GAP = 1;
constexpr uint32_t BG_COLOR = theme::color::BACKGROUND;
constexpr uint32_t BORDER_COLOR = 0x2E3A45;
constexpr uint32_t TEXT_PRIMARY = theme::color::TEXT_PRIMARY;
constexpr uint32_t TEXT_SECONDARY = theme::color::TEXT_SECONDARY;

const char* shortPropertyName(core::state::sequencer::StepProperty property) {
    return sequencer::semantic::labelForProperty(property);
}

uint32_t propertyColor(core::state::sequencer::StepProperty property) {
    return sequencer::semantic::colorForProperty(property);
}

template <size_t N>
bool textChanged(std::array<char, N>& cache, const char* text) {
    const char* next = text ? text : "";
    return std::strncmp(cache.data(), next, N) != 0;
}

template <size_t N>
void copyCachedText(std::array<char, N>& cache, const char* text) {
    const char* next = text ? text : "";
    std::strncpy(cache.data(), next, N - 1);
    cache[N - 1] = '\0';
}

}  // namespace

FLASHMEM StepPropertySelectionOverlay::StepPropertySelectionOverlay(lv_obj_t* parent) {
    createUI(parent);
}

FLASHMEM StepPropertySelectionOverlay::~StepPropertySelectionOverlay() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
    }
}

FLASHMEM void StepPropertySelectionOverlay::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    style::apply(container_).noScroll().noBorder();
    lv_obj_set_size(container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container_,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_left(container_, PANEL_PAD_H, 0);
    lv_obj_set_style_pad_right(container_, PANEL_PAD_H, 0);
    lv_obj_set_style_pad_top(container_, PANEL_PAD_TOP, 0);
    lv_obj_set_style_pad_bottom(container_, PANEL_PAD_BOTTOM, 0);
    lv_obj_set_style_bg_color(container_, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(container_, 1, 0);
    lv_obj_set_style_border_color(container_, lv_color_hex(BORDER_COLOR), 0);
    lv_obj_set_style_border_opa(container_, LV_OPA_70, 0);
    lv_obj_set_style_radius(container_, 4, 0);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    content_row_ = lv_obj_create(container_);
    lv_obj_remove_style_all(content_row_);
    lv_obj_set_size(content_row_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(content_row_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(content_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        content_row_,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(content_row_, CONTENT_GAP, 0);
    lv_obj_clear_flag(content_row_, LV_OBJ_FLAG_SCROLLABLE);

    icon_ = lv_label_create(content_row_);
    lv_obj_set_size(icon_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(icon_, standalone_fonts.icons_16, 0);

    text_column_ = lv_obj_create(content_row_);
    lv_obj_remove_style_all(text_column_);
    lv_obj_set_size(text_column_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(text_column_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(text_column_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        text_column_,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_row(text_column_, TEXT_GAP, 0);
    lv_obj_clear_flag(text_column_, LV_OBJ_FLAG_SCROLLABLE);

    label_ = lv_label_create(text_column_);
    lv_obj_set_size(label_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(label_, fonts.inter_14_semibold, 0);
    lv_obj_set_style_text_color(label_, lv_color_hex(TEXT_PRIMARY), 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_CLIP);

    value_ = lv_label_create(text_column_);
    lv_obj_set_size(value_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(value_, fonts.inter_12_medium, 0);
    lv_obj_set_style_text_color(value_, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_text_opa(value_, LV_OPA_80, 0);
    lv_label_set_long_mode(value_, LV_LABEL_LONG_CLIP);
    lv_obj_add_flag(value_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM void StepPropertySelectionOverlay::render(
    const StepPropertySelectionOverlayProps& props
) {
    if (!container_) return;

    if (!props.visible) {
        if (visible_cache_) {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
            visible_cache_ = false;
        }
        return;
    }

    bool needsLayout = false;
    if (!visible_cache_) {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(container_);
        visible_cache_ = true;
        needsLayout = true;
    }

    const char* icon = props.customContent
        ? props.icon
        : core::ui::sequencer::visual::propertyIconGlyph(props.property);
    const char* label = props.customContent ? props.label : shortPropertyName(props.property);
    const char* value = props.useValueText
        ? props.valueText.data()
        : (props.customContent ? props.value : nullptr);
    const uint32_t color = props.customContent
        ? (props.color == 0 ? TEXT_PRIMARY : props.color)
        : propertyColor(props.property);
    const bool valueVisible = value != nullptr && value[0] != '\0';
    const bool contentChanged =
        !has_rendered_ ||
        rendered_custom_content_ != props.customContent ||
        (!props.customContent && rendered_property_ != props.property) ||
        rendered_color_ != color ||
        rendered_icon_ != icon ||
        textChanged(rendered_label_, label) ||
        textChanged(rendered_value_, value);

    if (contentChanged) {
        standalone::icons::set(
            icon_,
            icon ? icon : "",
            standalone::icons::Size::L
        );
        lv_obj_set_style_text_color(icon_, lv_color_hex(color), 0);
        lv_label_set_text(label_, label ? label : "");
        copyCachedText(rendered_label_, label);
        if (value_) {
            lv_label_set_text(value_, value ? value : "");
            copyCachedText(rendered_value_, value);
        }
        rendered_color_ = color;
        rendered_icon_ = icon;
        rendered_property_ = props.property;
        rendered_custom_content_ = props.customContent;
        has_rendered_ = true;
        needsLayout = true;
    }

    if (value_) {
        if (valueVisible && lv_obj_has_flag(value_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(value_, LV_OBJ_FLAG_HIDDEN);
            needsLayout = true;
        } else if (!valueVisible && !lv_obj_has_flag(value_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(value_, LV_OBJ_FLAG_HIDDEN);
            needsLayout = true;
        }
    }

    if (needsLayout) {
        lv_obj_update_layout(container_);
        lv_obj_align(container_, LV_ALIGN_CENTER, 0, 0);
    }
}

}  // namespace core::ui

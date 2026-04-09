#include "ui/common/TrackHeaderRow.hpp"

#include <cstring>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace style = oc::ui::lvgl::style;

namespace {

constexpr uint32_t COLOR_DIM_TEXT = theme::color::TEXT_PRIMARY;
constexpr lv_coord_t ROW_HEIGHT = 16;
constexpr lv_coord_t HORIZONTAL_INSET = oc::ui::lvgl::base_theme::layout::MARGIN_SM + 4;
constexpr lv_opa_t LABEL_OPA = LV_OPA_80;
constexpr lv_coord_t ACCENT_WIDTH = 4;
constexpr lv_coord_t ITEM_SIZE = 7;
constexpr lv_coord_t ITEM_GAP = 4;

template <size_t N>
void setLabelTextIfChanged(lv_obj_t* label, std::array<char, N>& cache, const char* text) {
    if (!label) return;

    const char* next = (text && text[0]) ? text : "";
    if (std::strncmp(cache.data(), next, N) == 0) {
        return;
    }

    std::strncpy(cache.data(), next, N - 1);
    cache[N - 1] = '\0';
    lv_label_set_text(label, cache.data());
}

}  // namespace

TrackHeaderRow::TrackHeaderRow(lv_obj_t* parent) {
    createUI(parent);
}

TrackHeaderRow::~TrackHeaderRow() {
    if (container_) {
        lv_obj_delete(container_);
    }
}

FLASHMEM void TrackHeaderRow::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_)
        .size(LV_PCT(100), ROW_HEIGHT)
        .noScroll()
        .noBorder()
        .pad(0);
    lv_obj_set_style_pad_left(container_, 0, 0);
    lv_obj_set_style_pad_right(container_, HORIZONTAL_INSET, 0);
    lv_obj_set_style_pad_top(container_, 0, 0);
    lv_obj_set_style_pad_bottom(container_, 0, 0);
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(container_, 4, 0);

    accent_ = lv_obj_create(container_);
    style::apply(accent_)
        .size(ACCENT_WIDTH, LV_PCT(100))
        .noBorder()
        .noScroll()
        .pad(0);
    lv_obj_set_style_radius(accent_, 0, 0);
    lv_obj_set_style_bg_opa(accent_, LV_OPA_COVER, 0);

    label_ = lv_label_create(container_);
    lv_obj_set_style_text_font(label_, fonts.inter_14_medium, 0);
    lv_obj_set_style_text_color(label_, lv_color_hex(COLOR_DIM_TEXT), 0);
    lv_obj_set_style_text_opa(label_, LABEL_OPA, 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_CLIP);

    spacer_ = lv_obj_create(container_);
    style::apply(spacer_).size(0, 1).transparent().noBorder().noScroll().pad(0);
    lv_obj_set_flex_grow(spacer_, 1);

    items_row_ = lv_obj_create(container_);
    style::apply(items_row_).transparent().noBorder().noScroll().pad(0);
    lv_obj_set_layout(items_row_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(items_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(items_row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(items_row_, ITEM_GAP, 0);

    for (uint8_t i = 0; i < items_.size(); ++i) {
        items_[i] = lv_obj_create(items_row_);
        style::apply(items_[i])
            .size(ITEM_SIZE, ITEM_SIZE)
            .noBorder()
            .noScroll()
            .pad(0);
        lv_obj_set_style_radius(items_[i], 1, 0);
    }
}

void TrackHeaderRow::render(const TrackHeaderRowProps& props) {
    if (!container_) return;

    setLabelTextIfChanged(label_, left_text_cache_, props.leftText);

    if (!surface_cache_initialized_ || accent_cache_color_ != props.accentColor) {
        lv_obj_set_style_bg_color(accent_, lv_color_hex(props.accentColor), 0);
        accent_cache_color_ = props.accentColor;
    }
    if (!surface_cache_initialized_ || accent_cache_opa_ != props.accentOpa) {
        lv_obj_set_style_bg_opa(accent_, props.accentOpa, 0);
        accent_cache_opa_ = props.accentOpa;
    }

    if (!surface_cache_initialized_ || background_cache_color_ != props.backgroundColor) {
        lv_obj_set_style_bg_color(container_, lv_color_hex(props.backgroundColor), 0);
        background_cache_color_ = props.backgroundColor;
    }
    if (!surface_cache_initialized_ || background_cache_opa_ != props.backgroundOpa) {
        lv_obj_set_style_bg_opa(container_, props.backgroundOpa, 0);
        background_cache_opa_ = props.backgroundOpa;
    }

    for (uint8_t i = 0; i < items_.size(); ++i) {
        if (!surface_cache_initialized_ || item_color_cache_[i] != props.itemColors[i]) {
            lv_obj_set_style_bg_color(items_[i], lv_color_hex(props.itemColors[i]), 0);
            item_color_cache_[i] = props.itemColors[i];
        }
        if (!surface_cache_initialized_ || item_opa_cache_[i] != props.itemOpacities[i]) {
            lv_obj_set_style_bg_opa(items_[i], props.itemOpacities[i], 0);
            item_opa_cache_[i] = props.itemOpacities[i];
        }
    }

    surface_cache_initialized_ = true;
}

}  // namespace core::ui

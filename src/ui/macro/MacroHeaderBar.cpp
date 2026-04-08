#include "ui/macro/MacroHeaderBar.hpp"

#include <algorithm>
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
constexpr lv_coord_t HEADER_HEIGHT = 16;
constexpr lv_coord_t HORIZONTAL_INSET = oc::ui::lvgl::base_theme::layout::MARGIN_SM + 4;
constexpr lv_opa_t HEADER_OPA = LV_OPA_80;
constexpr lv_coord_t PAGE_ACCENT_WIDTH = 4;
constexpr lv_coord_t ACTIVITY_SIZE = 7;
constexpr lv_coord_t ACTIVITY_GAP = 4;
constexpr lv_opa_t HEADER_BG_OPA_IDLE = LV_OPA_TRANSP;
constexpr lv_opa_t HEADER_BG_OPA_CLUTCH = LV_OPA_TRANSP;
constexpr lv_opa_t PAGE_SELECTOR_BASE_OPA_ENABLED = static_cast<lv_opa_t>(18);
constexpr lv_opa_t PAGE_SELECTOR_BASE_OPA_DISABLED = static_cast<lv_opa_t>(8);
constexpr lv_opa_t PAGE_SELECTOR_ACTIVE_BONUS = static_cast<lv_opa_t>(48);
constexpr lv_opa_t PAGE_SELECTOR_PREVIEW_BONUS = static_cast<lv_opa_t>(120);
constexpr lv_opa_t ACTIVITY_VELOCITY_RANGE = static_cast<lv_opa_t>(36);
constexpr uint32_t HEADER_ACCENT_COLOR = theme::color::TEXT_PRIMARY;
constexpr uint32_t HEADER_BG_COLOR = theme::color::TEXT_PRIMARY;
constexpr uint32_t ACTIVITY_COLOR = theme::color::TEXT_PRIMARY;

lv_opa_t pageSelectorOpa(uint8_t activity, bool isActive, bool isPreview, bool enabled) {
    uint16_t opa = enabled ? PAGE_SELECTOR_BASE_OPA_ENABLED : PAGE_SELECTOR_BASE_OPA_DISABLED;
    if (isActive) opa += PAGE_SELECTOR_ACTIVE_BONUS;
    if (isPreview) opa += PAGE_SELECTOR_PREVIEW_BONUS;
    opa += static_cast<uint16_t>(activity) * static_cast<uint16_t>(ACTIVITY_VELOCITY_RANGE) / 127U;
    return static_cast<lv_opa_t>(std::min<uint16_t>(opa, LV_OPA_COVER));
}

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

MacroHeaderBar::MacroHeaderBar(lv_obj_t* parent) {
    createUI(parent);
}

MacroHeaderBar::~MacroHeaderBar() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        top_row_ = nullptr;
        page_accent_ = nullptr;
        left_label_ = nullptr;
        top_row_spacer_ = nullptr;
        channel_activity_row_ = nullptr;
    }
}

FLASHMEM void MacroHeaderBar::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_)
        .size(LV_PCT(100), HEADER_HEIGHT)
        .transparent()
        .pad(0)
        .noScroll()
        .noBorder();

    top_row_ = lv_obj_create(container_);
    style::apply(top_row_)
        .size(LV_PCT(100), LV_PCT(100))
        .noScroll()
        .noBorder()
        .pad(0);
    lv_obj_set_style_bg_opa(top_row_, HEADER_BG_OPA_IDLE, 0);
    lv_obj_set_style_pad_left(top_row_, 0, 0);
    lv_obj_set_style_pad_right(top_row_, HORIZONTAL_INSET, 0);
    lv_obj_set_style_pad_top(top_row_, 0, 0);
    lv_obj_set_style_pad_bottom(top_row_, 0, 0);
    lv_obj_set_layout(top_row_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top_row_, 4, 0);

    page_accent_ = lv_obj_create(top_row_);
    style::apply(page_accent_)
        .size(PAGE_ACCENT_WIDTH, LV_PCT(100))
        .noBorder()
        .noScroll()
        .pad(0);
    lv_obj_set_style_radius(page_accent_, 0, 0);
    lv_obj_set_style_bg_opa(page_accent_, LV_OPA_COVER, 0);

    left_label_ = lv_label_create(top_row_);
    lv_obj_set_style_text_font(left_label_, fonts.inter_14_medium, 0);
    lv_obj_set_style_text_color(left_label_, lv_color_hex(COLOR_DIM_TEXT), 0);
    lv_obj_set_style_text_opa(left_label_, HEADER_OPA, 0);
    lv_label_set_long_mode(left_label_, LV_LABEL_LONG_CLIP);

    top_row_spacer_ = lv_obj_create(top_row_);
    style::apply(top_row_spacer_).size(0, 1).transparent().noBorder().noScroll().pad(0);
    lv_obj_set_flex_grow(top_row_spacer_, 1);

    channel_activity_row_ = lv_obj_create(top_row_);
    style::apply(channel_activity_row_).transparent().noBorder().noScroll().pad(0);
    lv_obj_set_layout(channel_activity_row_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(channel_activity_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        channel_activity_row_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(channel_activity_row_, ACTIVITY_GAP, 0);

    for (uint8_t i = 0; i < channel_activity_items_.size(); ++i) {
        channel_activity_items_[i] = lv_obj_create(channel_activity_row_);
        style::apply(channel_activity_items_[i])
            .size(ACTIVITY_SIZE, ACTIVITY_SIZE)
            .noBorder()
            .noScroll()
            .pad(0);
        lv_obj_set_style_radius(channel_activity_items_[i], 1, 0);
        lv_obj_set_style_bg_color(
            channel_activity_items_[i],
            lv_color_hex(ACTIVITY_COLOR),
            0
        );
        lv_obj_set_style_bg_opa(channel_activity_items_[i], PAGE_SELECTOR_BASE_OPA_ENABLED, 0);
    }
}

void MacroHeaderBar::render(const MacroHeaderBarProps& props) {
    if (!container_) return;

    const lv_opa_t bgOpa = props.clutchActive ? HEADER_BG_OPA_CLUTCH : HEADER_BG_OPA_IDLE;
    const bool clutchChanged = !cache_initialized_ || cached_clutch_active_ != props.clutchActive;
    const bool pageChanged = !cache_initialized_ || cached_page_ != props.activePage;
    const bool previewPageChanged = !cache_initialized_ || cached_preview_page_ != props.previewPage;
    const bool selectingPageChanged =
        !cache_initialized_ || cached_selecting_page_ != props.selectingPage;
    const bool enabledMaskChanged =
        !cache_initialized_ || cached_enabled_mask_ != props.enabledMask;

    if (!cache_initialized_) {
        lv_obj_set_style_bg_color(page_accent_, lv_color_hex(HEADER_ACCENT_COLOR), 0);
        cached_page_ = props.activePage;
    }

    if (pageChanged || clutchChanged) {
        lv_obj_set_style_bg_color(top_row_, lv_color_hex(HEADER_BG_COLOR), 0);
        lv_obj_set_style_bg_opa(top_row_, bgOpa, 0);
        cached_page_ = props.activePage;
    }

    setLabelTextIfChanged(left_label_, left_text_cache_, props.leftText);

    if (!cache_initialized_ || cached_activity_ != props.pageOutputActivity ||
        selectingPageChanged || previewPageChanged || pageChanged || enabledMaskChanged) {
        for (uint8_t i = 0; i < channel_activity_items_.size(); ++i) {
            auto* item = channel_activity_items_[i];
            if (!item) continue;

            const bool isActive = props.activePage == i;
            const bool isPreview = props.previewPage == i;
            const bool enabled =
                (props.enabledMask & static_cast<uint8_t>(1U << i)) != 0;
            lv_obj_set_style_bg_color(item, lv_color_hex(ACTIVITY_COLOR), 0);
            lv_obj_set_style_bg_opa(
                item,
                pageSelectorOpa(props.pageOutputActivity[i], isActive, isPreview, enabled),
                0
            );
        }
        cached_activity_ = props.pageOutputActivity;
    }

    cached_clutch_active_ = props.clutchActive;
    cached_selecting_page_ = props.selectingPage;
    cached_preview_page_ = props.previewPage;
    cached_enabled_mask_ = props.enabledMask;
    cache_initialized_ = true;
}

}  // namespace core::ui

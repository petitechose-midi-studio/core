#include "ui/common/CompactMetricRow.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/font/StandaloneFonts.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {
namespace {

template <size_t N>
FLASHMEM void setMetricText(
    lv_obj_t* label,
    std::array<char, N>& cache,
    const char* text
) {
    if (!label) return;
    const char* next = text ? text : "";
    if (std::strncmp(cache.data(), next, N) == 0) return;
    std::strncpy(cache.data(), next, N - 1U);
    cache[N - 1U] = '\0';
    lv_label_set_text_static(label, cache.data());
}

}  // namespace

FLASHMEM bool CompactMetricRow::create(lv_obj_t* parent) {
    if (container_) return true;
    if (!parent) return false;

    container_ = lv_obj_create(parent);
    if (!container_) return false;
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        container_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(container_, 5, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    for (auto& widgets : widgets_) {
        widgets.group = lv_obj_create(container_);
        if (!widgets.group) return false;
        lv_obj_remove_style_all(widgets.group);
        lv_obj_set_size(widgets.group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_layout(widgets.group, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(widgets.group, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            widgets.group,
            LV_FLEX_ALIGN_START,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER
        );
        lv_obj_set_style_pad_column(widgets.group, 1, 0);
        lv_obj_clear_flag(widgets.group, LV_OBJ_FLAG_SCROLLABLE);

        widgets.icon = lv_label_create(widgets.group);
        widgets.value = lv_label_create(widgets.group);
        if (!widgets.icon || !widgets.value) return false;
        lv_label_set_text_static(widgets.icon, "");
        lv_label_set_text_static(widgets.value, "");
        lv_obj_set_style_text_font(
            widgets.icon,
            standalone_fonts.icons_14
                ? standalone_fonts.icons_14
                : LV_FONT_DEFAULT,
            0
        );
        lv_obj_set_style_text_font(
            widgets.value,
            fonts.meta_label() ? fonts.meta_label() : LV_FONT_DEFAULT,
            0
        );
        lv_obj_set_style_text_color(
            widgets.icon,
            lv_color_hex(standalone::theme::color::TEXT_SECONDARY),
            0
        );
        lv_obj_set_style_text_color(
            widgets.value,
            lv_color_hex(standalone::theme::color::TEXT_SECONDARY),
            0
        );
        // Icons provide recognition while the value carries the information.
        // Keeping that distinction visible prevents compact headers from
        // becoming a row of equally loud glyphs and numbers.
        lv_obj_set_style_text_opa(widgets.icon, LV_OPA_70, 0);
        lv_obj_set_style_text_opa(widgets.value, LV_OPA_90, 0);
    }
    return true;
}

FLASHMEM bool CompactMetricRow::render(
    const std::array<CompactMetricProps, METRIC_COUNT>& props
) {
    if (!container_) return false;

    bool anyVisible = false;
    for (size_t index = 0; index < widgets_.size(); ++index) {
        const auto& metric = props[index];
        auto& widgets = widgets_[index];
        auto& cache = cache_[index];
        const bool visible = metric.icon && metric.icon[0] != '\0' &&
            metric.value && metric.value[0] != '\0';
        anyVisible = anyVisible || visible;
        setMetricText(widgets.icon, cache.icon, metric.icon);
        setMetricText(widgets.value, cache.value, metric.value);
        if (visible == cache.visible) continue;
        if (visible) {
            lv_obj_clear_flag(widgets.group, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(widgets.group, LV_OBJ_FLAG_HIDDEN);
        }
        cache.visible = visible;
    }

    if (anyVisible != visible_) {
        if (anyVisible) {
            lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        }
        visible_ = anyVisible;
    }
    return anyVisible;
}

}  // namespace core::ui

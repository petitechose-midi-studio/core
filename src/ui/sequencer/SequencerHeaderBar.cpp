#include "SequencerHeaderBar.hpp"

#include <algorithm>
#include <cstring>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <ms/ui/font/CoreFonts.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace style = oc::ui::lvgl::style;

namespace {

constexpr uint32_t COLOR_PROGRESS = 0x5CA8EE;  // Match SequencerView play color
constexpr uint32_t COLOR_DIM_TEXT = oc::ui::lvgl::base_theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t OPA_DIM_TEXT = static_cast<lv_opa_t>(oc::ui::lvgl::base_theme::opacity::OPA_50);

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

SequencerHeaderBar::SequencerHeaderBar(lv_obj_t* parent) {
    createUI(parent);
}

SequencerHeaderBar::~SequencerHeaderBar() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        top_row_ = nullptr;
        strip_row_ = nullptr;
        left_label_ = nullptr;
        center_label_ = nullptr;
        right_label_ = nullptr;
    }
}

void SequencerHeaderBar::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_)
        .size(LV_PCT(100), STRIP_HEIGHT)
        .transparent()
        .pad(0)
        .noScroll()
        .noBorder();

    // Strip row (8 segments)
    strip_row_ = lv_obj_create(container_);
    style::apply(strip_row_)
        .size(LV_PCT(100), STRIP_HEIGHT)
        .transparent()
        .noScroll()
        .noBorder()
        .padH(theme::layout::PAD_SM)
        .flexRow(LV_FLEX_ALIGN_START, 2);

    for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
        auto& seg = segments_[i];

        seg.container = lv_obj_create(strip_row_);
        lv_obj_clear_flag(seg.container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_height(seg.container, STRIP_HEIGHT);
        lv_obj_set_width(seg.container, 0);
        lv_obj_set_flex_grow(seg.container, 1);
        lv_obj_set_style_pad_all(seg.container, 0, 0);
        lv_obj_set_style_border_width(seg.container, 0, 0);
        lv_obj_set_style_radius(seg.container, 1, 0);
        lv_obj_set_style_bg_opa(seg.container, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg.container, lv_color_hex(theme::color::KNOB_BACKGROUND), 0);

        seg.valid = lv_obj_create(seg.container);
        lv_obj_clear_flag(seg.valid, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(seg.valid, 0, STRIP_HEIGHT);
        lv_obj_set_style_pad_all(seg.valid, 0, 0);
        lv_obj_set_style_border_width(seg.valid, 0, 0);
        lv_obj_set_style_radius(seg.valid, 1, 0);
        lv_obj_set_style_bg_opa(seg.valid, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg.valid, lv_color_hex(theme::color::INACTIVE), 0);
        lv_obj_set_pos(seg.valid, 0, 0);

        seg.progress = lv_obj_create(seg.container);
        lv_obj_clear_flag(seg.progress, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(seg.progress, 0, STRIP_HEIGHT);
        lv_obj_set_style_pad_all(seg.progress, 0, 0);
        lv_obj_set_style_border_width(seg.progress, 0, 0);
        lv_obj_set_style_radius(seg.progress, 1, 0);
        lv_obj_set_style_bg_opa(seg.progress, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg.progress, lv_color_hex(COLOR_PROGRESS), 0);
        lv_obj_set_pos(seg.progress, 0, 0);

        seg.marker = lv_obj_create(seg.container);
        lv_obj_clear_flag(seg.marker, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(seg.marker, MARKER_WIDTH, STRIP_HEIGHT);
        lv_obj_set_style_pad_all(seg.marker, 0, 0);
        lv_obj_set_style_border_width(seg.marker, 0, 0);
        lv_obj_set_style_radius(seg.marker, 0, 0);
        lv_obj_set_style_bg_opa(seg.marker, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg.marker, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
        lv_obj_set_pos(seg.marker, 0, 0);
        lv_obj_add_flag(seg.marker, LV_OBJ_FLAG_HIDDEN);
    }
}

void SequencerHeaderBar::render(const SequencerHeaderBarProps& props) {
    renderTopRow(props);
    renderStrip(props);
}

void SequencerHeaderBar::renderTopRow(const SequencerHeaderBarProps& props) {
    if (!left_label_ || !center_label_ || !right_label_) return;

    if (!top_row_cache_initialized_ || top_row_dimmed_ != props.dimmed) {
        const lv_color_t propertyColor = lv_color_hex(theme::color::TEXT_PRIMARY);
        const lv_opa_t propertyOpa = LV_OPA_COVER;

        const lv_color_t stepColor = lv_color_hex(
            props.dimmed ? COLOR_DIM_TEXT : theme::color::TEXT_PRIMARY
        );
        const lv_opa_t stepOpa = props.dimmed ? OPA_DIM_TEXT : LV_OPA_COVER;

        // Property label is global context: never dim it based on focused step state.
        lv_obj_set_style_text_color(left_label_, propertyColor, 0);
        lv_obj_set_style_text_opa(left_label_, propertyOpa, 0);

        // Value and step index depend on focused step state.
        lv_obj_set_style_text_color(center_label_, stepColor, 0);
        lv_obj_set_style_text_color(right_label_, stepColor, 0);
        lv_obj_set_style_text_opa(center_label_, stepOpa, 0);
        lv_obj_set_style_text_opa(right_label_, stepOpa, 0);

        top_row_dimmed_ = props.dimmed;
        top_row_cache_initialized_ = true;
    }

    setLabelTextIfChanged(left_label_, left_text_cache_, props.leftText);
    setLabelTextIfChanged(center_label_, center_text_cache_, props.centerText);
    setLabelTextIfChanged(right_label_, right_text_cache_, props.rightText);
}

void SequencerHeaderBar::renderStrip(const SequencerHeaderBarProps& props) {
    if (!strip_row_) return;

    const uint8_t len = std::min<uint8_t>(props.length, static_cast<uint8_t>(PAGE_COUNT * STEPS_PER_PAGE));
    const bool playing = (props.playheadStep >= 0) && (props.playheadStep < len);
    const int16_t playhead = playing ? props.playheadStep : -1;

    bool widthChanged = false;
    lv_coord_t stripWidth = lv_obj_get_width(strip_row_);
    if (stripWidth <= 0 || stripWidth != strip_cached_width_) {
        lv_obj_update_layout(strip_row_);
        stripWidth = lv_obj_get_width(strip_row_);
        if (stripWidth != strip_cached_width_) {
            strip_cached_width_ = stripWidth;
            widthChanged = true;
            for (auto& cache : segment_cache_) {
                cache.initialized = false;
            }
        }
    }

    const bool stripStateChanged =
        !strip_cache_initialized_ ||
        strip_cached_length_ != len ||
        strip_cached_viewed_page_ != props.viewedPage ||
        strip_cached_playhead_ != playhead;

    if (!stripStateChanged && !widthChanged) {
        return;
    }

    strip_cache_initialized_ = true;
    strip_cached_length_ = len;
    strip_cached_viewed_page_ = props.viewedPage;
    strip_cached_playhead_ = playhead;

    for (uint8_t p = 0; p < PAGE_COUNT; ++p) {
        auto& seg = segments_[p];
        auto& cache = segment_cache_[p];
        if (!seg.container || !seg.valid || !seg.progress || !seg.marker) continue;

        const lv_coord_t w = lv_obj_get_width(seg.container);
        if (w <= 0) continue;

        const bool initialized = cache.initialized;
        if (!initialized || cache.width != w) {
            cache.width = w;
            cache.validWidth = -1;
            cache.progressWidth = -1;
            cache.markerVisible = false;
            cache.markerX = -1;
            cache.validColorHex = 0;
        }

        const uint8_t pageStart = static_cast<uint8_t>(p * STEPS_PER_PAGE);
        const int16_t remaining = static_cast<int16_t>(len) - static_cast<int16_t>(pageStart);
        const uint8_t validSteps = (remaining <= 0)
            ? 0
            : static_cast<uint8_t>(std::min<int16_t>(remaining, static_cast<int16_t>(STEPS_PER_PAGE)));

        const lv_coord_t validW = static_cast<lv_coord_t>((static_cast<int32_t>(w) * validSteps) / STEPS_PER_PAGE);

        uint8_t progressSteps = 0;
        bool showMarker = false;
        uint8_t playheadInPage = 0;

        if (playing && validSteps > 0) {
            if (playhead < pageStart) {
                progressSteps = 0;
            } else if (playhead >= static_cast<int16_t>(pageStart + validSteps)) {
                progressSteps = validSteps;
            } else {
                playheadInPage = static_cast<uint8_t>(playhead - pageStart);
                progressSteps = playheadInPage;  // Steps before the playhead
                showMarker = true;
            }
        }

        const lv_coord_t progressW = static_cast<lv_coord_t>((static_cast<int32_t>(w) * progressSteps) / STEPS_PER_PAGE);
        const lv_coord_t clampedProgressW = std::min(progressW, validW);

        if (!initialized || cache.validWidth != validW) {
            lv_obj_set_width(seg.valid, validW);
            cache.validWidth = validW;
        }

        if (!initialized || cache.progressWidth != clampedProgressW) {
            lv_obj_set_width(seg.progress, clampedProgressW);
            cache.progressWidth = clampedProgressW;
        }

        // Highlight the viewed page by brightening the valid baseline.
        const bool isViewed = (p == props.viewedPage);
        const uint32_t validColor = isViewed
            ? oc::ui::lvgl::base_theme::color::INACTIVE_LIGHTER
            : theme::color::INACTIVE;
        if (!initialized || cache.validColorHex != validColor) {
            lv_obj_set_style_bg_color(seg.valid, lv_color_hex(validColor), 0);
            cache.validColorHex = validColor;
        }

        if (!showMarker) {
            if (!initialized || cache.markerVisible) {
                lv_obj_add_flag(seg.marker, LV_OBJ_FLAG_HIDDEN);
            }
            cache.markerVisible = false;
            cache.markerX = -1;
            cache.initialized = true;
            continue;
        }

        // Place marker at the center of the current step.
        const float stepCenter = (static_cast<float>(playheadInPage) + 0.5f) / static_cast<float>(STEPS_PER_PAGE);
        lv_coord_t x = static_cast<lv_coord_t>(stepCenter * static_cast<float>(w) - (MARKER_WIDTH / 2.0f));
        const lv_coord_t maxX = std::max<lv_coord_t>(0, validW - MARKER_WIDTH);
        x = std::clamp<lv_coord_t>(x, 0, maxX);

        if (!initialized || !cache.markerVisible) {
            lv_obj_clear_flag(seg.marker, LV_OBJ_FLAG_HIDDEN);
        }

        if (!initialized || cache.markerX != x) {
            lv_obj_set_pos(seg.marker, x, 0);
            cache.markerX = x;
        }

        cache.markerVisible = true;
        cache.initialized = true;
    }
}

}  // namespace core::ui

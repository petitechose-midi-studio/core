#include "SequencerHeaderBar.hpp"

#include <algorithm>

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
        .size(LV_PCT(100), static_cast<lv_coord_t>(theme::layout::TOP_BAR_HEIGHT + STRIP_HEIGHT))
        .bgColor(theme::color::BACKGROUND)
        .pad(0)
        .noScroll()
        .noBorder();
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(container_, 0, LV_STATE_DEFAULT);

    // Top row (text)
    top_row_ = lv_obj_create(container_);
    style::apply(top_row_)
        .size(LV_PCT(100), theme::layout::TOP_BAR_HEIGHT)
        .transparent()
        .noScroll()
        .noBorder()
        .padH(theme::layout::PAD_SM)
        .flexRow(LV_FLEX_ALIGN_SPACE_BETWEEN, 0);

    left_label_ = lv_label_create(top_row_);
    lv_label_set_text(left_label_, "");
    lv_obj_set_width(left_label_, LV_PCT(34));
    lv_label_set_long_mode(left_label_, LV_LABEL_LONG_CLIP);
    style::apply(left_label_)
        .textFont(fonts.inter_14_medium)
        .textColor(theme::color::TEXT_PRIMARY)
        .textAlign(LV_TEXT_ALIGN_LEFT);

    center_label_ = lv_label_create(top_row_);
    lv_label_set_text(center_label_, "");
    lv_obj_set_width(center_label_, LV_PCT(32));
    lv_label_set_long_mode(center_label_, LV_LABEL_LONG_CLIP);
    style::apply(center_label_)
        .textFont(fonts.inter_14_semibold)
        .textColor(theme::color::TEXT_PRIMARY)
        .textAlign(LV_TEXT_ALIGN_CENTER);

    right_label_ = lv_label_create(top_row_);
    lv_label_set_text(right_label_, "");
    lv_obj_set_width(right_label_, LV_PCT(34));
    lv_label_set_long_mode(right_label_, LV_LABEL_LONG_CLIP);
    style::apply(right_label_)
        .textFont(fonts.inter_14_medium)
        .textColor(theme::color::TEXT_PRIMARY)
        .textAlign(LV_TEXT_ALIGN_RIGHT);

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

    lv_label_set_text(left_label_, (props.leftText && props.leftText[0]) ? props.leftText : "");
    lv_label_set_text(center_label_, (props.centerText && props.centerText[0]) ? props.centerText : "");
    lv_label_set_text(right_label_, (props.rightText && props.rightText[0]) ? props.rightText : "");
}

void SequencerHeaderBar::renderStrip(const SequencerHeaderBarProps& props) {
    if (!strip_row_) return;

    const uint8_t len = std::min<uint8_t>(props.length, static_cast<uint8_t>(PAGE_COUNT * STEPS_PER_PAGE));
    const bool playing = (props.playheadStep >= 0) && (props.playheadStep < len);
    const int16_t playhead = playing ? props.playheadStep : -1;

    // Ensure flex sizes are resolved before querying widths.
    lv_obj_update_layout(strip_row_);

    for (uint8_t p = 0; p < PAGE_COUNT; ++p) {
        auto& seg = segments_[p];
        if (!seg.container || !seg.valid || !seg.progress || !seg.marker) continue;

        lv_obj_update_layout(seg.container);
        const lv_coord_t w = lv_obj_get_width(seg.container);
        if (w <= 0) continue;

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
        lv_obj_set_width(seg.valid, validW);
        lv_obj_set_width(seg.progress, std::min(progressW, validW));

        // Highlight the viewed page by brightening the valid baseline.
        const bool isViewed = (p == props.viewedPage);
        const uint32_t validColor = isViewed
            ? oc::ui::lvgl::base_theme::color::INACTIVE_LIGHTER
            : theme::color::INACTIVE;
        lv_obj_set_style_bg_color(seg.valid, lv_color_hex(validColor), 0);

        if (!showMarker) {
            lv_obj_add_flag(seg.marker, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        // Place marker at the center of the current step.
        const float stepCenter = (static_cast<float>(playheadInPage) + 0.5f) / static_cast<float>(STEPS_PER_PAGE);
        lv_coord_t x = static_cast<lv_coord_t>(stepCenter * static_cast<float>(w) - (MARKER_WIDTH / 2.0f));
        const lv_coord_t maxX = std::max<lv_coord_t>(0, validW - MARKER_WIDTH);
        x = std::clamp<lv_coord_t>(x, 0, maxX);
        lv_obj_set_pos(seg.marker, x, 0);
        lv_obj_clear_flag(seg.marker, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace core::ui

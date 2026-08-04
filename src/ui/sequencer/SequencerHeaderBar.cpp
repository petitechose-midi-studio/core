#include "SequencerHeaderBar.hpp"

#include <cstring>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/sequencer/SequencerHeaderBarRenderModel.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace header_model = core::ui::sequencer::header_bar;
namespace style = oc::ui::lvgl::style;

namespace {

constexpr uint32_t COLOR_DIM_TEXT = theme::color::TEXT_PRIMARY;
constexpr lv_coord_t HORIZONTAL_INSET = oc::ui::lvgl::base_theme::layout::MARGIN_SM + 4;
constexpr lv_coord_t ACCENT_WIDTH = 4;
constexpr lv_coord_t LABEL_MAX_WIDTH = 46;
constexpr lv_coord_t BADGE_WIDTH = 52;
constexpr lv_coord_t TITLE_SEPARATOR_WIDTH = 5;
constexpr uint32_t PAGE_CURSOR_COLOR = theme::color::MACRO_1;
constexpr uint32_t PAGE_SELECTION_COLOR = theme::color::MACRO_6;
constexpr uint32_t PAGE_DUPLICATE_PREVIEW_COLOR = theme::color::MACRO_4;
constexpr uint32_t PAGE_DUPLICATE_OVERWRITE_COLOR = 0xFFB000;
constexpr uint32_t PAGE_DUPLICATE_BLOCKED_COLOR =
    theme::color::MACRO_AUTOMATION_RECORDING;

template <size_t N>
FLASHMEM void setLabelTextIfChanged(lv_obj_t* label, std::array<char, N>& cache, const char* text) {
    if (!label) return;

    const char* next = (text && text[0]) ? text : "";
    if (std::strncmp(cache.data(), next, N) == 0) {
        return;
    }

    std::strncpy(cache.data(), next, N - 1);
    cache[N - 1] = '\0';
    lv_label_set_text(label, cache.data());
}

FLASHMEM void drawStripRect(lv_layer_t* layer,
                            const lv_area_t& area,
                            lv_color_t color,
                            lv_opa_t opa,
                            lv_coord_t radius,
                            lv_coord_t borderWidth = 0,
                            lv_color_t borderColor = lv_color_black(),
                            lv_opa_t borderOpa = LV_OPA_TRANSP) {
    if (!layer) return;

    lv_draw_rect_dsc_t rectDsc;
    lv_draw_rect_dsc_init(&rectDsc);
    rectDsc.bg_color = color;
    rectDsc.bg_opa = opa;
    rectDsc.radius = radius;
    rectDsc.border_width = borderWidth;
    rectDsc.border_color = borderColor;
    rectDsc.border_opa = borderOpa;
    lv_draw_rect(layer, &rectDsc, &area);
}

FLASHMEM lv_area_t markerArea(const lv_area_t& segmentArea, lv_coord_t y) {
    constexpr lv_coord_t MARKER_INSET = 1;
    return lv_area_t{
        .x1 = static_cast<lv_coord_t>(segmentArea.x1 + MARKER_INSET),
        .y1 = y,
        .x2 = static_cast<lv_coord_t>(segmentArea.x2 - MARKER_INSET),
        .y2 = static_cast<lv_coord_t>(y + header_model::STRIP_CURSOR_HEIGHT - 1),
    };
}

}  // namespace

FLASHMEM SequencerHeaderBar::SequencerHeaderBar(lv_obj_t* parent) {
    createUI(parent);
}

FLASHMEM SequencerHeaderBar::~SequencerHeaderBar() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        accent_ = nullptr;
        label_ = nullptr;
        badge_ = nullptr;
        strip_row_ = nullptr;
        view_cursor_ = nullptr;
        strip_cursor_ = nullptr;
    }
}

FLASHMEM void SequencerHeaderBar::createUI(lv_obj_t* parent) {
    if (!parent) return;

    container_ = lv_obj_create(parent);
    style::apply(container_)
        .size(LV_PCT(100), HEADER_HEIGHT)
        .pad(0)
        .noScroll()
        .noBorder();
    lv_obj_set_layout(container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        container_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_left(container_, 0, 0);
    lv_obj_set_style_pad_right(container_, HORIZONTAL_INSET, 0);
    lv_obj_set_style_pad_top(container_, 0, 0);
    lv_obj_set_style_pad_bottom(container_, 0, 0);
    lv_obj_set_style_pad_column(container_, 4, 0);
    lv_obj_add_flag(container_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

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
    lv_obj_set_style_text_opa(label_, LV_OPA_80, 0);
    lv_label_set_long_mode(label_, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label_, LABEL_MAX_WIDTH);

    badge_ = lv_label_create(container_);
    lv_obj_set_style_text_font(badge_, fonts.inter_13_bold, 0);
    lv_obj_set_style_text_color(badge_, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_opa(badge_, LV_OPA_80, 0);
    lv_obj_set_style_radius(badge_, 3, 0);
    lv_obj_set_style_pad_left(badge_, 4, 0);
    lv_obj_set_style_pad_right(badge_, 4, 0);
    lv_obj_set_style_pad_top(badge_, 1, 0);
    lv_obj_set_style_pad_bottom(badge_, 1, 0);
    lv_label_set_long_mode(badge_, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(badge_, BADGE_WIDTH);
    lv_obj_set_style_text_align(badge_, LV_TEXT_ALIGN_CENTER, 0);

    auto* spacer = lv_obj_create(container_);
    style::apply(spacer).size(0, 1).transparent().noBorder().noScroll().pad(0);
    lv_obj_set_width(spacer, TITLE_SEPARATOR_WIDTH);

    strip_row_ = lv_obj_create(container_);
    style::apply(strip_row_)
        .size(0, header_model::STRIP_ROW_HEIGHT)
        .transparent()
        .noScroll()
        .noBorder()
        .pad(0);
    lv_obj_set_flex_grow(strip_row_, 1);
    lv_obj_add_flag(strip_row_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(strip_row_, onStripDrawEvent, LV_EVENT_DRAW_MAIN, this);

    view_cursor_ = lv_obj_create(strip_row_);
    lv_obj_remove_style_all(view_cursor_);
    lv_obj_add_flag(view_cursor_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(view_cursor_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(view_cursor_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view_cursor_, header_model::VIEW_CURSOR_WIDTH, 0);
    lv_obj_set_style_border_color(view_cursor_, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
    lv_obj_set_style_border_opa(view_cursor_, header_model::VIEW_CURSOR_OPA, 0);
    lv_obj_set_style_radius(view_cursor_, header_model::STRIP_RADIUS, 0);
    lv_obj_add_flag(view_cursor_, LV_OBJ_FLAG_HIDDEN);

    strip_cursor_ = lv_obj_create(strip_row_);
    lv_obj_remove_style_all(strip_cursor_);
    lv_obj_add_flag(strip_cursor_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(strip_cursor_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(strip_cursor_, 1, 0);
    lv_obj_set_style_border_width(strip_cursor_, 0, 0);
    lv_obj_set_style_bg_color(strip_cursor_, lv_color_hex(PAGE_CURSOR_COLOR), 0);
    lv_obj_set_style_bg_opa(strip_cursor_, LV_OPA_COVER, 0);
    lv_obj_add_flag(strip_cursor_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM void SequencerHeaderBar::render(const SequencerHeaderBarProps& props) {
    renderTopRowOnly(props);
    renderStripOnly(props);
}

FLASHMEM void SequencerHeaderBar::renderTopRowOnly(const SequencerHeaderBarProps& props) {
    renderTopRow(props);
}

FLASHMEM void SequencerHeaderBar::renderStripOnly(const SequencerHeaderBarProps& props) {
    renderStrip(props);
}

FLASHMEM void SequencerHeaderBar::renderTopRow(const SequencerHeaderBarProps& props) {
    if (!container_ || !accent_ || !label_ || !badge_) return;

    setLabelTextIfChanged(label_, left_text_cache_, props.leftText);
    setLabelTextIfChanged(badge_, badge_text_cache_, props.badgeText.data());
    const auto visual = header_model::buildTopRowVisualState(props);

    if (!badge_cache_initialized_ || badge_bg_color_cache_ != visual.badgeBgColor) {
        lv_obj_set_style_bg_color(badge_, lv_color_hex(visual.badgeBgColor), 0);
        badge_bg_color_cache_ = visual.badgeBgColor;
    }
    if (!badge_cache_initialized_ || badge_bg_opa_cache_ != visual.badgeBgOpa) {
        lv_obj_set_style_bg_opa(badge_, visual.badgeBgOpa, 0);
        badge_bg_opa_cache_ = visual.badgeBgOpa;
    }
    if (!badge_cache_initialized_ || badge_border_width_cache_ != visual.badgeBorderWidth) {
        lv_obj_set_style_border_width(badge_, visual.badgeBorderWidth, 0);
        badge_border_width_cache_ = visual.badgeBorderWidth;
    }
    if (!badge_cache_initialized_ || badge_border_color_cache_ != visual.badgeBorderColor) {
        lv_obj_set_style_border_color(badge_, lv_color_hex(visual.badgeBorderColor), 0);
        badge_border_color_cache_ = visual.badgeBorderColor;
    }
    if (!badge_cache_initialized_ || badge_border_opa_cache_ != visual.badgeBorderOpa) {
        lv_obj_set_style_border_opa(badge_, visual.badgeBorderOpa, 0);
        badge_border_opa_cache_ = visual.badgeBorderOpa;
    }
    if (!badge_cache_initialized_ || badge_text_opa_cache_ != visual.badgeTextOpa) {
        lv_obj_set_style_text_opa(badge_, visual.badgeTextOpa, 0);
        badge_text_opa_cache_ = visual.badgeTextOpa;
    }
    if (!surface_cache_initialized_ || accent_cache_color_ != visual.accentColor) {
        lv_obj_set_style_bg_color(accent_, lv_color_hex(visual.accentColor), 0);
        accent_cache_color_ = visual.accentColor;
    }
    if (!surface_cache_initialized_ || accent_cache_opa_ != visual.accentOpa) {
        lv_obj_set_style_bg_opa(accent_, visual.accentOpa, 0);
        accent_cache_opa_ = visual.accentOpa;
    }
    if (!surface_cache_initialized_ || background_cache_color_ != visual.backgroundColor) {
        lv_obj_set_style_bg_color(container_, lv_color_hex(visual.backgroundColor), 0);
        background_cache_color_ = visual.backgroundColor;
    }
    if (!surface_cache_initialized_ || background_cache_opa_ != visual.backgroundOpa) {
        lv_obj_set_style_bg_opa(container_, visual.backgroundOpa, 0);
        background_cache_opa_ = visual.backgroundOpa;
    }

    surface_cache_initialized_ = true;
    badge_cache_initialized_ = true;
}

FLASHMEM void SequencerHeaderBar::onStripDrawEvent(lv_event_t* event) {
    auto* self = static_cast<SequencerHeaderBar*>(lv_event_get_user_data(event));
    if (!self || !self->strip_row_) return;

    lv_layer_t* layer = lv_event_get_layer(event);
    if (!layer) return;

    lv_area_t stripCoords{};
    lv_obj_get_coords(self->strip_row_, &stripCoords);

    const auto& props = self->strip_draw_props_;
    const auto stripState = header_model::buildStripState(props);

    for (uint8_t p = 0; p < PAGE_COUNT; ++p) {
        const auto visual = header_model::buildStripSegmentVisual(
            props,
            stripState,
            self->strip_segment_geometry_[p],
            stripCoords,
            p
        );
        if (!visual.visible) continue;

        drawStripRect(layer,
                      visual.segmentArea,
                      stripState.disabledColor,
                      visual.containerBgOpa,
                      header_model::STRIP_RADIUS,
                      0,
                      lv_color_hex(theme::color::TEXT_PRIMARY),
                      LV_OPA_TRANSP);

        if (visual.drawValidFill) {
            drawStripRect(
                layer,
                visual.validArea,
                visual.validColor,
                LV_OPA_COVER,
                header_model::STRIP_RADIUS
            );
        }

        if (visual.sourceMarker) {
            const auto area = markerArea(
                visual.segmentArea,
                static_cast<lv_coord_t>(
                    stripCoords.y1 + header_model::STRIP_HEIGHT + header_model::STRIP_CURSOR_OFFSET_Y
                )
            );
            drawStripRect(
                layer,
                area,
                lv_color_hex(PAGE_SELECTION_COLOR),
                LV_OPA_COVER,
                1
            );
        }

        if (visual.destinationPreview) {
            const auto area = markerArea(
                visual.segmentArea,
                static_cast<lv_coord_t>(
                    stripCoords.y1 + header_model::STRIP_HEIGHT +
                    header_model::STRIP_CURSOR_OFFSET_Y +
                    header_model::STRIP_CURSOR_HEIGHT +
                    header_model::MARKER_GAP
                )
            );
            const uint32_t color = visual.destinationBlocked
                ? PAGE_DUPLICATE_BLOCKED_COLOR
                : (visual.destinationOverwrite
                    ? PAGE_DUPLICATE_OVERWRITE_COLOR
                    : PAGE_DUPLICATE_PREVIEW_COLOR);
            drawStripRect(layer, area, lv_color_hex(color), LV_OPA_COVER, 1);
        }
    }
}

FLASHMEM void SequencerHeaderBar::renderStrip(const SequencerHeaderBarProps& props) {
    if (!strip_row_) return;

    const auto stripState = header_model::buildStripState(props);

    bool widthChanged = false;
    lv_coord_t stripWidth = lv_obj_get_content_width(strip_row_);
    if (stripWidth <= 0) {
        lv_obj_update_layout(container_);
        stripWidth = lv_obj_get_content_width(strip_row_);
    }
    if (stripWidth != strip_cached_width_) {
        strip_cached_width_ = stripWidth;
        widthChanged = true;
        header_model::buildStripSegmentGeometry(stripWidth, strip_segment_geometry_);
    }

    const bool stripStateChanged = !strip_cache_initialized_ ||
                                   strip_cached_length_ != stripState.length ||
                                   strip_cached_active_page_ != props.activePage ||
                                   strip_cached_viewed_page_ != props.viewedPage ||
                                   strip_cached_preview_track_ != props.previewTrack ||
                                   strip_cached_enabled_mask_ != props.enabledMask ||
                                   strip_cached_page_source_marker_mask_ !=
                                       props.pageSourceMarkerMask ||
                                   strip_cached_page_destination_preview_mask_ !=
                                       props.pageDestinationPreviewMask ||
                                   strip_cached_page_destination_overwrite_mask_ !=
                                       props.pageDestinationOverwriteMask ||
                                   strip_cached_page_destination_blocked_mask_ !=
                                       props.pageDestinationBlockedMask;

    if (!stripStateChanged && !widthChanged) {
        return;
    }

    strip_cache_initialized_ = true;
    strip_cached_length_ = stripState.length;
    strip_cached_active_page_ = props.activePage;
    strip_cached_viewed_page_ = props.viewedPage;
    strip_cached_preview_track_ = props.previewTrack;
    strip_cached_enabled_mask_ = props.enabledMask;
    strip_cached_page_source_marker_mask_ = props.pageSourceMarkerMask;
    strip_cached_page_destination_preview_mask_ = props.pageDestinationPreviewMask;
    strip_cached_page_destination_overwrite_mask_ = props.pageDestinationOverwriteMask;
    strip_cached_page_destination_blocked_mask_ =
        props.pageDestinationBlockedMask;
    strip_draw_props_ = props;
    lv_obj_invalidate(strip_row_);

    const auto viewCursor = header_model::buildViewCursorLayout(props, strip_segment_geometry_);
    if (!viewCursor.visible || !view_cursor_) {
        if (view_cursor_ && view_cursor_visible_cache_) {
            lv_obj_add_flag(view_cursor_, LV_OBJ_FLAG_HIDDEN);
            view_cursor_visible_cache_ = false;
        }
    } else {
        if (!view_cursor_visible_cache_) {
            lv_obj_clear_flag(view_cursor_, LV_OBJ_FLAG_HIDDEN);
            view_cursor_visible_cache_ = true;
        }
        if (view_cursor_x_cache_ != viewCursor.x || view_cursor_y_cache_ != viewCursor.y) {
            lv_obj_set_pos(view_cursor_, viewCursor.x, viewCursor.y);
            view_cursor_x_cache_ = viewCursor.x;
            view_cursor_y_cache_ = viewCursor.y;
        }
        if (view_cursor_width_cache_ != viewCursor.width || view_cursor_height_cache_ != viewCursor.height) {
            lv_obj_set_size(view_cursor_, viewCursor.width, viewCursor.height);
            view_cursor_width_cache_ = viewCursor.width;
            view_cursor_height_cache_ = viewCursor.height;
        }
    }

    const auto stripCursor = header_model::buildStripCursorLayout(props, strip_segment_geometry_);
    if (!stripCursor.visible || !strip_cursor_) {
        if (strip_cursor_visible_cache_) {
            lv_obj_add_flag(strip_cursor_, LV_OBJ_FLAG_HIDDEN);
            strip_cursor_visible_cache_ = false;
        }
        return;
    }

    if (!strip_cursor_visible_cache_) {
        lv_obj_clear_flag(strip_cursor_, LV_OBJ_FLAG_HIDDEN);
        strip_cursor_visible_cache_ = true;
    }
    if (strip_cursor_x_cache_ != stripCursor.x) {
        lv_obj_set_x(strip_cursor_, stripCursor.x);
        strip_cursor_x_cache_ = stripCursor.x;
    }
    if (strip_cursor_width_cache_ != stripCursor.width) {
        lv_obj_set_size(strip_cursor_, stripCursor.width, stripCursor.height);
        strip_cursor_width_cache_ = stripCursor.width;
    }
    if (strip_cursor_y_cache_ != stripCursor.y) {
        lv_obj_set_y(strip_cursor_, stripCursor.y);
        strip_cursor_y_cache_ = stripCursor.y;
    }
    if (strip_cursor_opa_cache_ != stripCursor.opa) {
        lv_obj_set_style_bg_opa(strip_cursor_, stripCursor.opa, 0);
        strip_cursor_opa_cache_ = stripCursor.opa;
    }
}

}  // namespace core::ui

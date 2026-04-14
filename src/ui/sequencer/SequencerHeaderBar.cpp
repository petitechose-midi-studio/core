#include "SequencerHeaderBar.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/common/AddSlotIcon.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace style = oc::ui::lvgl::style;
namespace add_slot_icon_ns = core::ui::add_slot_icon;

namespace {

constexpr uint32_t COLOR_DIM_TEXT = theme::color::TEXT_PRIMARY;
constexpr lv_coord_t HORIZONTAL_INSET = oc::ui::lvgl::base_theme::layout::MARGIN_SM + 4;
constexpr lv_coord_t ACCENT_WIDTH = 4;
constexpr lv_coord_t LABEL_MAX_WIDTH = 46;
constexpr lv_coord_t BADGE_WIDTH = 40;
constexpr lv_coord_t TITLE_SEPARATOR_WIDTH = 5;
constexpr lv_coord_t STRIP_GAP = 1;
constexpr lv_coord_t STRIP_RADIUS = 2;
constexpr lv_opa_t TRACK_BG_OPA_IDLE = LV_OPA_10;
constexpr lv_opa_t TRACK_BG_OPA_SELECTING = static_cast<lv_opa_t>(31);
constexpr lv_opa_t PAGE_ITEM_BASE_OPA_ENABLED = static_cast<lv_opa_t>(18);
constexpr lv_opa_t PAGE_ITEM_BASE_OPA_DISABLED = static_cast<lv_opa_t>(8);
constexpr lv_opa_t PAGE_ITEM_ACTIVE_BONUS = static_cast<lv_opa_t>(48);
constexpr lv_coord_t VIEW_CURSOR_WIDTH = 2;
constexpr lv_opa_t VIEW_CURSOR_OPA = LV_OPA_80;
constexpr lv_coord_t STRIP_CURSOR_HEIGHT = 2;
constexpr lv_coord_t STRIP_CURSOR_OFFSET_Y = 2;
constexpr lv_coord_t STRIP_ROW_HEIGHT = 14 + STRIP_CURSOR_OFFSET_Y + STRIP_CURSOR_HEIGHT;
constexpr uint32_t PAGE_CURSOR_COLOR = theme::color::MACRO_1;
constexpr lv_coord_t PAGE_OUTLINE_WIDTH = 1;
constexpr lv_opa_t PAGE_OUTLINE_OPA_SELECTED = LV_OPA_70;

bool isTrackEnabled(uint16_t enabledMask, uint8_t index) {
    return (enabledMask & static_cast<uint16_t>(1U << index)) != 0;
}

constexpr uint32_t trackColor(uint8_t index) {
    return theme::color::trackColor(index);
}

constexpr uint32_t trackInactiveColor() {
    return theme::color::INACTIVE;
}

lv_color_t pageStripBaseColor(const SequencerHeaderBarProps& props) {
    return lv_color_hex(
        isTrackEnabled(props.enabledMask, props.previewTrack)
            ? trackColor(props.previewTrack)
            : trackInactiveColor()
    );
}

lv_opa_t pageItemOpa(bool enabled, bool isActive) {
    uint16_t opa = enabled ? PAGE_ITEM_BASE_OPA_ENABLED : PAGE_ITEM_BASE_OPA_DISABLED;
    if (isActive) opa += PAGE_ITEM_ACTIVE_BONUS;
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

lv_area_t makeArea(lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height) {
    return lv_area_t{
        .x1 = x,
        .y1 = y,
        .x2 = static_cast<lv_coord_t>(x + width - 1),
        .y2 = static_cast<lv_coord_t>(y + height - 1),
    };
}

void drawStripRect(lv_layer_t* layer,
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

template <size_t N, typename SegmentGeometry>
void buildStripSegmentGeometry(lv_coord_t stripWidth, std::array<SegmentGeometry, N>& geometry) {
    if (stripWidth <= 0) {
        for (auto& segment : geometry) segment = {};
        return;
    }

    const lv_coord_t totalGap = static_cast<lv_coord_t>((N - 1) * STRIP_GAP);
    const lv_coord_t availableWidth = std::max<lv_coord_t>(0, stripWidth - totalGap);
    const lv_coord_t baseWidth = availableWidth / static_cast<lv_coord_t>(N);
    lv_coord_t remainder =
        static_cast<lv_coord_t>(availableWidth - (baseWidth * static_cast<lv_coord_t>(N)));

    lv_coord_t x = 0;
    for (auto& segment : geometry) {
        const lv_coord_t width = static_cast<lv_coord_t>(baseWidth + (remainder > 0 ? 1 : 0));
        if (remainder > 0) --remainder;
        segment.x = x;
        segment.width = width;
        x = static_cast<lv_coord_t>(x + width + STRIP_GAP);
    }
}

}  // namespace

SequencerHeaderBar::SequencerHeaderBar(lv_obj_t* parent) {
    createUI(parent);
}

SequencerHeaderBar::~SequencerHeaderBar() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        accent_ = nullptr;
        label_ = nullptr;
        badge_ = nullptr;
        spacer_ = nullptr;
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

    spacer_ = lv_obj_create(container_);
    style::apply(spacer_).size(0, 1).transparent().noBorder().noScroll().pad(0);
    lv_obj_set_width(spacer_, TITLE_SEPARATOR_WIDTH);

    strip_row_ = lv_obj_create(container_);
    style::apply(strip_row_)
        .size(0, STRIP_ROW_HEIGHT)
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
    lv_obj_set_style_border_width(view_cursor_, VIEW_CURSOR_WIDTH, 0);
    lv_obj_set_style_border_color(view_cursor_, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
    lv_obj_set_style_border_opa(view_cursor_, VIEW_CURSOR_OPA, 0);
    lv_obj_set_style_radius(view_cursor_, STRIP_RADIUS, 0);
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

void SequencerHeaderBar::render(const SequencerHeaderBarProps& props) {
    renderTopRowOnly(props);
    renderStripOnly(props);
}

void SequencerHeaderBar::renderTopRowOnly(const SequencerHeaderBarProps& props) {
    renderTopRow(props);
}

void SequencerHeaderBar::renderStripOnly(const SequencerHeaderBarProps& props) {
    renderStrip(props);
}

void SequencerHeaderBar::renderTopRow(const SequencerHeaderBarProps& props) {
    if (!container_ || !accent_ || !label_ || !badge_) return;

    setLabelTextIfChanged(label_, left_text_cache_, props.leftText);
    setLabelTextIfChanged(badge_, badge_text_cache_, props.badgeText.data());
    const uint32_t accentColor =
        isTrackEnabled(props.enabledMask, props.previewTrack)
            ? trackColor(props.previewTrack)
            : trackInactiveColor();
    const bool showBadge = props.badgeText[0] != '\0';
    const bool selectionMode = props.selectingPage || props.selectingTrack;
    const uint32_t badgeBgColor = selectionMode ? theme::color::TEXT_PRIMARY : accentColor;
    const lv_opa_t badgeBgOpa =
        showBadge ? (selectionMode ? LV_OPA_20 : static_cast<lv_opa_t>(24)) : LV_OPA_TRANSP;
    const lv_coord_t badgeBorderWidth = (showBadge && selectionMode) ? 1 : 0;
    const uint32_t badgeBorderColor = theme::color::TEXT_PRIMARY;
    const lv_opa_t badgeBorderOpa = (showBadge && selectionMode) ? LV_OPA_80 : LV_OPA_TRANSP;
    const lv_opa_t badgeTextOpa = showBadge ? LV_OPA_80 : LV_OPA_TRANSP;

    if (!badge_cache_initialized_ || badge_bg_color_cache_ != badgeBgColor) {
        lv_obj_set_style_bg_color(badge_, lv_color_hex(badgeBgColor), 0);
        badge_bg_color_cache_ = badgeBgColor;
    }
    if (!badge_cache_initialized_ || badge_bg_opa_cache_ != badgeBgOpa) {
        lv_obj_set_style_bg_opa(badge_, badgeBgOpa, 0);
        badge_bg_opa_cache_ = badgeBgOpa;
    }
    if (!badge_cache_initialized_ || badge_border_width_cache_ != badgeBorderWidth) {
        lv_obj_set_style_border_width(badge_, badgeBorderWidth, 0);
        badge_border_width_cache_ = badgeBorderWidth;
    }
    if (!badge_cache_initialized_ || badge_border_color_cache_ != badgeBorderColor) {
        lv_obj_set_style_border_color(badge_, lv_color_hex(badgeBorderColor), 0);
        badge_border_color_cache_ = badgeBorderColor;
    }
    if (!badge_cache_initialized_ || badge_border_opa_cache_ != badgeBorderOpa) {
        lv_obj_set_style_border_opa(badge_, badgeBorderOpa, 0);
        badge_border_opa_cache_ = badgeBorderOpa;
    }
    if (!badge_cache_initialized_ || badge_text_opa_cache_ != badgeTextOpa) {
        lv_obj_set_style_text_opa(badge_, badgeTextOpa, 0);
        badge_text_opa_cache_ = badgeTextOpa;
    }
    const lv_opa_t accentOpa = props.selectingTrack ? LV_OPA_COVER : LV_OPA_80;
    const uint32_t backgroundColor = accentColor;
    const lv_opa_t backgroundOpa = props.selectingTrack ? TRACK_BG_OPA_SELECTING : TRACK_BG_OPA_IDLE;

    if (!surface_cache_initialized_ || accent_cache_color_ != accentColor) {
        lv_obj_set_style_bg_color(accent_, lv_color_hex(accentColor), 0);
        accent_cache_color_ = accentColor;
    }
    if (!surface_cache_initialized_ || accent_cache_opa_ != accentOpa) {
        lv_obj_set_style_bg_opa(accent_, accentOpa, 0);
        accent_cache_opa_ = accentOpa;
    }
    if (!surface_cache_initialized_ || background_cache_color_ != backgroundColor) {
        lv_obj_set_style_bg_color(container_, lv_color_hex(backgroundColor), 0);
        background_cache_color_ = backgroundColor;
    }
    if (!surface_cache_initialized_ || background_cache_opa_ != backgroundOpa) {
        lv_obj_set_style_bg_opa(container_, backgroundOpa, 0);
        background_cache_opa_ = backgroundOpa;
    }

    surface_cache_initialized_ = true;
    badge_cache_initialized_ = true;
}

void SequencerHeaderBar::onStripDrawEvent(lv_event_t* event) {
    auto* self = static_cast<SequencerHeaderBar*>(lv_event_get_user_data(event));
    if (!self || !self->strip_row_) return;

    lv_layer_t* layer = lv_event_get_layer(event);
    if (!layer) return;

    lv_area_t stripCoords{};
    lv_obj_get_coords(self->strip_row_, &stripCoords);

    const auto& props = self->strip_draw_props_;
    const uint8_t len = self->strip_cached_length_;
    const uint8_t pageCount = static_cast<uint8_t>(
        std::min<uint16_t>((len + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE, PAGE_COUNT)
    );
    const bool playing = (self->strip_cached_playhead_ >= 0) && (self->strip_cached_playhead_ < len);
    const lv_color_t baseColor = pageStripBaseColor(props);
    const lv_color_t disabledColor = lv_color_hex(theme::color::KNOB_BACKGROUND);

    for (uint8_t p = 0; p < PAGE_COUNT; ++p) {
        const auto& geometry = self->strip_segment_geometry_[p];
        if (geometry.width <= 0) continue;

        const lv_area_t segmentArea = makeArea(
            static_cast<lv_coord_t>(stripCoords.x1 + geometry.x),
            stripCoords.y1,
            geometry.width,
            STRIP_HEIGHT
        );

        const uint8_t pageStart = static_cast<uint8_t>(p * STEPS_PER_PAGE);
        const int16_t remaining = static_cast<int16_t>(len) - static_cast<int16_t>(pageStart);
        const uint8_t validSteps =
            (remaining <= 0)
                ? 0
                : static_cast<uint8_t>(
                      std::min<int16_t>(remaining, static_cast<int16_t>(STEPS_PER_PAGE))
                  );

        const bool isViewed = (p == props.viewedPage);
        const bool isSelected = (props.pageSelectedMask & static_cast<uint16_t>(1U << p)) != 0;
        const bool isAddSlot = props.previewPageAddSlot && props.addPageIndex == p;
        const bool isActivePage = (p == props.activePage);
        const lv_opa_t containerBgOpa = pageItemOpa(p < pageCount, isActivePage);

        drawStripRect(layer,
                      segmentArea,
                      disabledColor,
                      containerBgOpa,
                      STRIP_RADIUS,
                      isSelected ? PAGE_OUTLINE_WIDTH : 0,
                      lv_color_hex(theme::color::TEXT_PRIMARY),
                      isSelected ? PAGE_OUTLINE_OPA_SELECTED : LV_OPA_TRANSP);

        if (validSteps > 0) {
            const lv_coord_t validWidth = static_cast<lv_coord_t>(
                (static_cast<int32_t>(geometry.width) * validSteps) / STEPS_PER_PAGE
            );
            if (validWidth > 0) {
                const lv_color_t validColor = isViewed
                    ? lv_color_lighten(baseColor, LV_OPA_20)
                    : lv_color_darken(baseColor, LV_OPA_70);
                drawStripRect(
                    layer,
                    makeArea(segmentArea.x1, segmentArea.y1, validWidth, STRIP_HEIGHT),
                    validColor,
                    LV_OPA_COVER,
                    STRIP_RADIUS
                );
            }

            if (playing &&
                self->strip_cached_playhead_ >= pageStart &&
                self->strip_cached_playhead_ < static_cast<int16_t>(pageStart + validSteps)) {
                const lv_color_t progressColor = isViewed
                    ? lv_color_lighten(baseColor, LV_OPA_10)
                    : lv_color_lighten(baseColor, LV_OPA_40);
                drawStripRect(
                    layer,
                    makeArea(segmentArea.x1, segmentArea.y1, validWidth, STRIP_HEIGHT),
                    progressColor,
                    LV_OPA_COVER,
                    STRIP_RADIUS
                );

                const uint8_t playheadInPage =
                    static_cast<uint8_t>(self->strip_cached_playhead_ - pageStart);
                const float stepCenter =
                    (static_cast<float>(playheadInPage) + 0.5f) / static_cast<float>(STEPS_PER_PAGE);
                lv_coord_t markerX = static_cast<lv_coord_t>(
                    stepCenter * static_cast<float>(geometry.width) - (MARKER_WIDTH / 2.0f)
                );
                const lv_coord_t maxX = std::max<lv_coord_t>(0, validWidth - MARKER_WIDTH);
                markerX = std::clamp<lv_coord_t>(markerX, 0, maxX);

                drawStripRect(
                    layer,
                    makeArea(
                        static_cast<lv_coord_t>(segmentArea.x1 + markerX),
                        segmentArea.y1,
                        MARKER_WIDTH,
                        STRIP_HEIGHT
                    ),
                    lv_color_hex(theme::color::TEXT_PRIMARY),
                    LV_OPA_COVER,
                    0
                );
            }
        }

        if (isAddSlot) {
            add_slot_icon_ns::drawCentered(
                layer,
                segmentArea,
                theme::color::TEXT_PRIMARY,
                LV_OPA_COVER
            );
        }
    }
}

void SequencerHeaderBar::renderStrip(const SequencerHeaderBarProps& props) {
    if (!strip_row_) return;

    const uint8_t len =
        std::min<uint8_t>(props.length, static_cast<uint8_t>(PAGE_COUNT * STEPS_PER_PAGE));
    const bool playing = (props.playheadStep >= 0) && (props.playheadStep < len);
    const int16_t playhead = playing ? props.playheadStep : -1;

    bool widthChanged = false;
    lv_coord_t stripWidth = lv_obj_get_content_width(strip_row_);
    if (stripWidth <= 0) {
        lv_obj_update_layout(container_);
        stripWidth = lv_obj_get_content_width(strip_row_);
    }
    if (stripWidth != strip_cached_width_) {
        strip_cached_width_ = stripWidth;
        widthChanged = true;
        buildStripSegmentGeometry(stripWidth, strip_segment_geometry_);
    }

    const bool stripStateChanged = !strip_cache_initialized_ ||
                                   strip_cached_length_ != len ||
                                   strip_cached_active_page_ != props.activePage ||
                                   strip_cached_viewed_page_ != props.viewedPage ||
                                   strip_cached_preview_track_ != props.previewTrack ||
                                   strip_cached_add_page_index_ != props.addPageIndex ||
                                   strip_cached_enabled_mask_ != props.enabledMask ||
                                   strip_cached_page_selected_mask_ != props.pageSelectedMask ||
                                   strip_cached_preview_page_add_slot_ != props.previewPageAddSlot ||
                                   strip_cached_playhead_ != playhead;

    if (!stripStateChanged && !widthChanged) {
        return;
    }

    strip_cache_initialized_ = true;
    strip_cached_length_ = len;
    strip_cached_active_page_ = props.activePage;
    strip_cached_viewed_page_ = props.viewedPage;
    strip_cached_preview_track_ = props.previewTrack;
    strip_cached_add_page_index_ = props.addPageIndex;
    strip_cached_enabled_mask_ = props.enabledMask;
    strip_cached_page_selected_mask_ = props.pageSelectedMask;
    strip_cached_preview_page_add_slot_ = props.previewPageAddSlot;
    strip_cached_playhead_ = playhead;
    strip_draw_props_ = props;
    lv_obj_invalidate(strip_row_);

    const bool showViewCursor = props.viewedPage < PAGE_COUNT && view_cursor_;
    if (!showViewCursor) {
        if (view_cursor_ && view_cursor_visible_cache_) {
            lv_obj_add_flag(view_cursor_, LV_OBJ_FLAG_HIDDEN);
            view_cursor_visible_cache_ = false;
        }
    } else {
        const auto& geometry = strip_segment_geometry_[props.viewedPage];
        if (geometry.width <= 0) {
            if (view_cursor_visible_cache_) {
                lv_obj_add_flag(view_cursor_, LV_OBJ_FLAG_HIDDEN);
                view_cursor_visible_cache_ = false;
            }
        } else {
            const lv_coord_t itemX = geometry.x;
            const lv_coord_t itemY = 0;
            const lv_coord_t itemW = geometry.width;
            const lv_coord_t itemH = STRIP_HEIGHT;
            if (!view_cursor_visible_cache_) {
                lv_obj_clear_flag(view_cursor_, LV_OBJ_FLAG_HIDDEN);
                view_cursor_visible_cache_ = true;
            }
            if (view_cursor_x_cache_ != itemX || view_cursor_y_cache_ != itemY) {
                lv_obj_set_pos(view_cursor_, itemX, itemY);
                view_cursor_x_cache_ = itemX;
                view_cursor_y_cache_ = itemY;
            }
            if (view_cursor_width_cache_ != itemW || view_cursor_height_cache_ != itemH) {
                lv_obj_set_size(view_cursor_, itemW, itemH);
                view_cursor_width_cache_ = itemW;
                view_cursor_height_cache_ = itemH;
            }
        }
    }

    const bool showCursor = props.activePage < PAGE_COUNT && strip_cursor_;
    if (!showCursor) {
        if (strip_cursor_visible_cache_) {
            lv_obj_add_flag(strip_cursor_, LV_OBJ_FLAG_HIDDEN);
            strip_cursor_visible_cache_ = false;
        }
        return;
    }

    const auto& activeGeometry = strip_segment_geometry_[props.activePage];
    if (activeGeometry.width <= 0) return;

    const lv_coord_t targetX = static_cast<lv_coord_t>(activeGeometry.x + 1);
    const lv_coord_t targetW = std::max<lv_coord_t>(1, activeGeometry.width - 2);
    const lv_coord_t targetY = static_cast<lv_coord_t>(STRIP_HEIGHT + STRIP_CURSOR_OFFSET_Y);
    const lv_opa_t targetOpa = props.selectingPage ? LV_OPA_COVER : static_cast<lv_opa_t>(200);

    if (!strip_cursor_visible_cache_) {
        lv_obj_clear_flag(strip_cursor_, LV_OBJ_FLAG_HIDDEN);
        strip_cursor_visible_cache_ = true;
    }
    if (strip_cursor_x_cache_ != targetX) {
        lv_obj_set_x(strip_cursor_, targetX);
        strip_cursor_x_cache_ = targetX;
    }
    if (strip_cursor_width_cache_ != targetW) {
        lv_obj_set_size(strip_cursor_, targetW, STRIP_CURSOR_HEIGHT);
        strip_cursor_width_cache_ = targetW;
    }
    if (strip_cursor_y_cache_ != targetY) {
        lv_obj_set_y(strip_cursor_, targetY);
        strip_cursor_y_cache_ = targetY;
    }
    if (strip_cursor_opa_cache_ != targetOpa) {
        lv_obj_set_style_bg_opa(strip_cursor_, targetOpa, 0);
        strip_cursor_opa_cache_ = targetOpa;
    }
}

}  // namespace core::ui

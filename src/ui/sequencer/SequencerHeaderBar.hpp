#pragma once

/**
 * @file SequencerHeaderBar.hpp
 * @brief Sequencer header: text row + playhead progress strip
 */

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/StatusBarState.hpp"

namespace core::ui {

struct SequencerHeaderBarStripSegmentGeometry {
    lv_coord_t x = 0;
    lv_coord_t width = 0;
};

struct SequencerHeaderBarProps {
    static constexpr uint8_t TRACK_COUNT = core::state::StatusBarState::TRACK_COUNT;

    uint8_t length = 0;
    uint8_t activePage = 0;
    uint8_t viewedPage = 0;     // 0..15, may point to a future paste target page
    int16_t playheadStep = -1;  // -1 when stopped
    uint8_t previewTrack = 0;
    uint8_t addPageIndex = core::state::sequencer::SequencerState::PAGE_COUNT;
    uint16_t enabledMask = 0x0001;
    bool selectingTrack = false;
    bool selectingPage = false;
    bool previewPageAddSlot = false;
    uint16_t pageSelectedMask = 0;
    const char* leftText = "";
    std::array<char, 12> badgeText{};
};

/**
 * @brief Stateless header widget rendered from Sequencer state
 *
 * Pattern: stateless + render(props), similar to plugin-bitwig DeviceStateBar.
 */
class SequencerHeaderBar : public oc::ui::lvgl::IWidget {
public:
    explicit SequencerHeaderBar(lv_obj_t* parent);
    ~SequencerHeaderBar() override;

    SequencerHeaderBar(const SequencerHeaderBar&) = delete;
    SequencerHeaderBar& operator=(const SequencerHeaderBar&) = delete;

    void render(const SequencerHeaderBarProps& props);
    void renderTopRowOnly(const SequencerHeaderBarProps& props);
    void renderStripOnly(const SequencerHeaderBarProps& props);

    lv_obj_t* getElement() const override { return container_; }

private:
    static constexpr uint8_t PAGE_COUNT = core::state::sequencer::SequencerState::PAGE_COUNT;
    static constexpr lv_coord_t HEADER_HEIGHT = 28;

    void createUI(lv_obj_t* parent);
    static void onStripDrawEvent(lv_event_t* event);
    void renderTopRow(const SequencerHeaderBarProps& props);
    void renderStrip(const SequencerHeaderBarProps& props);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* accent_ = nullptr;
    lv_obj_t* label_ = nullptr;
    lv_obj_t* badge_ = nullptr;
    lv_obj_t* strip_row_ = nullptr;
    lv_obj_t* view_cursor_ = nullptr;
    lv_obj_t* strip_cursor_ = nullptr;
    std::array<SequencerHeaderBarStripSegmentGeometry, PAGE_COUNT> strip_segment_geometry_{};

    std::array<char, 16> left_text_cache_{};
    std::array<char, 12> badge_text_cache_{};
    bool surface_cache_initialized_ = false;
    uint32_t accent_cache_color_ = 0;
    lv_opa_t accent_cache_opa_ = LV_OPA_TRANSP;
    uint32_t background_cache_color_ = 0;
    lv_opa_t background_cache_opa_ = LV_OPA_TRANSP;
    bool badge_cache_initialized_ = false;
    uint32_t badge_bg_color_cache_ = 0;
    lv_opa_t badge_bg_opa_cache_ = LV_OPA_TRANSP;
    lv_coord_t badge_border_width_cache_ = -1;
    uint32_t badge_border_color_cache_ = 0;
    lv_opa_t badge_border_opa_cache_ = LV_OPA_TRANSP;
    lv_opa_t badge_text_opa_cache_ = LV_OPA_TRANSP;

    bool strip_cache_initialized_ = false;
    SequencerHeaderBarProps strip_draw_props_{};
    uint8_t strip_cached_length_ = 0;
    uint8_t strip_cached_active_page_ = 0;
    uint8_t strip_cached_viewed_page_ = 0;
    uint8_t strip_cached_preview_track_ = 0;
    uint8_t strip_cached_add_page_index_ = PAGE_COUNT;
    uint16_t strip_cached_enabled_mask_ = 0;
    uint16_t strip_cached_page_selected_mask_ = 0;
    bool strip_cached_preview_page_add_slot_ = false;
    int16_t strip_cached_playhead_ = -2;
    lv_coord_t strip_cached_width_ = -1;
    bool strip_cursor_visible_cache_ = false;
    lv_coord_t strip_cursor_x_cache_ = -1;
    lv_coord_t strip_cursor_width_cache_ = -1;
    lv_coord_t strip_cursor_y_cache_ = -1;
    lv_opa_t strip_cursor_opa_cache_ = LV_OPA_TRANSP;
    bool view_cursor_visible_cache_ = false;
    lv_coord_t view_cursor_x_cache_ = -1;
    lv_coord_t view_cursor_y_cache_ = -1;
    lv_coord_t view_cursor_width_cache_ = -1;
    lv_coord_t view_cursor_height_cache_ = -1;
};

}  // namespace core::ui

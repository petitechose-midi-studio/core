#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include "ui/sequencer/SequencerHeaderBar.hpp"

namespace core::ui::sequencer::header_bar {

constexpr uint8_t PAGE_COUNT = core::state::sequencer::SequencerState::PAGE_COUNT;
constexpr uint8_t STEPS_PER_PAGE = core::state::sequencer::SequencerState::STEPS_PER_PAGE;
constexpr lv_coord_t STRIP_HEIGHT = 14;
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
constexpr lv_coord_t STRIP_ROW_HEIGHT = STRIP_HEIGHT + STRIP_CURSOR_OFFSET_Y + STRIP_CURSOR_HEIGHT;
constexpr lv_coord_t PAGE_OUTLINE_WIDTH = 1;
constexpr lv_opa_t PAGE_OUTLINE_OPA_SELECTED = LV_OPA_70;

struct TopRowVisualState {
    uint32_t accentColor = 0;
    lv_opa_t accentOpa = LV_OPA_TRANSP;
    uint32_t backgroundColor = 0;
    lv_opa_t backgroundOpa = LV_OPA_TRANSP;
    uint32_t badgeBgColor = 0;
    lv_opa_t badgeBgOpa = LV_OPA_TRANSP;
    lv_coord_t badgeBorderWidth = 0;
    uint32_t badgeBorderColor = 0;
    lv_opa_t badgeBorderOpa = LV_OPA_TRANSP;
    lv_opa_t badgeTextOpa = LV_OPA_TRANSP;
};

struct StripState {
    uint8_t length = 0;
    uint8_t pageCount = 0;
    lv_color_t baseColor = lv_color_black();
    lv_color_t disabledColor = lv_color_black();
};

struct StripSegmentVisual {
    bool visible = false;
    lv_area_t segmentArea{};
    lv_opa_t containerBgOpa = LV_OPA_TRANSP;
    bool selected = false;
    bool drawValidFill = false;
    lv_area_t validArea{};
    lv_color_t validColor = lv_color_black();
    bool drawAddSlot = false;
};

struct CursorLayout {
    bool visible = false;
    lv_coord_t x = 0;
    lv_coord_t y = 0;
    lv_coord_t width = 0;
    lv_coord_t height = 0;
    lv_opa_t opa = LV_OPA_TRANSP;
};

TopRowVisualState buildTopRowVisualState(const SequencerHeaderBarProps& props);
StripState buildStripState(const SequencerHeaderBarProps& props);
void buildStripSegmentGeometry(
    lv_coord_t stripWidth,
    std::array<SequencerHeaderBarStripSegmentGeometry, PAGE_COUNT>& geometry
);
StripSegmentVisual buildStripSegmentVisual(
    const SequencerHeaderBarProps& props,
    const StripState& stripState,
    const SequencerHeaderBarStripSegmentGeometry& geometry,
    const lv_area_t& stripCoords,
    uint8_t pageIndex
);
CursorLayout buildViewCursorLayout(
    const SequencerHeaderBarProps& props,
    const std::array<SequencerHeaderBarStripSegmentGeometry, PAGE_COUNT>& geometry
);
CursorLayout buildStripCursorLayout(
    const SequencerHeaderBarProps& props,
    const std::array<SequencerHeaderBarStripSegmentGeometry, PAGE_COUNT>& geometry
);

}  // namespace core::ui::sequencer::header_bar

#include "ui/sequencer/SequencerHeaderBarRenderModel.hpp"

#include <algorithm>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer::header_bar {

namespace theme = standalone::theme;

namespace {

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

lv_area_t makeArea(lv_coord_t x, lv_coord_t y, lv_coord_t width, lv_coord_t height) {
    return lv_area_t{
        .x1 = x,
        .y1 = y,
        .x2 = static_cast<lv_coord_t>(x + width - 1),
        .y2 = static_cast<lv_coord_t>(y + height - 1),
    };
}

}  // namespace

TopRowVisualState buildTopRowVisualState(const SequencerHeaderBarProps& props) {
    TopRowVisualState state{};
    const uint32_t accentColor =
        isTrackEnabled(props.enabledMask, props.previewTrack)
            ? trackColor(props.previewTrack)
            : trackInactiveColor();
    const bool showBadge = props.badgeText[0] != '\0';
    const bool selectionMode = props.selectingPage || props.selectingTrack;

    state.accentColor = accentColor;
    state.accentOpa = props.selectingTrack ? LV_OPA_COVER : LV_OPA_80;
    state.backgroundColor = accentColor;
    state.backgroundOpa = props.selectingTrack ? TRACK_BG_OPA_SELECTING : TRACK_BG_OPA_IDLE;
    state.badgeBgColor = selectionMode ? theme::color::TEXT_PRIMARY : accentColor;
    state.badgeBgOpa =
        showBadge ? (selectionMode ? LV_OPA_20 : static_cast<lv_opa_t>(24)) : LV_OPA_TRANSP;
    state.badgeBorderWidth = (showBadge && selectionMode) ? 1 : 0;
    state.badgeBorderColor = theme::color::TEXT_PRIMARY;
    state.badgeBorderOpa = (showBadge && selectionMode) ? LV_OPA_80 : LV_OPA_TRANSP;
    state.badgeTextOpa = showBadge ? LV_OPA_80 : LV_OPA_TRANSP;
    return state;
}

StripState buildStripState(const SequencerHeaderBarProps& props) {
    StripState state{};
    state.length = std::min<uint8_t>(props.length, static_cast<uint8_t>(PAGE_COUNT * STEPS_PER_PAGE));
    state.playing = (props.playheadStep >= 0) && (props.playheadStep < state.length);
    state.playhead = state.playing ? props.playheadStep : -1;
    state.pageCount = static_cast<uint8_t>(
        std::min<uint16_t>((state.length + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE, PAGE_COUNT)
    );
    state.baseColor = pageStripBaseColor(props);
    state.disabledColor = lv_color_hex(theme::color::KNOB_BACKGROUND);
    return state;
}

void buildStripSegmentGeometry(
    lv_coord_t stripWidth,
    std::array<SequencerHeaderBarStripSegmentGeometry, PAGE_COUNT>& geometry
) {
    if (stripWidth <= 0) {
        for (auto& segment : geometry) segment = {};
        return;
    }

    const lv_coord_t totalGap = static_cast<lv_coord_t>((PAGE_COUNT - 1) * STRIP_GAP);
    const lv_coord_t availableWidth = std::max<lv_coord_t>(0, stripWidth - totalGap);
    const lv_coord_t baseWidth = availableWidth / static_cast<lv_coord_t>(PAGE_COUNT);
    lv_coord_t remainder = static_cast<lv_coord_t>(
        availableWidth - (baseWidth * static_cast<lv_coord_t>(PAGE_COUNT))
    );

    lv_coord_t x = 0;
    for (auto& segment : geometry) {
        const lv_coord_t width = static_cast<lv_coord_t>(baseWidth + (remainder > 0 ? 1 : 0));
        if (remainder > 0) --remainder;
        segment.x = x;
        segment.width = width;
        x = static_cast<lv_coord_t>(x + width + STRIP_GAP);
    }
}

StripSegmentVisual buildStripSegmentVisual(const SequencerHeaderBarProps& props,
                                          const StripState& stripState,
                                          const SequencerHeaderBarStripSegmentGeometry& geometry,
                                          const lv_area_t& stripCoords,
                                          uint8_t pageIndex) {
    StripSegmentVisual visual{};
    if (geometry.width <= 0) {
        return visual;
    }

    visual.visible = true;
    visual.segmentArea = makeArea(
        static_cast<lv_coord_t>(stripCoords.x1 + geometry.x),
        stripCoords.y1,
        geometry.width,
        STRIP_HEIGHT
    );

    const uint8_t pageStart = static_cast<uint8_t>(pageIndex * STEPS_PER_PAGE);
    const int16_t remaining =
        static_cast<int16_t>(stripState.length) - static_cast<int16_t>(pageStart);
    const uint8_t validSteps =
        (remaining <= 0)
            ? 0
            : static_cast<uint8_t>(std::min<int16_t>(remaining, static_cast<int16_t>(STEPS_PER_PAGE)));

    const bool isViewed = (pageIndex == props.viewedPage);
    const bool isSelected = (props.pageSelectedMask & static_cast<uint16_t>(1U << pageIndex)) != 0;
    const bool isAddSlot = props.previewPageAddSlot && props.addPageIndex == pageIndex;
    const bool isActivePage = (pageIndex == props.activePage);

    visual.containerBgOpa = pageItemOpa(pageIndex < stripState.pageCount, isActivePage);
    visual.selected = isSelected;
    visual.drawAddSlot = isAddSlot;

    if (validSteps == 0) {
        return visual;
    }

    const lv_coord_t validWidth = static_cast<lv_coord_t>(
        (static_cast<int32_t>(geometry.width) * validSteps) / STEPS_PER_PAGE
    );
    if (validWidth <= 0) {
        return visual;
    }

    visual.drawValidFill = true;
    visual.validArea = makeArea(visual.segmentArea.x1, visual.segmentArea.y1, validWidth, STRIP_HEIGHT);
    visual.validColor = isViewed
        ? lv_color_lighten(stripState.baseColor, LV_OPA_20)
        : lv_color_darken(stripState.baseColor, LV_OPA_70);

    if (stripState.playing &&
        stripState.playhead >= pageStart &&
        stripState.playhead < static_cast<int16_t>(pageStart + validSteps)) {
        visual.drawProgressFill = true;
        visual.progressArea = visual.validArea;
        visual.progressColor = isViewed
            ? lv_color_lighten(stripState.baseColor, LV_OPA_10)
            : lv_color_lighten(stripState.baseColor, LV_OPA_40);

        const uint8_t playheadInPage = static_cast<uint8_t>(stripState.playhead - pageStart);
        const float stepCenter =
            (static_cast<float>(playheadInPage) + 0.5f) / static_cast<float>(STEPS_PER_PAGE);
        lv_coord_t markerX = static_cast<lv_coord_t>(
            stepCenter * static_cast<float>(geometry.width) - (MARKER_WIDTH / 2.0f)
        );
        const lv_coord_t maxX = std::max<lv_coord_t>(0, validWidth - MARKER_WIDTH);
        markerX = std::clamp<lv_coord_t>(markerX, 0, maxX);

        visual.drawMarker = true;
        visual.markerArea = makeArea(
            static_cast<lv_coord_t>(visual.segmentArea.x1 + markerX),
            visual.segmentArea.y1,
            MARKER_WIDTH,
            STRIP_HEIGHT
        );
    }

    return visual;
}

CursorLayout buildViewCursorLayout(
    const SequencerHeaderBarProps& props,
    const std::array<SequencerHeaderBarStripSegmentGeometry, PAGE_COUNT>& geometry
) {
    CursorLayout layout{};
    if (props.viewedPage >= PAGE_COUNT) {
        return layout;
    }

    const auto& segment = geometry[props.viewedPage];
    if (segment.width <= 0) {
        return layout;
    }

    layout.visible = true;
    layout.x = segment.x;
    layout.y = 0;
    layout.width = segment.width;
    layout.height = STRIP_HEIGHT;
    layout.opa = VIEW_CURSOR_OPA;
    return layout;
}

CursorLayout buildStripCursorLayout(
    const SequencerHeaderBarProps& props,
    const std::array<SequencerHeaderBarStripSegmentGeometry, PAGE_COUNT>& geometry
) {
    CursorLayout layout{};
    if (props.activePage >= PAGE_COUNT) {
        return layout;
    }

    const auto& segment = geometry[props.activePage];
    if (segment.width <= 0) {
        return layout;
    }

    layout.visible = true;
    layout.x = static_cast<lv_coord_t>(segment.x + 1);
    layout.y = static_cast<lv_coord_t>(STRIP_HEIGHT + STRIP_CURSOR_OFFSET_Y);
    layout.width = std::max<lv_coord_t>(1, segment.width - 2);
    layout.height = STRIP_CURSOR_HEIGHT;
    layout.opa = props.selectingPage ? LV_OPA_COVER : static_cast<lv_opa_t>(200);
    return layout;
}

}  // namespace core::ui::sequencer::header_bar

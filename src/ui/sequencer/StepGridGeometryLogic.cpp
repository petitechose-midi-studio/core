#include "ui/sequencer/StepGridGeometryLogic.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "ui/sequencer/StepGridRenderLogic.hpp"

namespace core::ui::sequencer::grid {

namespace {

constexpr lv_coord_t STEP_NOTE_LABEL_PAD_X = 4;
constexpr lv_coord_t STEP_NOTE_LABEL_PAD_BOTTOM = STEP_BOTTOM_RESERVED_HEIGHT + 4;
constexpr lv_coord_t STEP_INLINE_ICON_GAP = 3;
constexpr lv_coord_t STEP_RAIL_MIN_WIDTH = 6;
constexpr lv_coord_t STEP_BUTTON_MIN_HEIGHT = 56;
constexpr lv_coord_t STEP_GUIDE_HEIGHT = 8;
constexpr lv_coord_t STEP_GUIDE_BOTTOM = STEP_BOTTOM_RESERVED_HEIGHT + 5;
constexpr std::array<float, 3> STEP_GUIDE_POSITIONS = {
    0.0f,
    1.0f / 3.0f,
    0.5f,
};

}  // namespace

lv_coord_t measureRailWidth(lv_coord_t contentWidth) {
    return std::max<lv_coord_t>(STEP_RAIL_MIN_WIDTH, contentWidth);
}

lv_coord_t measureButtonHeight(lv_coord_t contentHeight) {
    return std::max<lv_coord_t>(STEP_BUTTON_MIN_HEIGHT, contentHeight);
}

StepGuideLayout buildGuideLayout(uint8_t guideIndex, lv_coord_t railWidth, lv_coord_t buttonHeight) {
    StepGuideLayout layout;
    layout.height = STEP_GUIDE_HEIGHT;

    const float position =
        (guideIndex < STEP_GUIDE_POSITIONS.size()) ? STEP_GUIDE_POSITIONS[guideIndex] : 0.0f;
    layout.x = static_cast<lv_coord_t>(
        std::round(position * static_cast<float>(std::max<lv_coord_t>(0, railWidth - 1)))
    );
    layout.y = static_cast<lv_coord_t>(
        std::max<lv_coord_t>(0, buttonHeight - STEP_GUIDE_BOTTOM - STEP_GUIDE_HEIGHT)
    );
    return layout;
}

StepTileGeometry buildTileGeometry(const lv_area_t& buttonArea,
                                   const lv_area_t& noteLayerArea,
                                   lv_coord_t railWidth,
                                   lv_coord_t buttonHeight) {
    return {
        .railWidth = railWidth,
        .buttonHeight = buttonHeight,
        .noteBaseX = static_cast<lv_coord_t>(buttonArea.x1 - noteLayerArea.x1),
        .noteBaseY = static_cast<lv_coord_t>(buttonArea.y1 - noteLayerArea.y1),
        .noteLabelBaselineY = static_cast<lv_coord_t>(
            buttonArea.y2 - noteLayerArea.y1 - STEP_NOTE_LABEL_PAD_BOTTOM
        ),
    };
}

InlineLabelLayout buildInlineLabelLayout(lv_coord_t noteBaseX,
                                         lv_coord_t noteLabelBaselineY,
                                         lv_coord_t noteLabelHeight,
                                         bool showInlineIcon,
                                         lv_coord_t inlineIconWidth,
                                         lv_coord_t inlineIconHeight) {
    InlineLabelLayout layout;
    layout.labelX = static_cast<lv_coord_t>(noteBaseX + STEP_NOTE_LABEL_PAD_X);
    layout.labelY = static_cast<lv_coord_t>(noteLabelBaselineY - noteLabelHeight);

    if (!showInlineIcon) {
        return layout;
    }

    layout.iconX = layout.labelX;
    layout.iconY = static_cast<lv_coord_t>(noteLabelBaselineY - inlineIconHeight);
    layout.labelX = static_cast<lv_coord_t>(layout.iconX + inlineIconWidth + STEP_INLINE_ICON_GAP);
    return layout;
}

}  // namespace core::ui::sequencer::grid

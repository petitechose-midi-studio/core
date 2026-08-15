#include "ui/sequencer/DrumOverviewSurface.hpp"


#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <ms/ui/widget/TextOverflow.hpp>
#include <oc/ui/lvgl/StaticSurfaceInvalidation.hpp>

#include "state/sequencer/DrumPatternState.hpp"
#include "state/sequencer/SequencerUiState.hpp"
#include "ui/sequencer/DrumLaneVisuals.hpp"
#include "ui/sequencer/DrumHitVisualSpec.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer {

namespace theme = standalone::theme;

namespace {

constexpr uint32_t DRUM_TIMING = standalone::theme::color::STEP_DIVISION;
constexpr lv_coord_t DRUM_LABEL_WIDTH = 58;
constexpr lv_opa_t IDLE_LANE_ICON_OPA = LV_OPA_70;
constexpr lv_opa_t IDLE_LANE_TEXT_OPA = LV_OPA_60;
constexpr lv_opa_t IDLE_CELL_OPA = LV_OPA_80;
constexpr lv_opa_t UNAVAILABLE_CELL_OPA = LV_OPA_20;
constexpr lv_opa_t IDLE_BEAT_OPA = LV_OPA_30;
constexpr lv_opa_t SELECTED_BEAT_OPA = LV_OPA_50;
constexpr lv_opa_t IDLE_PAGE_GROUP_OPA = LV_OPA_60;
constexpr lv_opa_t SELECTED_PAGE_GROUP_OPA = LV_OPA_80;
constexpr lv_opa_t IDLE_LOOP_OPA = LV_OPA_60;
constexpr uint8_t PAGE_GROUP_SPLIT_COLUMN =
    core::state::sequencer::DrumSequencerState::STEPS_PER_PAGE / 2U;

FLASHMEM lv_coord_t drumLaneHeight(const lv_area_t& surface) {
    const lv_coord_t height = static_cast<lv_coord_t>(
        surface.y2 - surface.y1 + 1
    );
    return std::max<lv_coord_t>(
        1,
        static_cast<lv_coord_t>(
            height /
            core::state::sequencer::DrumSequencerState::VISIBLE_LANE_COUNT
        )
    );
}

FLASHMEM void includeDamage(
    lv_area_t& damage,
    bool& hasDamage,
    const lv_area_t& area
) {
    if (!hasDamage) {
        damage = area;
        hasDamage = true;
        return;
    }
    damage.x1 = std::min(damage.x1, area.x1);
    damage.y1 = std::min(damage.y1, area.y1);
    damage.x2 = std::max(damage.x2, area.x2);
    damage.y2 = std::max(damage.y2, area.y2);
}

constexpr bool sameArea(const lv_area_t& lhs, const lv_area_t& rhs) {
    return lhs.x1 == rhs.x1 && lhs.y1 == rhs.y1 &&
        lhs.x2 == rhs.x2 && lhs.y2 == rhs.y2;
}

FLASHMEM void drawRect(
    lv_layer_t* layer,
    const lv_area_t& area,
    uint32_t color,
    lv_opa_t backgroundOpacity,
    lv_coord_t borderWidth = 0,
    lv_opa_t borderOpacity = LV_OPA_TRANSP,
    lv_coord_t radius = 0
) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = backgroundOpacity;
    dsc.border_color = lv_color_hex(color);
    dsc.border_width = borderWidth;
    dsc.border_opa = borderOpacity;
    dsc.radius = radius;
    lv_draw_rect(layer, &dsc, &area);
}

FLASHMEM void drawFocusChevron(
    lv_layer_t* layer,
    lv_coord_t tipX,
    lv_coord_t centerY
) {
    for (lv_coord_t offset = -2; offset <= 2; ++offset) {
        const lv_coord_t distance = offset < 0
            ? static_cast<lv_coord_t>(-offset)
            : offset;
        const lv_coord_t x = static_cast<lv_coord_t>(
            tipX - distance
        );
        drawRect(
            layer,
            lv_area_t{.x1 = x, .y1 = static_cast<lv_coord_t>(centerY + offset),
                      .x2 = x, .y2 = static_cast<lv_coord_t>(centerY + offset)},
            theme::color::FOCUS_EDIT,
            LV_OPA_COVER
        );
    }
}

FLASHMEM void drawLabel(
    lv_layer_t* layer,
    const lv_area_t& area,
    const char* text,
    uint32_t color,
    lv_opa_t opacity,
    lv_text_align_t alignment = LV_TEXT_ALIGN_CENTER,
    const lv_font_t* font = nullptr
) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = font
        ? font
        : (fonts.meta_label() ? fonts.meta_label() : LV_FONT_DEFAULT);
    dsc.color = lv_color_hex(color);
    dsc.opa = opacity;
    dsc.align = alignment;
    lv_draw_label(layer, &dsc, &area);
}

FLASHMEM void drawIcon(
    lv_layer_t* layer,
    const lv_area_t& area,
    const char* icon,
    uint32_t color,
    lv_opa_t opacity
) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = icon;
    dsc.font = standalone_fonts.icons_14
        ? standalone_fonts.icons_14
        : LV_FONT_DEFAULT;
    dsc.color = lv_color_hex(color);
    dsc.opa = opacity;
    dsc.align = LV_TEXT_ALIGN_CENTER;
    lv_draw_label(layer, &dsc, &area);
}

FLASHMEM void drawAdvancedContentBadges(
    lv_layer_t* layer,
    const lv_area_t& cellArea,
    bool microSequence,
    bool cycleStates
) {
    if (!microSequence && !cycleStates) return;
    const lv_coord_t badgeX = static_cast<lv_coord_t>(cellArea.x2 - 2);
    if (microSequence) {
        drawRect(
            layer,
            lv_area_t{
                .x1 = badgeX,
                .y1 = static_cast<lv_coord_t>(cellArea.y1 + 1),
                .x2 = cellArea.x2,
                .y2 = static_cast<lv_coord_t>(cellArea.y1 + 3),
            },
            theme::color::STEP_MICRO_SEQUENCE,
            LV_OPA_COVER,
            0,
            LV_OPA_TRANSP,
            1
        );
    }
    if (cycleStates) {
        drawRect(
            layer,
            lv_area_t{
                .x1 = badgeX,
                .y1 = static_cast<lv_coord_t>(cellArea.y1 + 5),
                .x2 = cellArea.x2,
                .y2 = static_cast<lv_coord_t>(cellArea.y1 + 7),
            },
            theme::color::STEP_CYCLE_STATE,
            LV_OPA_COVER,
            0,
            LV_OPA_TRANSP,
            1
        );
    }
}

FLASHMEM void drawMicroRail(
    lv_layer_t* layer,
    const lv_area_t& cellArea,
    uint16_t activeMask,
    uint8_t length,
    bool cursorVisible,
    uint8_t cursor,
    uint32_t eventColor
) {
    if (length == 0U) return;
    length = std::min<uint8_t>(length, 16U);
    const lv_coord_t left = static_cast<lv_coord_t>(cellArea.x1 + 2);
    const lv_coord_t right = static_cast<lv_coord_t>(cellArea.x2 - 3);
    const lv_coord_t width = static_cast<lv_coord_t>(right - left + 1);
    if (width <= 0) return;

    for (uint8_t index = 0U; index < length; ++index) {
        const lv_coord_t start = static_cast<lv_coord_t>(
            left + (static_cast<uint16_t>(index) * width) / length
        );
        lv_coord_t end = static_cast<lv_coord_t>(
            left + (static_cast<uint16_t>(index + 1U) * width) / length - 1
        );
        end = std::max<lv_coord_t>(start, end);
        if (length <= 8U && end > start) --end;
        const bool active =
            (activeMask & static_cast<uint16_t>(1U << index)) != 0U;
        drawRect(
            layer,
            lv_area_t{
                .x1 = start,
                .y1 = static_cast<lv_coord_t>(cellArea.y2 - 1),
                .x2 = end,
                .y2 = cellArea.y2,
            },
            active ? eventColor : theme::color::BORDER_STRONG,
            active ? LV_OPA_COVER : LV_OPA_30
        );
        if (cursorVisible && index == cursor) {
            drawRect(
                layer,
                lv_area_t{
                    .x1 = start,
                    .y1 = static_cast<lv_coord_t>(cellArea.y2 - 3),
                    .x2 = end,
                    .y2 = static_cast<lv_coord_t>(cellArea.y2 - 2),
                },
                theme::color::LIVE_TIME,
                LV_OPA_COVER
            );
        }
    }
}

struct DrumHitGeometry {
    lv_area_t hitArea{};
    lv_area_t onsetArea{};
    lv_area_t nudgeArea{};
    lv_coord_t onset = 0;
    bool nudgeVisible = false;
};

FLASHMEM DrumHitGeometry buildDrumHitGeometry(
    lv_coord_t cellX,
    lv_coord_t rowY,
    lv_coord_t laneHeight,
    lv_coord_t cellWidth,
    lv_coord_t gridStart,
    lv_coord_t gridEnd,
    const drum_hit_visual::DrumHitVisualSpec& event
) {
    DrumHitGeometry geometry{};
    const lv_coord_t innerWidth = std::max<lv_coord_t>(
        2,
        static_cast<lv_coord_t>(cellWidth - 4)
    );
    const lv_coord_t innerBottom = static_cast<lv_coord_t>(
        rowY + laneHeight - 3
    );
    const lv_coord_t maxHitHeight = std::max<lv_coord_t>(
        2,
        static_cast<lv_coord_t>(laneHeight - 5)
    );
    const lv_coord_t hitHeight = static_cast<lv_coord_t>(
        2 + (static_cast<uint16_t>(event.velocity) *
                 static_cast<uint16_t>(maxHitHeight - 2)) /
                127U
    );
    const lv_coord_t nominalOnset = static_cast<lv_coord_t>(cellX + 2);
    const lv_coord_t nudgeOffset = static_cast<lv_coord_t>(
        (static_cast<int32_t>(event.nudge) * innerWidth) / 100
    );
    geometry.onset = std::clamp<lv_coord_t>(
        static_cast<lv_coord_t>(nominalOnset + nudgeOffset),
        static_cast<lv_coord_t>(gridStart + 1),
        static_cast<lv_coord_t>(gridEnd - 2)
    );
    const lv_coord_t gateWidth = std::max<lv_coord_t>(
        2,
        static_cast<lv_coord_t>(
            (static_cast<uint32_t>(event.gatePercent) * innerWidth) / 100U
        )
    );
    const lv_coord_t gateEnd = std::clamp<lv_coord_t>(
        static_cast<lv_coord_t>(geometry.onset + gateWidth - 1),
        geometry.onset,
        static_cast<lv_coord_t>(gridEnd - 2)
    );
    geometry.hitArea = {
        .x1 = geometry.onset,
        .y1 = static_cast<lv_coord_t>(innerBottom - hitHeight + 1),
        .x2 = gateEnd,
        .y2 = innerBottom,
    };
    geometry.onsetArea = {
        .x1 = geometry.onset,
        .y1 = geometry.hitArea.y1,
        .x2 = static_cast<lv_coord_t>(
            std::min<lv_coord_t>(gateEnd, geometry.onset + 1)
        ),
        .y2 = geometry.hitArea.y2,
    };
    geometry.nudgeVisible = event.nudge != 0;
    geometry.nudgeArea = {
        .x1 = std::min(nominalOnset, geometry.onset),
        .y1 = static_cast<lv_coord_t>(rowY + 1),
        .x2 = std::max(nominalOnset, geometry.onset),
        .y2 = static_cast<lv_coord_t>(rowY + 2),
    };
    return geometry;
}

FLASHMEM void drawDrumHit(
    lv_layer_t* layer,
    const DrumHitGeometry& geometry,
    const drum_hit_visual::DrumHitVisualSpec& event
) {
    if (event.ghost) {
        drawRect(
            layer,
            geometry.hitArea,
            event.color,
            LV_OPA_TRANSP,
            1,
            static_cast<lv_opa_t>(event.opacity),
            1
        );
        if (geometry.nudgeVisible) {
            drawRect(
                layer,
                geometry.nudgeArea,
                theme::color::STEP_NUDGE,
                LV_OPA_30
            );
        }
        return;
    }

    drawRect(
        layer,
        geometry.hitArea,
        event.color,
        static_cast<lv_opa_t>(event.opacity),
        0,
        LV_OPA_TRANSP,
        1
    );
    drawRect(layer, geometry.onsetArea, event.color, LV_OPA_COVER);
    if (geometry.nudgeVisible) {
        drawRect(
            layer,
            geometry.nudgeArea,
            theme::color::STEP_NUDGE,
            LV_OPA_COVER
        );
    }
}

using DrumSequencerState = core::state::sequencer::DrumSequencerState;
using StructureNavigationFocus = core::state::StructureNavigationFocus;

struct DrumGridRenderContext {
    lv_layer_t* layer = nullptr;
    const DrumSequencerState& drumUi;
    lv_area_t surface{};
    StructureNavigationFocus focus = StructureNavigationFocus::PAGE;
    lv_coord_t laneHeight = 0;
    lv_coord_t cellWidth = 0;
    lv_coord_t gridStart = 0;
    lv_coord_t gridEnd = 0;
    uint8_t laneCount = 0U;
    uint8_t pageStart = 0U;
    bool focusedLaneNameVisible = false;
    uint8_t focusedLaneNameLane = 0xFFU;
};

FLASHMEM void drawTrackOverview(
    lv_layer_t* layer,
    const lv_area_t& surface,
    lv_coord_t width,
    const DrumSequencerState& drumUi,
    uint8_t laneCount,
    uint8_t midiChannel
) {
    const uint32_t trackColor = theme::color::trackColor(drumUi.targetTrack);
    drawRect(
        layer,
        lv_area_t{
            .x1 = static_cast<lv_coord_t>(surface.x1 + 8),
            .y1 = static_cast<lv_coord_t>(surface.y1 + 8),
            .x2 = static_cast<lv_coord_t>(surface.x2 - 8),
            .y2 = static_cast<lv_coord_t>(surface.y1 + 10),
        },
        trackColor,
        LV_OPA_COVER,
        0,
        LV_OPA_TRANSP,
        1
    );

    char title[32] = {};
    std::snprintf(
        title,
        sizeof(title),
        "Drum Track %u",
        static_cast<unsigned>(drumUi.targetTrack + 1U)
    );
    drawLabel(
        layer,
        lv_area_t{
            .x1 = static_cast<lv_coord_t>(surface.x1 + 8),
            .y1 = static_cast<lv_coord_t>(surface.y1 + 16),
            .x2 = static_cast<lv_coord_t>(surface.x2 - 8),
            .y2 = static_cast<lv_coord_t>(surface.y1 + 35),
        },
        title,
        theme::color::TEXT_PRIMARY,
        LV_OPA_COVER
    );

    char summary[64] = {};
    std::snprintf(
        summary,
        sizeof(summary),
        "%u lanes  \xC2\xB7  Ch %u  \xC2\xB7  %u steps  \xC2\xB7  1/%u",
        static_cast<unsigned>(laneCount),
        static_cast<unsigned>(midiChannel),
        static_cast<unsigned>(drumUi.drumTrack->pattern.defaultLength),
        static_cast<unsigned>(
            drumUi.drumTrack->pattern.defaultStepsPerBeat * 4U
        )
    );
    drawLabel(
        layer,
        lv_area_t{
            .x1 = static_cast<lv_coord_t>(surface.x1 + 8),
            .y1 = static_cast<lv_coord_t>(surface.y1 + 39),
            .x2 = static_cast<lv_coord_t>(surface.x2 - 8),
            .y2 = static_cast<lv_coord_t>(surface.y1 + 56),
        },
        summary,
        theme::color::TEXT_SECONDARY,
        LV_OPA_80
    );

    const lv_coord_t paletteWidth = static_cast<lv_coord_t>(
        std::max<lv_coord_t>(1, (width - 18) / 8)
    );
    for (uint8_t lane = 0U; lane < laneCount; ++lane) {
        const uint8_t row = static_cast<uint8_t>(lane / 8U);
        const uint8_t column = static_cast<uint8_t>(lane % 8U);
        const lv_coord_t x = static_cast<lv_coord_t>(
            surface.x1 + 8 + column * paletteWidth
        );
        const lv_coord_t y = static_cast<lv_coord_t>(
            surface.y1 + 65 + row * 27
        );
        const auto& laneDescriptor = drumUi.drumTrack->kit.lanes[lane];
        const uint32_t laneColor = theme::color::trackColor(
            core::state::sequencer::drumLaneDisplayColorIndex(laneDescriptor)
        );
        drawRect(
            layer,
            lv_area_t{
                .x1 = x,
                .y1 = y,
                .x2 = static_cast<lv_coord_t>(x + paletteWidth - 4),
                .y2 = static_cast<lv_coord_t>(y + 2),
            },
            laneColor,
            LV_OPA_COVER,
            0,
            LV_OPA_TRANSP,
            1
        );
        drawIcon(
            layer,
            lv_area_t{
                .x1 = x,
                .y1 = static_cast<lv_coord_t>(y + 5),
                .x2 = static_cast<lv_coord_t>(x + paletteWidth - 4),
                .y2 = static_cast<lv_coord_t>(y + 20),
            },
            core::ui::sequencer::drumLaneIconGlyph(
                core::state::sequencer::drumLaneDisplayIcon(laneDescriptor)
            ),
            laneColor,
            LV_OPA_COVER
        );
    }
}

FLASHMEM void drawLaneFocusMarker(
    const DrumGridRenderContext& context,
    lv_coord_t rowY,
    bool selected
) {
    if (!selected) return;
    drawRect(
        context.layer,
        lv_area_t{
            .x1 = context.surface.x1,
            .y1 = static_cast<lv_coord_t>(rowY + 2),
            .x2 = static_cast<lv_coord_t>(context.surface.x1 + 1),
            .y2 = static_cast<lv_coord_t>(rowY + context.laneHeight - 3),
        },
        theme::color::FOCUS_EDIT,
        LV_OPA_COVER
    );
    drawFocusChevron(
        context.layer,
        static_cast<lv_coord_t>(context.surface.x1 + 6),
        static_cast<lv_coord_t>(rowY + context.laneHeight / 2)
    );
}

FLASHMEM void drawAddLaneRow(
    const DrumGridRenderContext& context,
    lv_coord_t rowY,
    bool selected
) {
    const uint32_t color = selected
        ? theme::color::FOCUS_EDIT
        : theme::color::TEXT_SECONDARY;
    drawLabel(
        context.layer,
        lv_area_t{
            .x1 = static_cast<lv_coord_t>(context.surface.x1 + 2),
            .y1 = rowY,
            .x2 = static_cast<lv_coord_t>(context.surface.x1 + 14),
            .y2 = static_cast<lv_coord_t>(rowY + context.laneHeight - 1),
        },
        "+",
        color,
        selected ? LV_OPA_COVER : LV_OPA_60
    );
    drawLabel(
        context.layer,
        lv_area_t{
            .x1 = static_cast<lv_coord_t>(context.surface.x1 + 15),
            .y1 = rowY,
            .x2 = static_cast<lv_coord_t>(
                context.surface.x1 + DRUM_LABEL_WIDTH - 3
            ),
            .y2 = static_cast<lv_coord_t>(rowY + context.laneHeight - 1),
        },
        "ADD",
        color,
        selected ? LV_OPA_COVER : LV_OPA_60,
        LV_TEXT_ALIGN_LEFT
    );
    drawRect(
        context.layer,
        lv_area_t{
            .x1 = static_cast<lv_coord_t>(context.gridStart + 1),
            .y1 = static_cast<lv_coord_t>(rowY + 2),
            .x2 = static_cast<lv_coord_t>(context.gridEnd - 2),
            .y2 = static_cast<lv_coord_t>(rowY + context.laneHeight - 3),
        },
        color,
        LV_OPA_TRANSP,
        1,
        selected ? LV_OPA_COVER : LV_OPA_30,
        1
    );
}

FLASHMEM void drawLaneHeader(
    const DrumGridRenderContext& context,
    uint8_t lane,
    lv_coord_t rowY,
    uint32_t laneColor,
    bool selected
) {
    const auto& laneDescriptor = context.drumUi.drumTrack->kit.lanes[lane];
    const auto& lanePattern = context.drumUi.drumTrack->pattern.lanes[lane];
    const lv_area_t nameArea{
        .x1 = static_cast<lv_coord_t>(context.surface.x1 + 23),
        .y1 = rowY,
        .x2 = static_cast<lv_coord_t>(
            context.surface.x1 + DRUM_LABEL_WIDTH - 3
        ),
        .y2 = static_cast<lv_coord_t>(rowY + context.laneHeight - 1),
    };
    drawIcon(
        context.layer,
        lv_area_t{
            .x1 = static_cast<lv_coord_t>(context.surface.x1 + 8),
            .y1 = rowY,
            .x2 = static_cast<lv_coord_t>(context.surface.x1 + 21),
            .y2 = static_cast<lv_coord_t>(rowY + context.laneHeight - 1),
        },
        core::ui::sequencer::drumLaneIconGlyph(
            core::state::sequencer::drumLaneDisplayIcon(laneDescriptor)
        ),
        laneColor,
        selected ? LV_OPA_COVER : IDLE_LANE_ICON_OPA
    );

    const bool focusedNameOwnsRendering =
        context.focusedLaneNameVisible &&
        context.focusedLaneNameLane == lane;
    if (!focusedNameOwnsRendering) {
        const lv_font_t* nameFont = selected
            ? fonts.compact_selected()
            : fonts.meta_label();
        nameFont = nameFont ? nameFont : LV_FONT_DEFAULT;
        char displayName[16] = {};
        ms::ui::text::formatEllipsized(
            displayName,
            sizeof(displayName),
            core::state::sequencer::drumLaneDisplayName(laneDescriptor),
            nameFont,
            static_cast<lv_coord_t>(nameArea.x2 - nameArea.x1 + 1)
        );
        drawLabel(
            context.layer,
            nameArea,
            displayName,
            selected
                ? theme::color::TEXT_PRIMARY
                : theme::color::TEXT_SECONDARY,
            selected ? LV_OPA_COVER : IDLE_LANE_TEXT_OPA,
            LV_TEXT_ALIGN_LEFT,
            nameFont
        );
    }

    if (lanePattern.timing.mode ==
        core::state::sequencer::DrumLaneTimingMode::CUSTOM) {
        drawRect(
            context.layer,
            lv_area_t{
                .x1 = static_cast<lv_coord_t>(
                    context.surface.x1 + DRUM_LABEL_WIDTH - 5
                ),
                .y1 = static_cast<lv_coord_t>(rowY + 6),
                .x2 = static_cast<lv_coord_t>(
                    context.surface.x1 + DRUM_LABEL_WIDTH - 3
                ),
                .y2 = static_cast<lv_coord_t>(rowY + 8),
            },
            DRUM_TIMING,
            LV_OPA_COVER,
            0,
            LV_OPA_TRANSP,
            2
        );
    }
}

FLASHMEM void drawDrumStepCell(
    const DrumGridRenderContext& context,
    uint8_t lane,
    uint8_t row,
    uint8_t column,
    lv_coord_t rowY,
    uint8_t laneLength,
    uint8_t stepsPerBeat,
    uint16_t laneBit,
    uint32_t laneColor,
    bool selected
) {
    const auto& drumUi = context.drumUi;
    const auto& lanePattern = drumUi.drumTrack->pattern.lanes[lane];
    const uint8_t step = drumUi.visibleStep(column);
    const bool available = step < laneLength;
    const bool active = available &&
        drumUi.drumTrack->pattern.stepEnabled(lane, step);
    const lv_coord_t cellX = static_cast<lv_coord_t>(
        context.gridStart + column * context.cellWidth
    );
    const lv_area_t cellArea{
        .x1 = static_cast<lv_coord_t>(cellX + 1),
        .y1 = static_cast<lv_coord_t>(rowY + 1),
        .x2 = static_cast<lv_coord_t>(cellX + context.cellWidth - 2),
        .y2 = static_cast<lv_coord_t>(rowY + context.laneHeight - 2),
    };
    const bool focused = selected &&
        context.focus == StructureNavigationFocus::STEP &&
        step == drumUi.focusedStep;
    const uint16_t resolvedContextKey = static_cast<uint16_t>(
        (static_cast<uint16_t>(drumUi.page) << 8U) |
        drumUi.laneWindowStart
    );
    const auto& resolvedPage = drumUi.resolvedPage;
    const std::size_t resolvedCell =
        core::state::sequencer::DrumResolvedPageProjection::cellIndex(
            row,
            column
        );
    const bool projectionAvailable =
        resolvedPage.contextKey == resolvedContextKey;
    const uint64_t resolvedCellBit =
        core::state::sequencer::DrumResolvedPageProjection::cellBit(
            row,
            column
        );
    const uint8_t microLength = projectionAvailable
        ? resolvedPage.microLength[resolvedCell]
        : 0U;
    const uint16_t microMask = projectionAvailable
        ? resolvedPage.microMask[resolvedCell]
        : 0U;
    const bool hasMicroSequence = microLength > 0U;
    const bool hasCycleStates = projectionAvailable &&
        (resolvedPage.cyclePresentMask & resolvedCellBit) != 0U;
    const bool microCursorVisible = microLength > 0U &&
        drumUi.playbackActive &&
        (drumUi.playheadValidMask & laneBit) != 0U &&
        drumUi.playheadSteps[lane] == step;
    const uint8_t microCursor = microCursorVisible
        ? std::min<uint8_t>(
              static_cast<uint8_t>(microLength - 1U),
              static_cast<uint8_t>(
                  (static_cast<uint16_t>(drumUi.playheadPhasesQ8[lane]) *
                   microLength) /
                  256U
              )
          )
        : 0U;

    const bool pageGroupBoundary = column == PAGE_GROUP_SPLIT_COLUMN;
    const bool beatBoundary = available && stepsPerBeat > 0U &&
        step % stepsPerBeat == 0U;
    if (pageGroupBoundary || beatBoundary) {
        drawRect(
            context.layer,
            lv_area_t{
                .x1 = cellX,
                .y1 = static_cast<lv_coord_t>(rowY + 1),
                .x2 = cellX,
                .y2 = static_cast<lv_coord_t>(
                    rowY + context.laneHeight - 2
                ),
            },
            pageGroupBoundary
                ? theme::color::BORDER_STRONG
                : theme::color::BORDER_SUBTLE,
            pageGroupBoundary
                ? (selected ? SELECTED_PAGE_GROUP_OPA : IDLE_PAGE_GROUP_OPA)
                : (selected ? SELECTED_BEAT_OPA : IDLE_BEAT_OPA)
        );
    }

    drawRect(
        context.layer,
        cellArea,
        theme::color::SURFACE_IDLE,
        available
            ? (selected ? LV_OPA_COVER : IDLE_CELL_OPA)
            : UNAVAILABLE_CELL_OPA,
        0,
        LV_OPA_TRANSP,
        1
    );
    if (focused) {
        drawRect(
            context.layer,
            cellArea,
            theme::color::FOCUS_EDIT,
            LV_OPA_TRANSP,
            1,
            LV_OPA_COVER,
            1
        );
    }

    if (!active) {
        drawAdvancedContentBadges(
            context.layer,
            cellArea,
            hasMicroSequence,
            hasCycleStates
        );
        drawMicroRail(
            context.layer,
            cellArea,
            microMask,
            microLength,
            microCursorVisible,
            microCursor,
            laneColor
        );
        return;
    }

    const uint8_t velocity = lanePattern.velocity[step];
    const uint16_t gate = lanePattern.gate[step];
    const int8_t nudge = lanePattern.nudge[step];
    const uint8_t probability = lanePattern.probability[step];
    const auto authoredEvent = drum_hit_visual::build(
        velocity,
        gate,
        nudge,
        laneColor,
        true
    );
    const auto authoredGeometry = buildDrumHitGeometry(
        cellX,
        rowY,
        context.laneHeight,
        context.cellWidth,
        context.gridStart,
        context.gridEnd,
        authoredEvent
    );
    const bool resolvedAvailable = (hasMicroSequence || hasCycleStates)
        ? drumUi.playbackActive && projectionAvailable &&
            (resolvedPage.validMask & resolvedCellBit) != 0U
        : false;
    const bool resolvedPlayed = resolvedAvailable &&
        (resolvedPage.playedMask & resolvedCellBit) != 0U;
    const uint8_t effectiveVelocity = resolvedPlayed
        ? resolvedPage.velocity[resolvedCell]
        : velocity;
    const uint16_t effectiveGate = resolvedPlayed
        ? resolvedPage.gate[resolvedCell]
        : gate;
    const int8_t effectiveNudge = resolvedPlayed
        ? resolvedPage.nudge[resolvedCell]
        : nudge;
    const bool resolvedDiffers = resolvedAvailable &&
        (!resolvedPlayed || effectiveVelocity != velocity ||
         effectiveGate != gate || effectiveNudge != nudge);
    const auto effectiveEvent = drum_hit_visual::build(
        effectiveVelocity,
        effectiveGate,
        effectiveNudge,
        laneColor,
        resolvedPlayed
    );
    const auto effectiveGeometry = resolvedPlayed
        ? buildDrumHitGeometry(
              cellX,
              rowY,
              context.laneHeight,
              context.cellWidth,
              context.gridStart,
              context.gridEnd,
              effectiveEvent
          )
        : authoredGeometry;

    if (resolvedDiffers) {
        const auto authoredGhost = drum_hit_visual::build(
            velocity,
            gate,
            nudge,
            resolvedPlayed ? laneColor : theme::color::INACTIVE,
            true,
            true
        );
        drawDrumHit(context.layer, authoredGeometry, authoredGhost);
    }
    if (!resolvedAvailable || resolvedPlayed) {
        drawDrumHit(
            context.layer,
            effectiveGeometry,
            resolvedAvailable ? effectiveEvent : authoredEvent
        );
    }

    const bool rootDecisionAvailable =
        (drumUi.chanceDecisionValidMask & laneBit) != 0U &&
        drumUi.chanceDecisionSteps[lane] == step;
    const bool decisionAvailable = resolvedAvailable || rootDecisionAvailable;
    const bool decisionPlayed = resolvedAvailable
        ? resolvedPlayed
        : rootDecisionAvailable &&
            (drumUi.chanceDecisionPlayedMask & laneBit) != 0U;
    if (probability < 100U || (resolvedAvailable && !resolvedPlayed)) {
        const lv_coord_t chanceX = std::clamp<lv_coord_t>(
            static_cast<lv_coord_t>(effectiveGeometry.onset + 2),
            static_cast<lv_coord_t>(cellX + 2),
            static_cast<lv_coord_t>(cellX + context.cellWidth - 4)
        );
        drawRect(
            context.layer,
            lv_area_t{
                .x1 = chanceX,
                .y1 = static_cast<lv_coord_t>(rowY + 2),
                .x2 = static_cast<lv_coord_t>(chanceX + 2),
                .y2 = static_cast<lv_coord_t>(rowY + 4),
            },
            decisionAvailable
                ? decisionPlayed
                    ? theme::color::LIVE_TIME
                    : theme::color::INACTIVE
                : theme::color::STEP_CHANCE,
            decisionAvailable && !decisionPlayed
                ? LV_OPA_TRANSP
                : LV_OPA_COVER,
            decisionAvailable && !decisionPlayed ? 1 : 0,
            decisionAvailable && !decisionPlayed
                ? LV_OPA_COVER
                : LV_OPA_TRANSP,
            2
        );
    }
    drawAdvancedContentBadges(
        context.layer,
        cellArea,
        hasMicroSequence,
        hasCycleStates
    );
    drawMicroRail(
        context.layer,
        cellArea,
        microMask,
        microLength,
        microCursorVisible,
        microCursor,
        laneColor
    );
}

FLASHMEM void drawLaneLoopEnd(
    const DrumGridRenderContext& context,
    lv_coord_t rowY,
    uint8_t laneLength,
    bool selected
) {
    if (laneLength <= context.pageStart ||
        laneLength > static_cast<uint8_t>(
            context.pageStart + DrumSequencerState::STEPS_PER_PAGE
        )) {
        return;
    }
    const uint8_t endColumn = static_cast<uint8_t>(
        laneLength - context.pageStart
    );
    const lv_coord_t endX = static_cast<lv_coord_t>(
        context.gridStart + endColumn * context.cellWidth
    );
    const lv_coord_t markerX = std::min<lv_coord_t>(
        endX,
        static_cast<lv_coord_t>(context.gridEnd - 2)
    );
    const lv_opa_t opacity = selected ? LV_OPA_COVER : IDLE_LOOP_OPA;
    drawRect(
        context.layer,
        lv_area_t{
            .x1 = static_cast<lv_coord_t>(markerX - 1),
            .y1 = static_cast<lv_coord_t>(rowY + 1),
            .x2 = markerX,
            .y2 = static_cast<lv_coord_t>(rowY + context.laneHeight - 2),
        },
        theme::color::BORDER_STRONG,
        opacity
    );
    drawRect(
        context.layer,
        lv_area_t{
            .x1 = static_cast<lv_coord_t>(markerX - 4),
            .y1 = static_cast<lv_coord_t>(rowY + 1),
            .x2 = markerX,
            .y2 = static_cast<lv_coord_t>(rowY + 2),
        },
        theme::color::BORDER_STRONG,
        opacity
    );
    drawRect(
        context.layer,
        lv_area_t{
            .x1 = static_cast<lv_coord_t>(markerX - 4),
            .y1 = static_cast<lv_coord_t>(rowY + context.laneHeight - 3),
            .x2 = markerX,
            .y2 = static_cast<lv_coord_t>(rowY + context.laneHeight - 2),
        },
        theme::color::BORDER_STRONG,
        opacity
    );
}

FLASHMEM void drawLanePlayhead(
    const DrumGridRenderContext& context,
    uint8_t lane,
    uint16_t laneBit,
    lv_coord_t rowY
) {
    const auto& drumUi = context.drumUi;
    if (!drumUi.playbackActive ||
        (drumUi.playheadValidMask & laneBit) == 0U) {
        return;
    }
    const uint8_t playhead = drumUi.playheadSteps[lane];
    if (playhead < context.pageStart ||
        playhead >= static_cast<uint8_t>(
            context.pageStart + DrumSequencerState::STEPS_PER_PAGE
        )) {
        return;
    }
    const lv_coord_t playheadX = static_cast<lv_coord_t>(
        context.gridStart + (playhead - context.pageStart) * context.cellWidth +
        (static_cast<uint16_t>(drumUi.playheadPhasesQ8[lane]) *
         context.cellWidth) /
            256U
    );
    drawRect(
        context.layer,
        lv_area_t{
            .x1 = playheadX,
            .y1 = rowY,
            .x2 = playheadX,
            .y2 = static_cast<lv_coord_t>(rowY + context.laneHeight - 1),
        },
        theme::color::LIVE_TIME,
        LV_OPA_COVER
    );
}

FLASHMEM void drawLaneSelectionOutline(
    const DrumGridRenderContext& context,
    lv_coord_t rowY,
    bool contentSelected,
    bool destination,
    uint32_t color
) {
    if (!contentSelected && !destination) return;
    drawRect(
        context.layer,
        lv_area_t{
            .x1 = static_cast<lv_coord_t>(context.surface.x1 + 1),
            .y1 = static_cast<lv_coord_t>(rowY + 1),
            .x2 = static_cast<lv_coord_t>(context.gridEnd - 2),
            .y2 = static_cast<lv_coord_t>(rowY + context.laneHeight - 2),
        },
        color,
        LV_OPA_TRANSP,
        1,
        destination ? LV_OPA_COVER : LV_OPA_70,
        1
    );
}

FLASHMEM void drawDrumLaneRow(
    const DrumGridRenderContext& context,
    uint8_t row,
    bool addSlotFocused
) {
    const auto& drumUi = context.drumUi;
    const auto& laneSelection = drumUi.laneSelection;
    const uint8_t lane = drumUi.visibleLane(row);
    const lv_coord_t rowY = static_cast<lv_coord_t>(
        context.surface.y1 + row * context.laneHeight
    );
    const bool addRow = lane >= context.laneCount;
    const bool selected = addRow
        ? addSlotFocused
        : laneSelection.active
            ? lane == laneSelection.cursorLane
            : lane == drumUi.selectedLane && !addSlotFocused;
    const lv_area_t& clip = context.layer->_clip_area;
    const bool headerVisible = clip.x1 < context.gridStart;
    if (headerVisible) {
        drawLaneFocusMarker(context, rowY, selected);
    }
    if (addRow) {
        drawAddLaneRow(context, rowY, selected);
        return;
    }

    const auto& laneDescriptor = drumUi.drumTrack->kit.lanes[lane];
    const uint32_t laneColor = theme::color::trackColor(
        core::state::sequencer::drumLaneDisplayColorIndex(laneDescriptor)
    );
    const uint16_t laneBit = static_cast<uint16_t>(1U << lane);
    const bool contentSelected = laneSelection.active &&
        (laneSelection.selectedMask & laneBit) != 0U;
    const bool destination =
        (laneSelection.placementActive() || laneSelection.moveActive()) &&
        (laneSelection.destinationMask & laneBit) != 0U;
    const bool destinationOverwrite = destination &&
        (laneSelection.overwriteMask & laneBit) != 0U;
    const uint32_t selectionMarkerColor = destination
        ? laneSelection.pasteBlocked
            ? theme::color::DESTRUCTIVE
            : destinationOverwrite
                ? theme::color::WARNING
                : theme::color::POSITIVE
        : theme::color::TEXT_PRIMARY;
    const uint8_t laneLength =
        drumUi.drumTrack->pattern.effectiveLength(lane);
    const uint8_t stepsPerBeat =
        drumUi.drumTrack->pattern.effectiveStepsPerBeat(lane);

    if (headerVisible) {
        drawLaneHeader(context, lane, rowY, laneColor, selected);
    }
    if (clip.x2 >= context.gridStart && clip.x1 < context.gridEnd) {
        // Gate tails can extend through every following cell. A negative
        // Nudge can only pull the next cell half a cell to the left, so later
        // cells cannot contribute to this damage band.
        const lv_coord_t lastRelevantX = static_cast<lv_coord_t>(
            clip.x2 - context.gridStart + context.cellWidth / 2
        );
        const uint8_t lastColumn = static_cast<uint8_t>(
            std::clamp<lv_coord_t>(
                static_cast<lv_coord_t>(
                    lastRelevantX / context.cellWidth
                ),
                0,
                static_cast<lv_coord_t>(
                    DrumSequencerState::STEPS_PER_PAGE - 1U
                )
            )
        );
        for (uint8_t column = 0U; column <= lastColumn; ++column) {
            drawDrumStepCell(
                context,
                lane,
                row,
                column,
                rowY,
                laneLength,
                stepsPerBeat,
                laneBit,
                laneColor,
                selected
            );
        }
    }
    drawLaneLoopEnd(context, rowY, laneLength, selected);
    drawLanePlayhead(context, lane, laneBit, rowY);
    drawLaneSelectionOutline(
        context,
        rowY,
        contentSelected,
        destination,
        selectionMarkerColor
    );
}

FLASHMEM void drawDrumLaneGrid(const DrumGridRenderContext& context) {
    const auto& drumUi = context.drumUi;
    const auto& laneSelection = drumUi.laneSelection;
    const bool addSlotFocused = drumUi.laneAddSlotFocused();
    const uint8_t itemCount = static_cast<uint8_t>(
        context.laneCount +
        (drumUi.laneAddSlotVisible() && !laneSelection.active ? 1U : 0U)
    );
    const uint8_t visibleRowCount = drumUi.laneWindowStart < itemCount
        ? std::min<uint8_t>(
              static_cast<uint8_t>(itemCount - drumUi.laneWindowStart),
              DrumSequencerState::VISIBLE_LANE_COUNT
          )
        : 0U;
    const lv_area_t& clip = context.layer->_clip_area;
    for (uint8_t row = 0U; row < visibleRowCount; ++row) {
        const lv_coord_t rowY = static_cast<lv_coord_t>(
            context.surface.y1 + row * context.laneHeight
        );
        if (rowY > clip.y2 ||
            static_cast<lv_coord_t>(rowY + context.laneHeight - 1) <
                clip.y1) {
            continue;
        }
        drawDrumLaneRow(context, row, addSlotFocused);
    }
}

}  // namespace

FLASHMEM DrumOverviewSurface::DrumOverviewSurface(lv_obj_t* parent) {
    createUi(parent);
}

FLASHMEM DrumOverviewSurface::~DrumOverviewSurface() {
    // Retire the Label timer/animation before its LVGL parent disappears.
    focused_lane_name_.reset();
    if (root_) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

FLASHMEM void DrumOverviewSurface::createUi(lv_obj_t* parent) {
    if (!parent) return;

    root_ = lv_obj_create(parent);
    if (!root_) return;
    lv_obj_remove_style_all(root_);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(
        root_,
        lv_color_hex(theme::color::BACKGROUND),
        0
    );
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(root_, onDrawEvent, LV_EVENT_DRAW_MAIN, this);

    // One retained scroller is shared by whichever Lane currently owns PAGE
    // focus. Idle Lane names remain part of the allocation-free draw surface.
    focused_lane_name_ = std::make_unique<oc::ui::lvgl::Label>(root_);
    focused_lane_name_->ownsLvglObjects(false)
        .autoScroll(false)
        .alignment(LV_TEXT_ALIGN_LEFT)
        .width(static_cast<lv_coord_t>(DRUM_LABEL_WIDTH - 26))
        .color(theme::color::TEXT_PRIMARY)
        .font(fonts.compact_selected());
    lv_obj_t* focusedNameElement = focused_lane_name_->getElement();
    lv_obj_add_flag(focusedNameElement, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(focusedNameElement, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(focusedNameElement, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(focusedNameElement, LV_OBJ_FLAG_CLICKABLE);
}

FLASHMEM void DrumOverviewSurface::hideFocusedLaneName() {
    if (!focused_lane_name_) return;
    if (focused_lane_name_visible_) {
        focused_lane_name_->autoScroll(false);
        focused_lane_name_->setText("");
        lv_obj_add_flag(
            focused_lane_name_->getElement(), LV_OBJ_FLAG_HIDDEN
        );
    }
    focused_lane_name_cache_[0] = '\0';
    focused_lane_name_lane_ = 0xFFU;
    focused_lane_name_visible_ = false;
}

FLASHMEM void DrumOverviewSurface::syncFocusedLaneName(
    const DrumOverviewSurfaceProps& props
) {
    if (!focused_lane_name_ || !props.projection ||
        props.navigationFocus != core::state::StructureNavigationFocus::PAGE) {
        hideFocusedLaneName();
        return;
    }

    const auto& drumUi = *props.projection;
    const uint8_t laneCount = std::min<uint8_t>(
        drumUi.drumTrack->kit.laneCount,
        core::state::sequencer::DRUM_MAX_LANES
    );
    if (drumUi.laneAddSlotFocused()) {
        hideFocusedLaneName();
        return;
    }
    const uint8_t lane = drumUi.laneSelection.active
        ? drumUi.laneSelection.cursorLane
        : drumUi.selectedLane;
    if (lane >= laneCount || lane < drumUi.laneWindowStart ||
        lane >= static_cast<uint8_t>(
            drumUi.laneWindowStart +
            core::state::sequencer::DrumSequencerState::VISIBLE_LANE_COUNT
        )) {
        hideFocusedLaneName();
        return;
    }

    const uint8_t row = static_cast<uint8_t>(lane - drumUi.laneWindowStart);
    const lv_coord_t height = lv_obj_get_height(root_);
    if (height <= 0) {
        hideFocusedLaneName();
        return;
    }
    const lv_coord_t laneHeight = std::max<lv_coord_t>(
        1,
        static_cast<lv_coord_t>(
            height /
            core::state::sequencer::DrumSequencerState::VISIBLE_LANE_COUNT
        )
    );
    lv_obj_t* element = focused_lane_name_->getElement();
    lv_obj_set_pos(
        element,
        23,
        static_cast<lv_coord_t>(row * laneHeight)
    );
    lv_obj_set_height(element, laneHeight);
    if (auto* label = focused_lane_name_->getLabel()) {
        const lv_font_t* font = fonts.compact_selected()
            ? fonts.compact_selected()
            : LV_FONT_DEFAULT;
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_y(
            label,
            std::max<lv_coord_t>(
                0,
                static_cast<lv_coord_t>(
                    (laneHeight - font->line_height) / 2
                )
            )
        );
    }

    const char* name = core::state::sequencer::drumLaneDisplayName(
        drumUi.drumTrack->kit.lanes[lane]
    );
    const bool contentChanged = focused_lane_name_lane_ != lane ||
        std::strncmp(
            focused_lane_name_cache_.data(),
            name,
            focused_lane_name_cache_.size()
        ) != 0;
    if (contentChanged) {
        std::snprintf(
            focused_lane_name_cache_.data(),
            focused_lane_name_cache_.size(),
            "%s",
            name
        );
        focused_lane_name_->autoScroll(true);
        focused_lane_name_->setText(name);
        focused_lane_name_lane_ = lane;
    }
    lv_obj_clear_flag(element, LV_OBJ_FLAG_HIDDEN);
    focused_lane_name_visible_ = true;
}

FLASHMEM void DrumOverviewSurface::onDrawEvent(lv_event_t* event) {
    auto* self = static_cast<DrumOverviewSurface*>(
        lv_event_get_user_data(event)
    );
    if (!self) return;
    self->drawSurface(lv_event_get_layer(event));
}

FLASHMEM bool DrumOverviewSurface::staticVisualChanged(
    const DrumOverviewSurfaceProps& props
) const {
    return !rendered_ ||
        renderedProps_.projection != props.projection ||
        renderedProps_.navigationFocus != props.navigationFocus ||
        renderedProps_.midiChannel != props.midiChannel ||
        renderedProps_.authoredRevision != props.authoredRevision ||
        renderedProps_.uiRevision != props.uiRevision;
}

FLASHMEM DrumOverviewSurface::PlaybackSnapshot
DrumOverviewSurface::capturePlayback(
    const core::state::sequencer::DrumSequencerState& projection
) const {
    PlaybackSnapshot snapshot{};
    snapshot.playheadSteps = projection.playheadSteps;
    snapshot.playheadPhasesQ8 = projection.playheadPhasesQ8;
    snapshot.chanceDecisionSteps = projection.chanceDecisionSteps;
    snapshot.playheadValidMask = projection.playheadValidMask;
    snapshot.chanceDecisionValidMask = projection.chanceDecisionValidMask;
    snapshot.chanceDecisionPlayedMask = projection.chanceDecisionPlayedMask;
    snapshot.resolvedPage = projection.resolvedPage;
    snapshot.playbackActive = projection.playbackActive;
    return snapshot;
}

FLASHMEM void DrumOverviewSurface::includePlayheadDamage(
    const PlaybackSnapshot& previous,
    const PlaybackSnapshot& next,
    uint8_t lane,
    lv_coord_t rowY,
    const lv_area_t& surface,
    lv_area_t& damage,
    bool& hasDamage
) {
    if (!root_ || lane >= RUNTIME_LANE_CAPACITY) return;

    const auto& projection = *renderedProps_.projection;
    const uint8_t pageStart = static_cast<uint8_t>(
        projection.page *
        core::state::sequencer::DrumSequencerState::STEPS_PER_PAGE
    );
    const lv_coord_t width = static_cast<lv_coord_t>(
        surface.x2 - surface.x1 + 1
    );
    const lv_coord_t cellWidth = static_cast<lv_coord_t>(
        (width - DRUM_LABEL_WIDTH) /
        core::state::sequencer::DrumSequencerState::STEPS_PER_PAGE
    );
    const lv_coord_t gridStart = static_cast<lv_coord_t>(
        surface.x1 + DRUM_LABEL_WIDTH
    );
    const lv_coord_t laneHeight = drumLaneHeight(surface);
    const auto markerArea = [=](
        const PlaybackSnapshot& snapshot,
        lv_area_t& area
    ) {
        const uint16_t laneBit = static_cast<uint16_t>(1U << lane);
        if (!snapshot.playbackActive ||
            (snapshot.playheadValidMask & laneBit) == 0U) {
            return false;
        }
        const uint8_t step = snapshot.playheadSteps[lane];
        if (step < pageStart ||
            step >= static_cast<uint8_t>(
                pageStart + DrumSequencerState::STEPS_PER_PAGE
            )) {
            return false;
        }
        const lv_coord_t x = static_cast<lv_coord_t>(
            gridStart + (step - pageStart) * cellWidth +
            (static_cast<uint16_t>(snapshot.playheadPhasesQ8[lane]) *
             cellWidth) / 256U
        );
        area = {
            .x1 = static_cast<lv_coord_t>(x - 1),
            .y1 = rowY,
            .x2 = static_cast<lv_coord_t>(x + 1),
            .y2 = static_cast<lv_coord_t>(rowY + laneHeight - 1),
        };
        return true;
    };

    lv_area_t previousArea{};
    lv_area_t nextArea{};
    const bool previousVisible = markerArea(previous, previousArea);
    const bool nextVisible = markerArea(next, nextArea);
    if (previousVisible == nextVisible &&
        (!previousVisible || sameArea(previousArea, nextArea))) {
        return;
    }
    if (previousVisible) includeDamage(damage, hasDamage, previousArea);
    if (nextVisible) includeDamage(damage, hasDamage, nextArea);
}

FLASHMEM void DrumOverviewSurface::includeChanceCellDamage(
    const PlaybackSnapshot& snapshot,
    uint8_t lane,
    lv_coord_t rowY,
    const lv_area_t& surface,
    lv_area_t& damage,
    bool& hasDamage
) {
    if (!root_ || lane >= RUNTIME_LANE_CAPACITY) return;
    const uint16_t laneBit = static_cast<uint16_t>(1U << lane);
    if ((snapshot.chanceDecisionValidMask & laneBit) == 0U) return;

    const auto& projection = *renderedProps_.projection;
    const uint8_t pageStart = static_cast<uint8_t>(
        projection.page *
        core::state::sequencer::DrumSequencerState::STEPS_PER_PAGE
    );
    const uint8_t step = snapshot.chanceDecisionSteps[lane];
    if (step < pageStart ||
        step >= static_cast<uint8_t>(
            pageStart +
            core::state::sequencer::DrumSequencerState::STEPS_PER_PAGE
        )) {
        return;
    }

    const lv_coord_t width = static_cast<lv_coord_t>(
        surface.x2 - surface.x1 + 1
    );
    const lv_coord_t cellWidth = static_cast<lv_coord_t>(
        (width - DRUM_LABEL_WIDTH) /
        core::state::sequencer::DrumSequencerState::STEPS_PER_PAGE
    );
    const lv_coord_t gridStart = static_cast<lv_coord_t>(
        surface.x1 + DRUM_LABEL_WIDTH
    );
    const lv_coord_t laneHeight = drumLaneHeight(surface);
    const lv_coord_t cellX = static_cast<lv_coord_t>(
        gridStart + (step - pageStart) * cellWidth
    );
    const lv_area_t cellArea{
        .x1 = cellX,
        .y1 = rowY,
        .x2 = static_cast<lv_coord_t>(cellX + cellWidth - 1),
        .y2 = static_cast<lv_coord_t>(rowY + laneHeight - 1),
    };
    includeDamage(damage, hasDamage, cellArea);
}

FLASHMEM void DrumOverviewSurface::includeResolvedCellDamage(
    uint8_t row,
    uint8_t column,
    const lv_area_t& surface,
    lv_area_t& damage,
    bool& hasDamage
) {
    if (!root_ ||
        row >= core::state::sequencer::DrumResolvedPageProjection::VISIBLE_LANES ||
        column >= core::state::sequencer::DrumResolvedPageProjection::STEPS_PER_PAGE) {
        return;
    }
    const lv_coord_t width = static_cast<lv_coord_t>(
        surface.x2 - surface.x1 + 1
    );
    const lv_coord_t cellWidth = static_cast<lv_coord_t>(
        (width - DRUM_LABEL_WIDTH) /
        core::state::sequencer::DrumSequencerState::STEPS_PER_PAGE
    );
    const lv_coord_t laneHeight = drumLaneHeight(surface);
    const lv_coord_t cellX = static_cast<lv_coord_t>(
        surface.x1 + DRUM_LABEL_WIDTH + column * cellWidth
    );
    const lv_coord_t rowY = static_cast<lv_coord_t>(
        surface.y1 + row * laneHeight
    );
    includeDamage(
        damage,
        hasDamage,
        lv_area_t{
            .x1 = cellX,
            .y1 = rowY,
            .x2 = static_cast<lv_coord_t>(cellX + cellWidth - 1),
            .y2 = static_cast<lv_coord_t>(rowY + laneHeight - 1),
        }
    );
}

FLASHMEM void DrumOverviewSurface::invalidatePlaybackDelta(
    const PlaybackSnapshot& previous,
    const PlaybackSnapshot& next
) {
    if (!root_ || !renderedProps_.projection) return;

    lv_area_t surface{};
    lv_obj_get_coords(root_, &surface);
    const lv_coord_t laneHeight = drumLaneHeight(surface);
    const auto& projection = *renderedProps_.projection;
    const uint8_t laneCount = std::min<uint8_t>(
        projection.drumTrack->kit.laneCount,
        core::state::sequencer::DRUM_MAX_LANES
    );
    const uint8_t visibleRowCount = projection.laneWindowStart < laneCount
        ? std::min<uint8_t>(
              static_cast<uint8_t>(laneCount - projection.laneWindowStart),
              core::state::sequencer::DrumSequencerState::
                  VISIBLE_LANE_COUNT
          )
        : 0U;

    for (uint8_t row = 0U; row < visibleRowCount; ++row) {
        const uint8_t lane = projection.visibleLane(row);
        if (lane >= RUNTIME_LANE_CAPACITY) continue;
        const uint16_t laneBit = static_cast<uint16_t>(1U << lane);
        const bool previousChance =
            (previous.chanceDecisionValidMask & laneBit) != 0U;
        const bool nextChance =
            (next.chanceDecisionValidMask & laneBit) != 0U;
        const bool chanceChanged =
            previousChance != nextChance ||
            (previousChance && nextChance &&
             (previous.chanceDecisionSteps[lane] !=
                  next.chanceDecisionSteps[lane] ||
              ((previous.chanceDecisionPlayedMask ^
                next.chanceDecisionPlayedMask) & laneBit) != 0U));

        const lv_coord_t rowY = static_cast<lv_coord_t>(
            surface.y1 + row * laneHeight
        );
        lv_area_t damage{};
        bool hasDamage = false;
        includePlayheadDamage(
            previous, next, lane, rowY, surface, damage, hasDamage
        );
        if (chanceChanged) {
            includeChanceCellDamage(
                previous, lane, rowY, surface, damage, hasDamage
            );
            includeChanceCellDamage(
                next, lane, rowY, surface, damage, hasDamage
            );
        }

        for (uint8_t column = 0U;
             column < core::state::sequencer::DrumResolvedPageProjection::
                 STEPS_PER_PAGE;
             ++column) {
            const std::size_t cell =
                core::state::sequencer::DrumResolvedPageProjection::cellIndex(
                    row,
                    column
                );
            const uint64_t cellBit =
                core::state::sequencer::DrumResolvedPageProjection::cellBit(
                    row,
                    column
                );
            const bool previousValid =
                (previous.resolvedPage.validMask & cellBit) != 0U;
            const bool nextValid =
                (next.resolvedPage.validMask & cellBit) != 0U;
            const bool advancedContentChanged =
                ((previous.resolvedPage.cyclePresentMask ^
                  next.resolvedPage.cyclePresentMask) & cellBit) != 0U ||
                previous.resolvedPage.microMask[cell] !=
                    next.resolvedPage.microMask[cell] ||
                previous.resolvedPage.microLength[cell] !=
                    next.resolvedPage.microLength[cell];
            const bool resolvedChanged =
                previous.resolvedPage.contextKey !=
                    next.resolvedPage.contextKey ||
                advancedContentChanged ||
                previousValid != nextValid ||
                ((previous.resolvedPage.playedMask ^
                  next.resolvedPage.playedMask) & cellBit) != 0U ||
                (previousValid && nextValid &&
                 (previous.resolvedPage.velocity[cell] !=
                      next.resolvedPage.velocity[cell] ||
                   previous.resolvedPage.gate[cell] !=
                       next.resolvedPage.gate[cell] ||
                   previous.resolvedPage.nudge[cell] !=
                       next.resolvedPage.nudge[cell]));
            if (resolvedChanged) {
                includeResolvedCellDamage(
                    row, column, surface, damage, hasDamage
                );
            }
        }
        if (hasDamage) {
            oc::ui::lvgl::invalidateStaticSurfaceArea(root_, damage);
        }
    }
}

FLASHMEM void DrumOverviewSurface::render(
    const DrumOverviewSurfaceProps& props
) {
    if (!root_) return;
    if (!props.visible || !props.projection ||
        !props.projection->gridVisible()) {
        if (visible_) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
            visible_ = false;
        }
        rendered_ = false;
        renderedProps_ = {};
        playback_ = {};
        hideFocusedLaneName();
        return;
    }

    if (!visible_) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
        visible_ = true;
        rendered_ = false;
    }

    const bool staticChanged = staticVisualChanged(props);
    renderedProps_ = props;
    syncFocusedLaneName(props);

    if (staticChanged) {
        if (props.navigationFocus ==
            core::state::StructureNavigationFocus::TRACK) {
            playback_ = {};
        } else {
            playback_ = capturePlayback(*props.projection);
        }
        lv_obj_invalidate(root_);
    } else if (props.navigationFocus !=
               core::state::StructureNavigationFocus::TRACK) {
        const PlaybackSnapshot nextPlayback =
            capturePlayback(*props.projection);
        invalidatePlaybackDelta(playback_, nextPlayback);
        playback_ = nextPlayback;
    }

    rendered_ = true;
}

FLASHMEM void DrumOverviewSurface::drawSurface(
    lv_layer_t* layer
) {
    if (!layer || !root_) return;
    const auto& drumUi = *renderedProps_.projection;
    if (!drumUi.gridVisible()) return;

    lv_area_t surface{};
    lv_obj_get_coords(root_, &surface);
    const lv_coord_t width = static_cast<lv_coord_t>(
        surface.x2 - surface.x1 + 1
    );
    if (width <= DRUM_LABEL_WIDTH) return;

    const lv_coord_t laneHeight = drumLaneHeight(surface);
    const lv_coord_t cellWidth = static_cast<lv_coord_t>(
        (width - DRUM_LABEL_WIDTH) /
        core::state::sequencer::DrumSequencerState::STEPS_PER_PAGE
    );
    const uint8_t laneCount = std::min<uint8_t>(
        drumUi.drumTrack->kit.laneCount,
        core::state::sequencer::DRUM_MAX_LANES
    );
    if (renderedProps_.navigationFocus ==
        core::state::StructureNavigationFocus::TRACK) {
        drawTrackOverview(
            layer,
            surface,
            width,
            drumUi,
            laneCount,
            renderedProps_.midiChannel
        );
        return;
    }

    const lv_coord_t gridStart = static_cast<lv_coord_t>(
        surface.x1 + DRUM_LABEL_WIDTH
    );
    const lv_coord_t gridEnd = static_cast<lv_coord_t>(
        gridStart +
        cellWidth *
            core::state::sequencer::DrumSequencerState::STEPS_PER_PAGE
    );
    drawDrumLaneGrid(
        DrumGridRenderContext{
            .layer = layer,
            .drumUi = drumUi,
            .surface = surface,
            .focus = renderedProps_.navigationFocus,
            .laneHeight = laneHeight,
            .cellWidth = cellWidth,
            .gridStart = gridStart,
            .gridEnd = gridEnd,
            .laneCount = laneCount,
            .pageStart = static_cast<uint8_t>(
                drumUi.page *
                core::state::sequencer::DrumSequencerState::STEPS_PER_PAGE
            ),
            .focusedLaneNameVisible = focused_lane_name_visible_,
            .focusedLaneNameLane = focused_lane_name_lane_,
        }
    );
}

}  // namespace core::ui::sequencer

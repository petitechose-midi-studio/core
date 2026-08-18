#include "StepGrid.hpp"

#include <algorithm>
#include <cstddef>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/type/TextFormat.hpp>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/ui/lvgl/StaticSurfaceInvalidation.hpp>
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepGridDataPalette.hpp"
#include "ui/sequencer/StepGridGeometryLogic.hpp"
#include "ui/sequencer/StepGridLabelRenderer.hpp"
#include "ui/sequencer/StepGridRenderLogic.hpp"
#include "ui/sequencer/StepGridRenderPlanner.hpp"
#include "ui/sequencer/StepGridWidgets.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace theme = oc::ui::lvgl::base_theme;
namespace grid = core::ui::sequencer::grid;

namespace core::ui {

namespace {

constexpr uint32_t PLAYHEAD_ACTIVE_COLOR =
    ::standalone::theme::color::LIVE_TIME;
constexpr uint32_t PLAYHEAD_INACTIVE_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_coord_t STEP_BOTTOM_RESERVED_HEIGHT = grid::STEP_BOTTOM_RESERVED_HEIGHT;
constexpr lv_opa_t PLAYHEAD_ACTIVE_OPA = LV_OPA_COVER;
constexpr lv_opa_t PLAYHEAD_INACTIVE_OPA = LV_OPA_70;
constexpr uint32_t STEP_INDEX_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_INDEX_OPA = LV_OPA_60;
constexpr uint32_t STEP_GUIDE_COLOR = theme::color::INACTIVE_LIGHTER;
constexpr lv_opa_t STEP_GUIDE_OPA = LV_OPA_20;
constexpr lv_coord_t STEP_GUIDE_WIDTH = 1;
constexpr uint8_t STEP_GUIDE_COUNT = 3;
constexpr uint32_t STEP_SELECTION_CURSOR_COLOR =
    ::standalone::theme::color::FOCUS_EDIT;
constexpr uint32_t STEP_SELECTION_SELECTED_COLOR =
    ::standalone::theme::color::CONTENT_ACTIVE;
constexpr uint32_t STEP_SELECTION_EMPTY_COLOR = grid::palette::SELECTION_EMPTY;
constexpr uint32_t STEP_SELECTION_GHOST_COLOR = grid::palette::SELECTION_GHOST;
constexpr uint32_t STEP_SELECTION_OVERWRITE_COLOR =
    grid::palette::SELECTION_OVERWRITE;
constexpr uint32_t STEP_SELECTION_BLOCKED_COLOR =
    grid::palette::SELECTION_BLOCKED;
constexpr lv_opa_t STEP_SELECTION_SELECTED_OPA = LV_OPA_20;
constexpr lv_opa_t STEP_SELECTION_PREVIEW_OPA = LV_OPA_30;
constexpr lv_coord_t STEP_SELECTION_SELECTED_BORDER = 1;
constexpr lv_coord_t STEP_SELECTION_BORDER_OUTSET = 2;
constexpr lv_coord_t STEP_INDEX_RIGHT_PAD = 4;
constexpr lv_coord_t STEP_INDEX_TOP_PAD = 2;
constexpr lv_coord_t STEP_BADGE_SIZE = 12;
constexpr lv_coord_t STEP_BADGE_GAP = 2;
constexpr lv_coord_t STEP_BADGE_LEFT_PAD = 4;
constexpr lv_coord_t STEP_BADGE_TOP_PAD = 3;
constexpr lv_coord_t PHASE_RAIL_HORIZONTAL_PAD = 4;
constexpr lv_coord_t PHASE_RAIL_BOTTOM_PAD = 2;
constexpr lv_coord_t PHASE_RAIL_HEIGHT = 2;
constexpr lv_coord_t PHASE_RAIL_GAP = 1;
constexpr lv_opa_t PHASE_RAIL_INACTIVE_OPA = LV_OPA_30;
constexpr lv_opa_t PHASE_RAIL_ACTIVE_OPA = LV_OPA_COVER;
constexpr uint32_t MICRO_SEQUENCE_BADGE_COLOR =
    sequencer::semantic::color(sequencer::semantic::Tone::MICRO_SEQUENCE);
constexpr uint32_t CYCLE_STATE_BADGE_COLOR =
    sequencer::semantic::color(sequencer::semantic::Tone::CYCLE_STATE);
constexpr uint32_t CHORD_BADGE_COLOR =
    sequencer::semantic::color(sequencer::semantic::Tone::CHORD);
constexpr uint32_t PROBABILITY_BADGE_COLOR =
    sequencer::semantic::color(sequencer::semantic::Tone::CHANCE);
constexpr uint32_t EXPANSION_LIMIT_BADGE_COLOR =
    ::standalone::theme::color::STEP_PITCH;
constexpr lv_opa_t STEP_BADGE_OPA = LV_OPA_COVER;
constexpr lv_opa_t STEP_BADGE_DISABLED_OPA = LV_OPA_50;
constexpr lv_coord_t NOTE_RAIL_HEIGHT = 3;
constexpr lv_coord_t NOTE_RAIL_HORIZONTAL_PAD = 0;
constexpr lv_coord_t NOTE_GHOST_DASH = 3;
constexpr lv_coord_t NOTE_GHOST_GAP = 2;
constexpr lv_coord_t NOTE_HEAD_WIDTH = 3;
constexpr lv_coord_t PITCH_OVERFLOW_EDGE_PAD = 8;
constexpr lv_coord_t PITCH_OVERFLOW_LANE_GAP = 3;
constexpr int32_t GRID_ROW_WRAP_Q8 = 4 * 256;
constexpr lv_coord_t SCALE_RULER_X_PAD = 1;
constexpr lv_opa_t SCALE_RULER_CHROMATIC_OPA = LV_OPA_20;
constexpr lv_opa_t SCALE_RULER_SCALE_OPA = LV_OPA_40;
constexpr lv_opa_t SCALE_RULER_ROOT_OPA = LV_OPA_70;
constexpr lv_opa_t SCALE_RULER_EDIT_CHROMATIC_OPA = LV_OPA_30;
constexpr lv_opa_t SCALE_RULER_EDIT_SCALE_OPA = LV_OPA_60;
constexpr lv_opa_t SCALE_RULER_EDIT_ROOT_OPA = LV_OPA_90;
constexpr lv_coord_t PLAYHEAD_TOP_PAD = 12;
constexpr lv_coord_t PLAYHEAD_BOTTOM_PAD = 5;
constexpr lv_coord_t SELECTION_CORNER_LENGTH = 7;
constexpr lv_coord_t SELECTION_CORNER_THICKNESS = 2;

FLASHMEM uint32_t chordBadgeColor(
    oc::note::sequencer::StepSequencerChordSource source
) {
    using Source = oc::note::sequencer::StepSequencerChordSource;
    if (source == Source::Inherited) {
        return sequencer::semantic::color(sequencer::semantic::Tone::CHORD_MODE);
    }
    return CHORD_BADGE_COLOR;
}

FLASHMEM void drawVariationRect(lv_layer_t* layer,
                       lv_coord_t x,
                       lv_coord_t y,
                       lv_coord_t width,
                       lv_coord_t height,
                       uint32_t colorHex,
                       lv_opa_t opa) {
    if (!layer || width <= 0 || height <= 0 || opa == LV_OPA_TRANSP) return;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(colorHex);
    dsc.bg_opa = opa;
    dsc.radius = 0;
    dsc.border_width = 0;

    const lv_area_t area{
        .x1 = x,
        .y1 = y,
        .x2 = static_cast<lv_coord_t>(x + width - 1),
        .y2 = static_cast<lv_coord_t>(y + height - 1),
    };
    lv_draw_rect(layer, &dsc, &area);
}

FLASHMEM uint32_t stepPastePreviewColor(
    core::state::sequencer::SequencerStepPastePreview preview
) {
    switch (preview) {
        case core::state::sequencer::SequencerStepPastePreview::EMPTY:
            return STEP_SELECTION_EMPTY_COLOR;
        case core::state::sequencer::SequencerStepPastePreview::GHOST:
            return STEP_SELECTION_GHOST_COLOR;
        case core::state::sequencer::SequencerStepPastePreview::OVERWRITE:
            return STEP_SELECTION_OVERWRITE_COLOR;
        case core::state::sequencer::SequencerStepPastePreview::BLOCKED:
            return STEP_SELECTION_BLOCKED_COLOR;
        case core::state::sequencer::SequencerStepPastePreview::NONE:
        default:
            return STEP_SELECTION_CURSOR_COLOR;
    }
}

FLASHMEM void drawSelectionOverlay(lv_layer_t* layer,
                          const lv_area_t& buttonArea,
                          const grid::TileRenderCache& cache) {
    if (!layer || !cache.stepSelectionActive) return;

    if (cache.stepPastePreviewActive) {
        const uint32_t color = stepPastePreviewColor(cache.stepPastePreview);
        lv_draw_rect_dsc_t fillDsc;
        lv_draw_rect_dsc_init(&fillDsc);
        fillDsc.bg_color = lv_color_hex(color);
        fillDsc.bg_opa = STEP_SELECTION_PREVIEW_OPA;
        fillDsc.radius = 0;
        fillDsc.border_width = 0;
        lv_draw_rect(layer, &fillDsc, &buttonArea);
    } else if (cache.stepSelectionSelected) {
        const uint32_t color = STEP_SELECTION_SELECTED_COLOR;
        lv_draw_rect_dsc_t fillDsc;
        lv_draw_rect_dsc_init(&fillDsc);
        fillDsc.bg_color = lv_color_hex(color);
        fillDsc.bg_opa = STEP_SELECTION_SELECTED_OPA;
        fillDsc.radius = 0;
        fillDsc.border_width = 0;
        lv_draw_rect(layer, &fillDsc, &buttonArea);
    }

    lv_area_t borderArea = buttonArea;
    borderArea.x1 = static_cast<lv_coord_t>(borderArea.x1 - STEP_SELECTION_BORDER_OUTSET);
    borderArea.y1 = static_cast<lv_coord_t>(borderArea.y1 - STEP_SELECTION_BORDER_OUTSET);
    borderArea.x2 = static_cast<lv_coord_t>(borderArea.x2 + STEP_SELECTION_BORDER_OUTSET);
    borderArea.y2 = static_cast<lv_coord_t>(borderArea.y2 + STEP_SELECTION_BORDER_OUTSET);

    if (cache.stepSelectionSelected) {
        lv_draw_rect_dsc_t borderDsc;
        lv_draw_rect_dsc_init(&borderDsc);
        borderDsc.bg_opa = LV_OPA_TRANSP;
        borderDsc.radius = 0;
        borderDsc.border_width = STEP_SELECTION_SELECTED_BORDER;
        borderDsc.border_color = lv_color_hex(STEP_SELECTION_SELECTED_COLOR);
        borderDsc.border_opa = LV_OPA_80;
        lv_draw_rect(layer, &borderDsc, &borderArea);
    }

    if (cache.stepSelectionCursor) {
        const uint32_t color = cache.stepPastePreviewActive
            ? stepPastePreviewColor(cache.stepPastePreview)
            : STEP_SELECTION_CURSOR_COLOR;
        // Keep focus corners inside the tile clip. Selection borders may extend
        // beyond it, but a cursor must always remain fully legible.
        lv_area_t cursorArea = buttonArea;
        cursorArea.y2 = static_cast<lv_coord_t>(
            std::max<lv_coord_t>(
                cursorArea.y1,
                static_cast<lv_coord_t>(
                    buttonArea.y2 - STEP_BOTTOM_RESERVED_HEIGHT - 1
                )
            )
        );
        const lv_coord_t width = lv_area_get_width(&cursorArea);
        const lv_coord_t height = lv_area_get_height(&cursorArea);
        const lv_coord_t corner = std::min<lv_coord_t>(
            SELECTION_CORNER_LENGTH,
            std::max<lv_coord_t>(1, std::min(width, height) / 2)
        );
        drawVariationRect(
            layer, cursorArea.x1, cursorArea.y1,
            corner, SELECTION_CORNER_THICKNESS, color, LV_OPA_COVER
        );
        drawVariationRect(
            layer, cursorArea.x1, cursorArea.y1,
            SELECTION_CORNER_THICKNESS, corner, color, LV_OPA_COVER
        );
        drawVariationRect(
            layer,
            static_cast<lv_coord_t>(cursorArea.x2 - corner + 1),
            cursorArea.y1,
            corner,
            SELECTION_CORNER_THICKNESS,
            color,
            LV_OPA_COVER
        );
        drawVariationRect(
            layer,
            static_cast<lv_coord_t>(cursorArea.x2 - SELECTION_CORNER_THICKNESS + 1),
            cursorArea.y1,
            SELECTION_CORNER_THICKNESS,
            corner,
            color,
            LV_OPA_COVER
        );
        drawVariationRect(
            layer,
            cursorArea.x1,
            static_cast<lv_coord_t>(cursorArea.y2 - SELECTION_CORNER_THICKNESS + 1),
            corner,
            SELECTION_CORNER_THICKNESS,
            color,
            LV_OPA_COVER
        );
        drawVariationRect(
            layer,
            cursorArea.x1,
            static_cast<lv_coord_t>(cursorArea.y2 - corner + 1),
            SELECTION_CORNER_THICKNESS,
            corner,
            color,
            LV_OPA_COVER
        );
        drawVariationRect(
            layer,
            static_cast<lv_coord_t>(cursorArea.x2 - corner + 1),
            static_cast<lv_coord_t>(cursorArea.y2 - SELECTION_CORNER_THICKNESS + 1),
            corner,
            SELECTION_CORNER_THICKNESS,
            color,
            LV_OPA_COVER
        );
        drawVariationRect(
            layer,
            static_cast<lv_coord_t>(cursorArea.x2 - SELECTION_CORNER_THICKNESS + 1),
            static_cast<lv_coord_t>(cursorArea.y2 - corner + 1),
            SELECTION_CORNER_THICKNESS,
            corner,
            color,
            LV_OPA_COVER
        );
    }
}

FLASHMEM lv_coord_t drawStepBadgeGlyph(lv_layer_t* layer,
                              const lv_area_t& buttonArea,
                              lv_coord_t x,
                              lv_coord_t y,
                              const char* icon,
                              uint32_t colorHex,
                              bool enabled) {
    if (!layer || icon == nullptr || icon[0] == '\0') return x;
    if (x + STEP_BADGE_SIZE - 1 > buttonArea.x2) return x;

    const lv_area_t area{
        .x1 = x,
        .y1 = y,
        .x2 = static_cast<lv_coord_t>(x + STEP_BADGE_SIZE - 1),
        .y2 = static_cast<lv_coord_t>(y + STEP_BADGE_SIZE - 1),
    };

    lv_draw_label_dsc_t labelDsc;
    lv_draw_label_dsc_init(&labelDsc);
    labelDsc.text = icon;
    labelDsc.font = standalone_fonts.icons_12;
    labelDsc.color = lv_color_hex(colorHex);
    labelDsc.opa = enabled ? STEP_BADGE_OPA : STEP_BADGE_DISABLED_OPA;
    labelDsc.align = LV_TEXT_ALIGN_CENTER;
    lv_draw_label(layer, &labelDsc, &area);
    return static_cast<lv_coord_t>(x + STEP_BADGE_SIZE + STEP_BADGE_GAP);
}

FLASHMEM void drawSemanticBadges(lv_layer_t* layer,
                        const lv_area_t& buttonArea,
                        const grid::TileRenderCache& cache) {
    const bool probabilityBadge = cache.enabled && cache.probability < 100;
    const bool chordBadge = cache.contentBadges.chord &&
                            cache.contentBadges.chordVoiceCount > 1;
    if (!chordBadge &&
        !cache.contentBadges.expansionLimitReached &&
        !probabilityBadge) {
        return;
    }

    lv_coord_t x = static_cast<lv_coord_t>(buttonArea.x1 + STEP_BADGE_LEFT_PAD);
    const lv_coord_t y = static_cast<lv_coord_t>(buttonArea.y1 + STEP_BADGE_TOP_PAD);
    if (cache.contentBadges.expansionLimitReached) {
        x = drawStepBadgeGlyph(
            layer,
            buttonArea,
            x,
            y,
            standalone::icons::STATUS_WARNING,
            EXPANSION_LIMIT_BADGE_COLOR,
            cache.enabled
        );
    }
    if (chordBadge) {
        x = drawStepBadgeGlyph(
            layer,
            buttonArea,
            x,
            y,
            standalone::icons::CHORD,
            chordBadgeColor(cache.contentBadges.chordSource),
            cache.enabled
        );
    }
    if (probabilityBadge) {
        drawStepBadgeGlyph(
            layer,
            buttonArea,
            x,
            y,
            standalone::icons::NOTE_PROP_RANDOM,
            PROBABILITY_BADGE_COLOR,
            cache.enabled
        );
    }
}

FLASHMEM void drawPhaseRail(lv_layer_t* layer,
                   const lv_area_t& buttonArea,
                   uint8_t length,
                   uint16_t activeMask,
                   bool cursorVisible,
                   uint8_t cursor,
                   lv_coord_t bottomOffset,
                   uint32_t activeColor) {
    length = std::min<uint8_t>(length, 16U);
    if (!layer || length == 0U) return;

    const lv_coord_t railX = static_cast<lv_coord_t>(
        buttonArea.x1 + PHASE_RAIL_HORIZONTAL_PAD
    );
    const lv_coord_t railY = static_cast<lv_coord_t>(
        buttonArea.y2 - STEP_BOTTOM_RESERVED_HEIGHT - PHASE_RAIL_BOTTOM_PAD -
        bottomOffset - PHASE_RAIL_HEIGHT + 1
    );
    const lv_coord_t availableWidth = std::max<lv_coord_t>(
        1,
        static_cast<lv_coord_t>(
            lv_area_get_width(&buttonArea) - 2 * PHASE_RAIL_HORIZONTAL_PAD
        )
    );
    const lv_coord_t totalGap = static_cast<lv_coord_t>(
        (length - 1U) * PHASE_RAIL_GAP
    );
    const lv_coord_t segmentWidth = std::max<lv_coord_t>(
        1,
        static_cast<lv_coord_t>((availableWidth - totalGap) / length)
    );
    const lv_coord_t usedWidth = static_cast<lv_coord_t>(
        segmentWidth * length + totalGap
    );
    const lv_coord_t centeredX = static_cast<lv_coord_t>(
        railX + std::max<lv_coord_t>(0, (availableWidth - usedWidth) / 2)
    );

    for (uint8_t index = 0U; index < length; ++index) {
        const bool active =
            (activeMask &
             static_cast<uint16_t>(1U << index)) != 0U;
        const lv_coord_t segmentX = static_cast<lv_coord_t>(
            centeredX + index * (segmentWidth + PHASE_RAIL_GAP)
        );
        drawVariationRect(
            layer,
            segmentX,
            railY,
            segmentWidth,
            PHASE_RAIL_HEIGHT,
            active ? activeColor : theme::color::INACTIVE_LIGHTER,
            active ? PHASE_RAIL_ACTIVE_OPA : PHASE_RAIL_INACTIVE_OPA
        );
        if (cursorVisible && cursor == index) {
            drawVariationRect(
                layer,
                segmentX,
                static_cast<lv_coord_t>(railY - 2),
                segmentWidth,
                1,
                PLAYHEAD_ACTIVE_COLOR,
                LV_OPA_COVER
            );
        }
    }
}

FLASHMEM void drawMicroRail(lv_layer_t* layer,
                   const lv_area_t& buttonArea,
                   const grid::TileRenderCache& cache) {
    drawPhaseRail(
        layer,
        buttonArea,
        cache.contentBadges.microLength,
        cache.contentBadges.microActiveMask,
        cache.contentBadges.microCursorVisible,
        cache.contentBadges.microCursor,
        0,
        MICRO_SEQUENCE_BADGE_COLOR
    );
}

FLASHMEM void drawCycleRail(lv_layer_t* layer,
                   const lv_area_t& buttonArea,
                   const grid::TileRenderCache& cache) {
    drawPhaseRail(
        layer,
        buttonArea,
        cache.contentBadges.cycleLength,
        cache.contentBadges.cycleActiveMask,
        cache.contentBadges.cycleCursorVisible,
        cache.contentBadges.cycleCursor,
        static_cast<lv_coord_t>(PHASE_RAIL_HEIGHT + 2),
        CYCLE_STATE_BADGE_COLOR
    );
}

FLASHMEM void drawDashedRail(lv_layer_t* layer,
                    lv_coord_t x,
                    lv_coord_t y,
                    lv_coord_t width,
                    uint32_t color) {
    lv_coord_t cursor = x;
    const lv_coord_t end = static_cast<lv_coord_t>(x + width);
    while (cursor < end) {
        const lv_coord_t dashWidth = std::min<lv_coord_t>(
            NOTE_GHOST_DASH,
            static_cast<lv_coord_t>(end - cursor)
        );
        drawVariationRect(
            layer,
            cursor,
            y,
            dashWidth,
            NOTE_RAIL_HEIGHT,
            color,
            LV_OPA_60
        );
        cursor = static_cast<lv_coord_t>(
            cursor + NOTE_GHOST_DASH + NOTE_GHOST_GAP
        );
    }
}

FLASHMEM void drawHollowHead(lv_layer_t* layer,
                    lv_coord_t x,
                    lv_coord_t y,
                    uint8_t height,
                    uint32_t color) {
    const lv_coord_t top = static_cast<lv_coord_t>(y - height / 2);
    drawVariationRect(layer, x, top, NOTE_HEAD_WIDTH, 1, color, LV_OPA_70);
    drawVariationRect(
        layer,
        x,
        static_cast<lv_coord_t>(top + height - 1),
        NOTE_HEAD_WIDTH,
        1,
        color,
        LV_OPA_70
    );
    drawVariationRect(layer, x, top, 1, height, color, LV_OPA_70);
    drawVariationRect(
        layer,
        static_cast<lv_coord_t>(x + NOTE_HEAD_WIDTH - 1),
        top,
        1,
        height,
        color,
        LV_OPA_70
    );
}

FLASHMEM void drawPitchOverflowMarker(
    lv_layer_t* layer,
    lv_coord_t x,
    lv_coord_t railY,
    grid::StepPitchOverflow overflow,
    uint32_t color,
    bool active,
    bool dense
) {
    if (overflow == grid::StepPitchOverflow::NONE) return;
    const lv_opa_t opa = active ? LV_OPA_COVER : LV_OPA_60;
    const lv_coord_t centerY = static_cast<lv_coord_t>(
        railY + NOTE_RAIL_HEIGHT / 2
    );
    const lv_coord_t tipY = overflow == grid::StepPitchOverflow::ABOVE
        ? static_cast<lv_coord_t>(centerY - 2)
        : static_cast<lv_coord_t>(centerY + 2);
    const lv_coord_t shoulderY = overflow == grid::StepPitchOverflow::ABOVE
        ? static_cast<lv_coord_t>(centerY - 1)
        : static_cast<lv_coord_t>(centerY + 1);
    drawVariationRect(layer, x, shoulderY, 3, 1, color, opa);
    drawVariationRect(
        layer,
        static_cast<lv_coord_t>(x + 1),
        tipY,
        1,
        1,
        color,
        opa
    );
    if (dense) {
        drawVariationRect(
            layer,
            static_cast<lv_coord_t>(x + 4),
            shoulderY,
            1,
            1,
            color,
            LV_OPA_70
        );
    }
}

FLASHMEM void drawRowWrapMarker(
    lv_layer_t* layer,
    const lv_area_t& area,
    lv_coord_t railY,
    bool exitsRow,
    uint32_t color,
    bool active
) {
    const lv_coord_t edgeX = exitsRow ? area.x2 : area.x1;
    const lv_coord_t innerX = static_cast<lv_coord_t>(
        edgeX + (exitsRow ? -1 : 1)
    );
    const lv_opa_t opa = active ? LV_OPA_80 : LV_OPA_50;
    drawVariationRect(
        layer,
        innerX,
        static_cast<lv_coord_t>(railY - 1),
        1,
        1,
        color,
        opa
    );
    drawVariationRect(
        layer,
        edgeX,
        railY,
        1,
        NOTE_RAIL_HEIGHT,
        color,
        opa
    );
    drawVariationRect(
        layer,
        innerX,
        static_cast<lv_coord_t>(railY + NOTE_RAIL_HEIGHT),
        1,
        1,
        color,
        opa
    );
}

FLASHMEM void drawScaleRuler(
    lv_layer_t* layer,
    const lv_area_t& buttonArea,
    uint8_t tileIndex,
    grid::StepGridPresentation presentation,
    const grid::StepPitchViewport& viewport,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    bool pitchEditing
) {
    if (!layer || presentation != grid::StepGridPresentation::MELODIC ||
        (tileIndex % 4U) != 0U) {
        return;
    }
    scaleSettings.clamp();
    const lv_coord_t buttonHeight = lv_area_get_height(&buttonArea);
    const uint16_t high = std::min<uint16_t>(
        127U,
        static_cast<uint16_t>(viewport.lowNote) + viewport.semitoneSpan
    );
    lv_coord_t lastY = -1;
    for (uint16_t note = viewport.lowNote; note <= high; ++note) {
        const lv_coord_t y = static_cast<lv_coord_t>(
            buttonArea.y1 + grid::stepPitchY(
                static_cast<uint8_t>(note),
                viewport,
                buttonHeight
            )
        );
        const bool root = (note % 12U) == scaleSettings.root;
        const bool inScale = oc::note::sequencer::scaleContainsNote(
            scaleSettings,
            static_cast<uint8_t>(note)
        );
        if (y == lastY && !root) continue;
        lastY = y;
        drawVariationRect(
            layer,
            static_cast<lv_coord_t>(buttonArea.x1 + SCALE_RULER_X_PAD),
            y,
            root ? (pitchEditing ? 5 : 4)
                 : (inScale ? (pitchEditing ? 3 : 2) : 1),
            1,
            theme::color::TEXT_PRIMARY,
            root ? (pitchEditing ? SCALE_RULER_EDIT_ROOT_OPA
                                 : SCALE_RULER_ROOT_OPA)
                 : (inScale
                        ? (pitchEditing ? SCALE_RULER_EDIT_SCALE_OPA
                                        : SCALE_RULER_SCALE_OPA)
                        : (pitchEditing
                               ? SCALE_RULER_EDIT_CHROMATIC_OPA
                               : SCALE_RULER_CHROMATIC_OPA))
        );
    }
}

FLASHMEM void drawTileNoteEvents(
    lv_layer_t* layer,
    const lv_area_t& buttonArea,
    uint8_t tileIndex,
    const std::array<grid::TileRenderCache, 8>& tiles,
    grid::StepGridPresentation presentation,
    uint32_t accentColor,
    const grid::StepPitchViewport& viewport,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    bool pitchEditing
) {
    if (!layer || tileIndex >= tiles.size()) return;

    const int32_t destinationStartQ8 = static_cast<int32_t>(tileIndex) * 256;
    const int32_t destinationEndQ8 = destinationStartQ8 + 256;
    const lv_coord_t areaWidth = lv_area_get_width(&buttonArea);
    const lv_coord_t areaHeight = lv_area_get_height(&buttonArea);
    const lv_coord_t usableWidth = std::max<lv_coord_t>(
        1,
        static_cast<lv_coord_t>(areaWidth - 2 * NOTE_RAIL_HORIZONTAL_PAD)
    );
    const lv_coord_t baseX = static_cast<lv_coord_t>(
        buttonArea.x1 + NOTE_RAIL_HORIZONTAL_PAD
    );
    uint8_t aboveOverflowCount = 0U;
    uint8_t belowOverflowCount = 0U;
    scaleSettings.clamp();

    for (uint8_t sourceTile = 0U; sourceTile < tiles.size(); ++sourceTile) {
        const auto& source = tiles[sourceTile];
        if (!source.inPattern || !source.enabled) continue;
        for (uint8_t eventIndex = 0U;
             eventIndex < source.noteEvents.count;
             ++eventIndex) {
            const auto& event = source.noteEvents.events[eventIndex];
            const int32_t globalStartQ8 =
                static_cast<int32_t>(sourceTile) * 256 + event.startQ8;
            const int32_t globalEndQ8 = globalStartQ8 +
                std::max<uint16_t>(1U, event.spanQ8);
            const int32_t clippedStart = std::max<int32_t>(
                globalStartQ8,
                destinationStartQ8
            );
            const int32_t clippedEnd = std::min<int32_t>(
                globalEndQ8,
                destinationEndQ8
            );
            if (clippedEnd <= clippedStart) continue;

            const int32_t localStart = clippedStart - destinationStartQ8;
            const int32_t localEnd = clippedEnd - destinationStartQ8;
            const lv_coord_t x = static_cast<lv_coord_t>(
                baseX + (localStart * usableWidth) / 256
            );
            const lv_coord_t endX = static_cast<lv_coord_t>(
                baseX + (localEnd * usableWidth + 255) / 256
            );
            const lv_coord_t width = std::max<lv_coord_t>(
                1,
                static_cast<lv_coord_t>(endX - x)
            );
            const auto overflow =
                presentation == grid::StepGridPresentation::MELODIC
                    ? grid::stepPitchOverflow(event.note, viewport)
                    : grid::StepPitchOverflow::NONE;
            uint8_t overflowOrdinal = 0U;
            if (overflow == grid::StepPitchOverflow::ABOVE) {
                overflowOrdinal = aboveOverflowCount++;
            } else if (overflow == grid::StepPitchOverflow::BELOW) {
                overflowOrdinal = belowOverflowCount++;
            }
            const uint8_t overflowLane = static_cast<uint8_t>(
                overflowOrdinal % 2U
            );
            const lv_coord_t localY = presentation ==
                    grid::StepGridPresentation::DRUM_LANE
                ? static_cast<lv_coord_t>(areaHeight / 2)
                : (overflow == grid::StepPitchOverflow::ABOVE
                       ? static_cast<lv_coord_t>(
                             PITCH_OVERFLOW_EDGE_PAD +
                             overflowLane * PITCH_OVERFLOW_LANE_GAP
                         )
                       : (overflow == grid::StepPitchOverflow::BELOW
                              ? static_cast<lv_coord_t>(
                                    areaHeight - PITCH_OVERFLOW_EDGE_PAD -
                                    overflowLane * PITCH_OVERFLOW_LANE_GAP
                                )
                              : grid::stepPitchY(
                                    event.note,
                                    viewport,
                                    areaHeight
                                )));
            const lv_coord_t y = static_cast<lv_coord_t>(
                buttonArea.y1 + localY - NOTE_RAIL_HEIGHT / 2
            );
            const uint32_t color = lv_color_to_int(
                grid::stepEventAccentColor(accentColor, event.velocity)
            );
            const bool active = event.active != 0U;
            if (active) {
                drawVariationRect(
                    layer,
                    x,
                    y,
                    width,
                    NOTE_RAIL_HEIGHT,
                    color,
                    LV_OPA_COVER
                );
            } else {
                drawDashedRail(layer, x, y, width, color);
            }

            const bool crossesRowWrap =
                globalStartQ8 < GRID_ROW_WRAP_Q8 &&
                globalEndQ8 > GRID_ROW_WRAP_Q8;
            if (crossesRowWrap && (tileIndex == 3U || tileIndex == 4U)) {
                drawRowWrapMarker(
                    layer,
                    buttonArea,
                    y,
                    tileIndex == 3U,
                    color,
                    active
                );
            }

            const bool onsetInTile =
                globalStartQ8 >= destinationStartQ8 &&
                globalStartQ8 < destinationEndQ8;
            if (!onsetInTile) continue;
            const lv_coord_t onsetX = static_cast<lv_coord_t>(
                baseX +
                ((globalStartQ8 - destinationStartQ8) * usableWidth) / 256
            );
            const uint8_t headHeight = grid::stepEventHeadHeight(event.velocity);
            if (overflow != grid::StepPitchOverflow::NONE) {
                drawPitchOverflowMarker(
                    layer,
                    onsetX,
                    y,
                    overflow,
                    color,
                    active,
                    overflowOrdinal >= 2U
                );
                continue;
            }
            if (active) {
                drawVariationRect(
                    layer,
                    onsetX,
                    static_cast<lv_coord_t>(
                        y + NOTE_RAIL_HEIGHT / 2 - headHeight / 2
                    ),
                    NOTE_HEAD_WIDTH,
                    headHeight,
                    color,
                    LV_OPA_COVER
                );
            } else {
                drawHollowHead(
                    layer,
                    onsetX,
                    static_cast<lv_coord_t>(y + NOTE_RAIL_HEIGHT / 2),
                    std::max<uint8_t>(3U, headHeight),
                    color
                );
            }
            if (presentation == grid::StepGridPresentation::MELODIC &&
                pitchEditing &&
                scaleSettings.type !=
                    oc::note::sequencer::StepSequencerScaleType::Chromatic &&
                !oc::note::sequencer::scaleContainsNote(
                    scaleSettings,
                    event.note
                )) {
                drawVariationRect(
                    layer,
                    onsetX,
                    static_cast<lv_coord_t>(y - 2),
                    NOTE_HEAD_WIDTH,
                    1,
                    grid::palette::OUT_OF_SCALE,
                    LV_OPA_COVER
                );
            }
        }
    }
}

FLASHMEM void drawPlayheadLine(lv_layer_t* layer,
                      const lv_area_t& buttonArea,
                      const grid::TileRenderCache& cache) {
    if (!layer || !cache.playheadLineVisible ||
        cache.playheadLineOpa == LV_OPA_TRANSP) {
        return;
    }
    const lv_coord_t width = std::max<lv_coord_t>(
        1,
        static_cast<lv_coord_t>(
            lv_area_get_width(&buttonArea) - 2 * NOTE_RAIL_HORIZONTAL_PAD
        )
    );
    const lv_coord_t usableSpan = std::max<lv_coord_t>(0, width - 1);
    const lv_coord_t x = static_cast<lv_coord_t>(
        buttonArea.x1 + NOTE_RAIL_HORIZONTAL_PAD +
        (static_cast<uint16_t>(cache.playheadProgress) * usableSpan) / 255U
    );
    const lv_coord_t y = static_cast<lv_coord_t>(
        buttonArea.y1 + PLAYHEAD_TOP_PAD
    );
    const lv_coord_t height = std::max<lv_coord_t>(
        1,
        static_cast<lv_coord_t>(
            lv_area_get_height(&buttonArea) -
            PLAYHEAD_TOP_PAD - PLAYHEAD_BOTTOM_PAD
        )
    );
    drawVariationRect(
        layer,
        x,
        y,
        1,
        height,
        cache.playheadLineColorFull,
        cache.playheadLineOpa
    );
}

}  // namespace

FLASHMEM StepGrid::StepGrid(lv_obj_t* parent,
                            GeometryInvalidatedCallback geometryInvalidated,
                            void* geometryInvalidatedUserData)
    : geometry_invalidated_(geometryInvalidated)
    , geometry_invalidated_user_data_(geometryInvalidatedUserData) {
    createUI(parent);
    createTiles();
}

FLASHMEM StepGrid::~StepGrid() {
    if (container_) {
        lv_obj_delete(container_);
        container_ = nullptr;
        grid_ = nullptr;
        note_layer_ = nullptr;
    }
}

FLASHMEM void StepGrid::createUI(lv_obj_t* parent) {
    grid::widgets::createRoot(
        parent,
        container_,
        grid_,
        note_layer_,
        onGeometryChangedEvent,
        this
    );
}

FLASHMEM void StepGrid::createTiles() {
    geometry_.noteLabelHeight = grid::widgets::noteLabelHeight();

    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        grid::widgets::createTile(
            i,
            grid_,
            note_layer_,
            tiles_[i],
            note_labels_[i],
            secondary_labels_[i],
            step_inline_icons_[i],
            step_buttons_[i],
            geometry_.inlineIconWidth[i],
            geometry_.inlineIconHeight[i],
            onGeometryChangedEvent,
            this
        );
        tile_button_draw_contexts_[i] = TileButtonDrawContext{.grid = this, .tileIndex = i};
        if (step_buttons_[i]) {
            lv_obj_add_event_cb(
                step_buttons_[i],
                onTileButtonDrawEvent,
                LV_EVENT_DRAW_MAIN,
                &tile_button_draw_contexts_[i]
            );
        }
    }
}

FLASHMEM void StepGrid::invalidateTileCaches() {
    for (auto& cache : render_cache_.tiles) {
        cache.initialized = false;
    }
}

FLASHMEM void StepGrid::onGeometryChangedEvent(lv_event_t* event) {
    auto* self = static_cast<StepGrid*>(lv_event_get_user_data(event));
    if (!self) return;
    self->markGeometryDirty();
}

FLASHMEM void StepGrid::markGeometryDirty() {
    const bool wasDirty = geometry_.dirty;
    geometry_.dirty = true;
    if (!wasDirty && geometry_invalidated_) {
        geometry_invalidated_(geometry_invalidated_user_data_);
    }
}

FLASHMEM bool StepGrid::refreshStaticGeometry() {
    if (!note_layer_ || !container_) return false;
    lv_coord_t containerWidth = lv_obj_get_width(container_);
    lv_coord_t containerHeight = lv_obj_get_height(container_);
    lv_coord_t noteLayerWidth = lv_obj_get_width(note_layer_);
    lv_coord_t noteLayerHeight = lv_obj_get_height(note_layer_);
    if (!geometry_.initialized ||
        geometry_.containerWidth != containerWidth ||
        geometry_.containerHeight != containerHeight ||
        geometry_.noteLayerWidth != noteLayerWidth ||
        geometry_.noteLayerHeight != noteLayerHeight) {
        OC_PERF_SCOPE(perfLayout, "ui.step-grid.layout");
        lv_obj_update_layout(container_);
        containerWidth = lv_obj_get_width(container_);
        containerHeight = lv_obj_get_height(container_);
        noteLayerWidth = lv_obj_get_width(note_layer_);
        noteLayerHeight = lv_obj_get_height(note_layer_);
    }

    lv_area_t noteLayerArea{};
    lv_obj_get_coords(note_layer_, &noteLayerArea);

    bool changed = !geometry_.initialized ||
                   geometry_.containerWidth != containerWidth ||
                   geometry_.containerHeight != containerHeight ||
                   geometry_.noteLayerWidth != noteLayerWidth ||
                   geometry_.noteLayerHeight != noteLayerHeight;

    for (uint8_t i = 0; i < step_buttons_.size(); ++i) {
        lv_obj_t* button = step_buttons_[i];
        if (!button) continue;

        lv_area_t buttonArea{};
        lv_obj_get_coords(button, &buttonArea);

        const auto geometry = grid::buildTileGeometry(
            buttonArea,
            noteLayerArea,
            grid::measureRailWidth(lv_obj_get_content_width(button)),
            grid::measureButtonHeight(lv_obj_get_content_height(button))
        );
        changed = changed ||
                  this->geometry_.railWidth[i] != geometry.railWidth ||
                  this->geometry_.buttonHeight[i] != geometry.buttonHeight ||
                  this->geometry_.noteBaseX[i] != geometry.noteBaseX ||
                  this->geometry_.noteBaseY[i] != geometry.noteBaseY ||
                  this->geometry_.noteLabelBaselineY[i] != geometry.noteLabelBaselineY;
        this->geometry_.railWidth[i] = geometry.railWidth;
        this->geometry_.buttonHeight[i] = geometry.buttonHeight;
        this->geometry_.noteBaseX[i] = geometry.noteBaseX;
        this->geometry_.noteBaseY[i] = geometry.noteBaseY;
        this->geometry_.noteLabelBaselineY[i] = geometry.noteLabelBaselineY;
    }

    geometry_.initialized = true;
    geometry_.containerWidth = containerWidth;
    geometry_.containerHeight = containerHeight;
    geometry_.noteLayerWidth = noteLayerWidth;
    geometry_.noteLayerHeight = noteLayerHeight;
    geometry_.dirty = false;
    return changed;
}

void StepGrid::renderTileIndex(uint8_t tileIndex,
                               const TileRenderState& state,
                               const TileRenderDiff& diff) {
    if (!diff.absoluteStepChanged) {
        return;
    }

    auto& cache = render_cache_.tiles[tileIndex];

    char text[4];
    oc::type::text::formatUnsigned(text, sizeof(text), static_cast<unsigned>(state.absoluteStep) + 1U);
    if (std::strcmp(cache.stepIndexText, text) == 0) {
        return;
    }

    std::strncpy(cache.stepIndexText, text, sizeof(cache.stepIndexText) - 1);
    cache.stepIndexText[sizeof(cache.stepIndexText) - 1] = '\0';
}

void StepGrid::renderTilePlayhead(uint8_t tileIndex, bool visible, bool active) {
    auto& cache = render_cache_.tiles[tileIndex];
    const lv_opa_t nextOpa =
        visible ? (active ? PLAYHEAD_ACTIVE_OPA : PLAYHEAD_INACTIVE_OPA) : LV_OPA_TRANSP;
    const uint32_t nextColor = active ? PLAYHEAD_ACTIVE_COLOR : PLAYHEAD_INACTIVE_COLOR;
    cache.playheadLineVisible = visible;
    if (cache.playheadLineColorFull != nextColor) {
        cache.playheadLineColorFull = nextColor;
    }

    if (cache.playheadLineOpa != nextOpa) {
        cache.playheadLineOpa = nextOpa;
    }

}

FLASHMEM void StepGrid::onTileButtonDrawEvent(lv_event_t* event) {
    auto* context = static_cast<TileButtonDrawContext*>(lv_event_get_user_data(event));
    if (!context || !context->grid) return;

    StepGrid* self = context->grid;
    const uint8_t tileIndex = context->tileIndex;
    if (tileIndex >= self->step_buttons_.size()) return;

    lv_obj_t* button = lv_event_get_target_obj(event);
    if (!button) return;

    lv_layer_t* layer = lv_event_get_layer(event);
    if (!layer) return;

    const auto& cache = self->render_cache_.tiles[tileIndex];
    if (!cache.initialized) return;

    lv_area_t buttonArea{};
    lv_obj_get_coords(button, &buttonArea);
    const lv_coord_t contentWidth = lv_obj_get_content_width(button);
    const lv_coord_t contentHeight = lv_obj_get_content_height(button);
    const lv_coord_t railWidth = grid::measureRailWidth(contentWidth);
    const lv_coord_t buttonHeight = grid::measureButtonHeight(contentHeight);
    if (cache.inPattern) {
        lv_draw_rect_dsc_t guideDsc;
        lv_draw_rect_dsc_init(&guideDsc);
        guideDsc.bg_color = lv_color_hex(STEP_GUIDE_COLOR);
        guideDsc.bg_opa = STEP_GUIDE_OPA;
        guideDsc.radius = 0;
        guideDsc.border_width = 0;

        for (uint8_t g = 0; g < STEP_GUIDE_COUNT; ++g) {
            const auto layout = grid::buildGuideLayout(g, railWidth, buttonHeight);
            const lv_area_t guideArea{
                .x1 = static_cast<lv_coord_t>(buttonArea.x1 + layout.x),
                .y1 = static_cast<lv_coord_t>(buttonArea.y1 + layout.y),
                .x2 = static_cast<lv_coord_t>(buttonArea.x1 + layout.x + STEP_GUIDE_WIDTH - 1),
                .y2 = static_cast<lv_coord_t>(buttonArea.y1 + layout.y + layout.height - 1),
            };
            lv_draw_rect(layer, &guideDsc, &guideArea);
        }
    }

    drawScaleRuler(
        layer,
        buttonArea,
        tileIndex,
        self->render_cache_.presentation,
        self->render_cache_.pitchViewport,
        self->render_cache_.scaleSettings,
        self->render_cache_.pitchEditing
    );
    drawTileNoteEvents(
        layer,
        buttonArea,
        tileIndex,
        self->render_cache_.tiles,
        self->render_cache_.presentation,
        self->render_cache_.accentColor,
        self->render_cache_.pitchViewport,
        self->render_cache_.scaleSettings,
        self->render_cache_.pitchEditing
    );
    drawPlayheadLine(layer, buttonArea, cache);

    drawSelectionOverlay(layer, buttonArea, cache);

    if (cache.stepIndexText[0] != '\0') {
        lv_draw_label_dsc_t labelDsc;
        lv_draw_label_dsc_init(&labelDsc);
        labelDsc.text = cache.stepIndexText;
        labelDsc.font = fonts.compact_selected();
        labelDsc.color = lv_color_hex(STEP_INDEX_COLOR);
        labelDsc.opa = STEP_INDEX_OPA;
        labelDsc.align = LV_TEXT_ALIGN_RIGHT;

        lv_area_t labelArea = buttonArea;
        labelArea.x1 = buttonArea.x1;
        labelArea.x2 = static_cast<lv_coord_t>(buttonArea.x2 - STEP_INDEX_RIGHT_PAD);
        labelArea.y1 = static_cast<lv_coord_t>(buttonArea.y1 + STEP_INDEX_TOP_PAD);
        lv_draw_label(layer, &labelDsc, &labelArea);
    }

    if (cache.inPattern) {
        drawSemanticBadges(layer, buttonArea, cache);
        drawCycleRail(layer, buttonArea, cache);
        drawMicroRail(layer, buttonArea, cache);
    }
}

void StepGrid::renderTile(
    uint8_t tileIndex,
    const TileRenderState& state,
    const TileRenderDiff& diff,
    bool propertyVisualChanged,
    bool tileFeedbackChanged,
    bool geometryChanged,
    const StepGridFrameState& frameState
) {
    auto& cache = render_cache_.tiles[tileIndex];
    const bool noteLabelNeedsRender =
        !cache.initialized ||
        geometryChanged ||
        propertyVisualChanged ||
        tileFeedbackChanged ||
        diff.inPatternChanged ||
        diff.enabledChanged ||
        diff.noteChanged ||
        diff.velocityChanged ||
        diff.probabilityChanged ||
        diff.gateChanged ||
        diff.nudgeChanged ||
        diff.variationChanged ||
        diff.probabilityCycleActiveChanged ||
        diff.childContentChanged ||
        diff.childPitchSummaryChanged;
    const lv_coord_t noteLabelY = geometry_.noteLabelBaselineY[tileIndex];
    if (!geometryChanged &&
        !diff.dataChanged && !diff.playheadChanged && !propertyVisualChanged && !tileFeedbackChanged &&
        !diff.probabilityMaskChanged && !diff.contentBadgesChanged &&
        !diff.childContentChanged && !diff.childPitchSummaryChanged) {
        return;
    }

    renderTileIndex(tileIndex, state, diff);

    if (noteLabelNeedsRender) {
        grid::label_renderer::renderTileNoteLabel(
            render_cache_.tiles[tileIndex],
            {
                .primary = note_labels_[tileIndex],
                .secondary = secondary_labels_[tileIndex],
                .inlineIcon = step_inline_icons_[tileIndex],
            },
            state,
            diff,
            propertyVisualChanged,
            tileFeedbackChanged,
            geometryChanged,
            frameState,
            {
                .baseX = geometry_.noteBaseX[tileIndex],
                .baselineY = noteLabelY,
                .labelHeight = geometry_.noteLabelHeight,
                .iconWidth = geometry_.inlineIconWidth[tileIndex],
                .iconHeight = geometry_.inlineIconHeight[tileIndex],
            }
        );
    }

    if (diff.dataChanged || diff.playheadChanged) {
        renderTilePlayhead(
            tileIndex,
            state.playheadVisible && state.inPattern,
            state.playing
        );
    }

    cache.commitRenderedState(state);
}

void StepGrid::render(const sequencer::grid::StepGridFrameState& frameState) {
    if (!container_ || lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;
    OC_PERF_SCOPE(perfRender, "ui.step-grid.render");

    bool geometryChanged = false;
    if (geometry_.dirty) {
        geometryChanged = refreshStaticGeometry();
    }

    const bool viewportFrozen = frameState.feedbackVisible &&
        render_cache_.tiles[0].initialized;
    const auto targetPitchViewport = viewportFrozen
        ? render_cache_.pitchViewport
        : frameState.pitchViewport;

    if (render_cache_.presentation != frameState.presentation ||
        render_cache_.accentColor != frameState.accentColor ||
        render_cache_.pitchViewport.lowNote != targetPitchViewport.lowNote ||
        render_cache_.pitchViewport.semitoneSpan !=
            targetPitchViewport.semitoneSpan ||
        render_cache_.scaleSettings.root != frameState.scaleSettings.root ||
        render_cache_.scaleSettings.type != frameState.scaleSettings.type ||
        render_cache_.scaleSettings.mode != frameState.scaleSettings.mode ||
        render_cache_.pitchEditing != frameState.pitchEditing) {
        invalidateTileCaches();
        render_cache_.presentation = frameState.presentation;
        render_cache_.accentColor = frameState.accentColor;
        render_cache_.pitchViewport = targetPitchViewport;
        render_cache_.scaleSettings = frameState.scaleSettings;
        render_cache_.pitchEditing = frameState.pitchEditing;
    }

    const auto plan = grid::buildFrameRenderPlan(
        render_cache_.tiles,
        render_cache_.property,
        render_cache_.feedback,
        frameState
    );

    render_cache_.property = frameState.activeProperty;
    render_cache_.feedback = plan.nextFeedback;

    if (!plan.anyDirty && !geometryChanged) {
        return;
    }

#if OC_ENABLE_STATS
    uint32_t dirtyTileCount = 0;
#endif
    // Preserve sparse updates; collapse dense multi-edits into one display region.
    oc::ui::lvgl::StaticSurfaceInvalidationBatch<4> invalidation(container_);
    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        if (!plan.tileDirty[i] && !geometryChanged) continue;
        invalidation.include(tiles_[i]);
#if OC_ENABLE_STATS
        dirtyTileCount += 1;
#endif
        const TileRenderState& state = frameState.tiles[i];
        renderTile(
            i,
            state,
            plan.diffs[i],
            plan.propertyVisualChanged,
            plan.feedbackChanged[i],
            geometryChanged,
            frameState
        );
    }
    invalidation.flush();

#if OC_ENABLE_STATS
    OC_PERF_UNITS(perfRender, dirtyTileCount, geometryChanged ? 1U : 0U);
#endif
}

}  // namespace core::ui

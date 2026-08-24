#include "ui/sequencer/SequencerClipLauncherSurface.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"
#include "state/sequencer/SequencerClipRegionOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "ui/sequencer/SequencerQuickControlVisuals.hpp"

namespace core::ui::sequencer {

namespace seq = core::state::sequencer;
namespace theme = standalone::theme;

namespace {

struct LauncherLayout {
    static constexpr lv_coord_t GAP = 2;
    static constexpr lv_coord_t HEADER_HEIGHT = 20;
    static constexpr lv_coord_t SCENE_RAIL_WIDTH = 42;

    lv_coord_t gridX = 0;
    lv_coord_t columnWidth = 0;
    lv_coord_t rowHeight = 0;
};

FLASHMEM LauncherLayout launcherLayout(const lv_area_t& surface) {
    const lv_coord_t width = lv_area_get_width(&surface);
    const lv_coord_t height = lv_area_get_height(&surface);
    const lv_coord_t gridWidth = static_cast<lv_coord_t>(
        width - LauncherLayout::SCENE_RAIL_WIDTH - LauncherLayout::GAP
    );
    return {
        .gridX = static_cast<lv_coord_t>(
            surface.x1 + LauncherLayout::SCENE_RAIL_WIDTH +
            LauncherLayout::GAP
        ),
        .columnWidth = static_cast<lv_coord_t>((
            gridWidth - LauncherLayout::GAP * 3
        ) / seq::ClipWorkspaceUiState::VISIBLE_TRACKS),
        .rowHeight = std::max<lv_coord_t>(
            1,
            static_cast<lv_coord_t>((
                height - LauncherLayout::HEADER_HEIGHT - LauncherLayout::GAP
            ) / seq::ClipWorkspaceUiState::VISIBLE_ROWS)
        ),
    };
}

FLASHMEM void drawRect(
    lv_layer_t* layer,
    const lv_area_t& area,
    uint32_t color,
    lv_opa_t fill,
    uint32_t borderColor = theme::color::BORDER_SUBTLE,
    lv_coord_t borderWidth = 0,
    lv_opa_t borderOpacity = LV_OPA_TRANSP,
    lv_coord_t radius = theme::layout::INTERACTIVE_SURFACE_RADIUS
) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = fill;
    dsc.border_color = lv_color_hex(borderColor);
    dsc.border_width = borderWidth;
    dsc.border_opa = borderOpacity;
    dsc.radius = radius;
    lv_draw_rect(layer, &dsc, &area);
}

FLASHMEM void drawText(
    lv_layer_t* layer,
    const lv_area_t& area,
    const char* text,
    uint32_t color,
    lv_opa_t opacity,
    const lv_font_t* font,
    lv_text_align_t alignment = LV_TEXT_ALIGN_CENTER
) {
    const lv_font_t* resolvedFont = font ? font : LV_FONT_DEFAULT;
    lv_area_t textArea = area;
    const lv_coord_t availableHeight = lv_area_get_height(&area);
    if (resolvedFont->line_height < availableHeight) {
        textArea.y1 = static_cast<lv_coord_t>(
            area.y1 + (availableHeight - resolvedFont->line_height) / 2
        );
    }
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = resolvedFont;
    dsc.color = lv_color_hex(color);
    dsc.opa = opacity;
    dsc.align = alignment;
    lv_draw_label(layer, &dsc, &textArea);
}

FLASHMEM void drawSquare(
    lv_layer_t* layer,
    lv_coord_t centerX,
    lv_coord_t centerY,
    lv_coord_t side,
    uint32_t color
) {
    const lv_coord_t half = static_cast<lv_coord_t>(side / 2);
    drawRect(
        layer,
        {static_cast<lv_coord_t>(centerX - half),
         static_cast<lv_coord_t>(centerY - half),
         static_cast<lv_coord_t>(centerX - half + side - 1),
         static_cast<lv_coord_t>(centerY - half + side - 1)},
        color,
        LV_OPA_COVER,
        color,
        0,
        LV_OPA_TRANSP,
        1
    );
}

FLASHMEM void drawQueuedCorners(
    lv_layer_t* layer,
    const lv_area_t& area,
    uint32_t color
) {
    constexpr lv_coord_t length = 8;
    constexpr lv_coord_t thickness = 2;
    const std::array<lv_area_t, 8> segments{{
        {area.x1, area.y1, static_cast<lv_coord_t>(area.x1 + length),
         static_cast<lv_coord_t>(area.y1 + thickness - 1)},
        {area.x1, area.y1, static_cast<lv_coord_t>(area.x1 + thickness - 1),
         static_cast<lv_coord_t>(area.y1 + length)},
        {static_cast<lv_coord_t>(area.x2 - length), area.y1, area.x2,
         static_cast<lv_coord_t>(area.y1 + thickness - 1)},
        {static_cast<lv_coord_t>(area.x2 - thickness + 1), area.y1, area.x2,
         static_cast<lv_coord_t>(area.y1 + length)},
        {area.x1, static_cast<lv_coord_t>(area.y2 - thickness + 1),
         static_cast<lv_coord_t>(area.x1 + length), area.y2},
        {area.x1, static_cast<lv_coord_t>(area.y2 - length),
         static_cast<lv_coord_t>(area.x1 + thickness - 1), area.y2},
        {static_cast<lv_coord_t>(area.x2 - length),
         static_cast<lv_coord_t>(area.y2 - thickness + 1), area.x2, area.y2},
        {static_cast<lv_coord_t>(area.x2 - thickness + 1),
         static_cast<lv_coord_t>(area.y2 - length), area.x2, area.y2},
    }};
    for (const auto& segment : segments) {
        drawRect(
            layer,
            segment,
            color,
            LV_OPA_COVER,
            color,
            0,
            LV_OPA_TRANSP,
            0
        );
    }
}

FLASHMEM void drawCountdownRing(
    lv_layer_t* layer,
    lv_coord_t centerX,
    lv_coord_t centerY,
    lv_coord_t radius,
    uint8_t remainingQ8,
    uint32_t color
) {
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.base.layer = layer;
    arc.center = {centerX, centerY};
    arc.radius = radius;
    arc.width = 2;
    arc.rounded = 0;
    arc.start_angle = 270;
    arc.end_angle = 629;
    arc.color = lv_color_hex(theme::color::BORDER_SUBTLE);
    arc.opa = LV_OPA_50;
    lv_draw_arc(layer, &arc);

    if (remainingQ8 == 0U) return;
    arc.end_angle = static_cast<int32_t>(
        270U + (static_cast<uint32_t>(remainingQ8) * 359U) / 255U
    );
    arc.color = lv_color_hex(color);
    arc.opa = LV_OPA_COVER;
    lv_draw_arc(layer, &arc);
}

FLASHMEM const char* quantizationLabel(uint8_t value) {
    switch (value) {
        case 1U: return "1 beat";
        case 2U: return "1 bar";
        default: return "Global";
    }
}

FLASHMEM bool areasOverlap(const lv_area_t& lhs, const lv_area_t& rhs) {
    return lhs.x1 <= rhs.x2 && lhs.x2 >= rhs.x1 &&
        lhs.y1 <= rhs.y2 && lhs.y2 >= rhs.y1;
}

struct PreviewWindow {
    uint32_t start = 0U;
    uint32_t loop = 0U;
    uint32_t end = 0U;

    [[nodiscard]] bool valid() const noexcept { return start < end; }
};

FLASHMEM PreviewWindow previewWindow(
    uint16_t playStart,
    uint16_t loopStart,
    uint16_t loopEnd
) {
    if (playStart > loopStart || loopStart >= loopEnd) return {};
    return {playStart, loopStart, loopEnd};
}

template <typename Preview>
FLASHMEM void setPreviewSpan(
    Preview& preview,
    uint8_t row,
    uint32_t start,
    uint32_t end,
    uint8_t velocity,
    const PreviewWindow& window
) {
    if (!window.valid() || row >= Preview::ROWS || velocity == 0U ||
        end <= window.start || start >= window.end) {
        return;
    }
    start = std::max(start, window.start);
    end = std::min(end, window.end);
    if (start >= end) return;

    const uint32_t duration = window.end - window.start;
    const uint8_t first = static_cast<uint8_t>(std::min<uint32_t>(
        Preview::COLUMNS - 1U,
        ((start - window.start) * Preview::COLUMNS) / duration
    ));
    const uint8_t lastExclusive = static_cast<uint8_t>(std::min<uint32_t>(
        Preview::COLUMNS,
        ((end - window.start) * Preview::COLUMNS + duration - 1U) / duration
    ));
    const uint8_t last = std::max<uint8_t>(
        static_cast<uint8_t>(first + 1U),
        lastExclusive
    );
    for (uint8_t column = first; column < last; ++column) {
        const std::size_t index =
            static_cast<std::size_t>(row) * Preview::COLUMNS + column;
        auto& pixel = preview.velocity[index];
        pixel = std::max(pixel, velocity);
    }
    preview.onsetMask[row] |= static_cast<uint32_t>(1UL << first);
    preview.content = true;
}

template <typename Preview>
FLASHMEM void setLoopMarker(
    Preview& preview,
    const PreviewWindow& window
) {
    if (!window.valid() || window.loop <= window.start ||
        window.loop >= window.end) {
        return;
    }
    preview.loopColumn = static_cast<uint8_t>(std::min<uint32_t>(
        Preview::COLUMNS - 1U,
        ((window.loop - window.start) * Preview::COLUMNS) /
            (window.end - window.start)
    ));
}

template <typename Preview>
FLASHMEM uint8_t previewPitchRow(
    uint8_t note,
    uint8_t minimum,
    uint8_t maximum
) {
    if (minimum >= maximum) return 3U;
    const uint16_t range = static_cast<uint16_t>(maximum - minimum);
    const uint8_t ascending = static_cast<uint8_t>(std::min<uint16_t>(
        Preview::ROWS - 1U,
        (static_cast<uint16_t>(note - minimum) * (Preview::ROWS - 1U) +
         range / 2U) / range
    ));
    return static_cast<uint8_t>(Preview::ROWS - 1U - ascending);
}

template <typename Pattern, typename Clip, typename Preview>
FLASHMEM void projectMelodicPreview(
    const Pattern& pattern,
    const Clip& clip,
    uint8_t length,
    uint8_t stepsPerBeat,
    const oc::note::sequencer::StepBitMask128& enabledMask,
    Preview& preview
) {
    const uint16_t ticksPerStep = seq::sequencerTicksPerStep(stepsPerBeat);
    const auto window = previewWindow(
        clip.playStartTick,
        clip.loopStartTick,
        clip.loopEndTick
    );
    if (ticksPerStep == 0U || !window.valid()) return;
    setLoopMarker(preview, window);

    uint8_t minimum = std::numeric_limits<uint8_t>::max();
    uint8_t maximum = 0U;
    for (uint8_t step = 0U; step < length; ++step) {
        const uint32_t tick = static_cast<uint32_t>(step) * ticksPerStep;
        if (!enabledMask.test(step) || tick < window.start ||
            tick >= window.end) {
            continue;
        }
        minimum = std::min(minimum, pattern.note[step]);
        maximum = std::max(maximum, pattern.note[step]);
    }
    if (minimum == std::numeric_limits<uint8_t>::max()) return;

    for (uint8_t step = 0U; step < length; ++step) {
        if (!enabledMask.test(step) || pattern.gate[step] == 0U) continue;
        const int32_t base = static_cast<int32_t>(step) * ticksPerStep;
        const int32_t nudged = base +
            (static_cast<int32_t>(pattern.nudge[step]) * ticksPerStep) / 100;
        if (nudged < 0) continue;
        const uint32_t start = static_cast<uint32_t>(nudged);
        const uint32_t span = std::max<uint32_t>(
            1U,
            (static_cast<uint32_t>(pattern.gate[step]) * ticksPerStep) / 100U
        );
        setPreviewSpan(
            preview,
            previewPitchRow<Preview>(pattern.note[step], minimum, maximum),
            start,
            start + span,
            std::max<uint8_t>(1U, pattern.velocity[step]),
            window
        );
    }
}

template <typename Preview>
FLASHMEM void projectDrumPreview(
    const seq::DrumTrackState& drum,
    uint16_t playStartTick,
    uint16_t loopStartTick,
    uint16_t loopEndTick,
    Preview& preview
) {
    const auto window = previewWindow(
        playStartTick,
        loopStartTick,
        loopEndTick
    );
    if (!window.valid()) return;
    setLoopMarker(preview, window);

    const uint8_t laneCount = std::min<uint8_t>(
        drum.kit.laneCount,
        seq::DRUM_MAX_LANES
    );
    for (uint8_t lane = 0U; lane < laneCount; ++lane) {
        const uint8_t length = drum.pattern.effectiveLength(lane);
        const uint16_t ticksPerStep = seq::sequencerTicksPerStep(
            drum.pattern.effectiveStepsPerBeat(lane)
        );
        const uint32_t cycle = static_cast<uint32_t>(length) * ticksPerStep;
        if (length == 0U || ticksPerStep == 0U || cycle == 0U) continue;
        const auto& lanePattern = drum.pattern.lanes[lane];
        for (uint8_t step = 0U; step < length; ++step) {
            if (!lanePattern.enabledMask.test(step) ||
                lanePattern.gate[step] == 0U) {
                continue;
            }
            const uint32_t base = static_cast<uint32_t>(step) * ticksPerStep;
            uint32_t occurrence = base;
            if (occurrence < window.start) {
                const uint32_t skipped =
                    (window.start - occurrence + cycle - 1U) / cycle;
                occurrence += skipped * cycle;
            }
            const uint32_t span = std::max<uint32_t>(
                1U,
                (static_cast<uint32_t>(lanePattern.gate[step]) *
                 ticksPerStep) / 100U
            );
            for (; occurrence < window.end; occurrence += cycle) {
                const int32_t nudged = static_cast<int32_t>(occurrence) +
                    (static_cast<int32_t>(lanePattern.nudge[step]) *
                     ticksPerStep) / 100;
                if (nudged >= 0) {
                    const uint32_t start = static_cast<uint32_t>(nudged);
                    setPreviewSpan(
                        preview,
                        lane,
                        start,
                        start + span,
                        std::max<uint8_t>(1U, lanePattern.velocity[step]),
                        window
                    );
                }
                if (std::numeric_limits<uint32_t>::max() - occurrence < cycle) {
                    break;
                }
            }
        }
    }
}

}  // namespace

FLASHMEM SequencerClipLauncherSurface::SequencerClipLauncherSurface(
    lv_obj_t* parent
) {
    root_ = lv_obj_create(parent);
    if (!root_) return;
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(root_, 1);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(root_, onDraw, LV_EVENT_DRAW_MAIN, this);
}

FLASHMEM SequencerClipLauncherSurface::~SequencerClipLauncherSurface() {
    if (root_) lv_obj_delete(root_);
}

FLASHMEM void SequencerClipLauncherSurface::render(
    const SequencerClipLauncherSurfaceProps& props
) {
    if (!root_) return;
    props_ = props;
    if (!props.visible || props.ui == nullptr || props.clips == nullptr ||
        props.launches == nullptr || props.tracks == nullptr) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    rebuildPreviews();
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(root_);
}

FLASHMEM void SequencerClipLauncherSurface::rebuildPreviews() {
    previews_.fill({});
    if (props_.ui == nullptr || props_.clips == nullptr ||
        props_.tracks == nullptr || props_.sequencer == nullptr) {
        return;
    }

    const auto& ui = *props_.ui;
    for (uint8_t column = 0U;
         column < seq::ClipWorkspaceUiState::VISIBLE_TRACKS;
         ++column) {
        const uint8_t track = static_cast<uint8_t>(
            ui.firstVisibleTrack + column
        );
        if (track >= seq::SequencerClipGridState::TRACK_COUNT) continue;
        for (uint8_t row = 0U;
             row < seq::ClipWorkspaceUiState::VISIBLE_ROWS;
             ++row) {
            const uint8_t slot = static_cast<uint8_t>(
                ui.firstVisibleSlot + row
            );
            const seq::SequencerClipAddress address{track, slot};
            if (!props_.clips->isOccupied(address)) continue;

            auto& preview = previews_[
                column * seq::ClipWorkspaceUiState::VISIBLE_ROWS + row
            ];
            if (props_.clips->isResident(address)) {
                const auto& clip = seq::canonicalTrackClip(
                    *props_.tracks,
                    *props_.sequencer,
                    track
                );
                if (props_.tracks->isDrumTrack(track)) {
                    projectDrumPreview(
                        props_.tracks->drumTrack(track),
                        clip.playStartTick,
                        clip.loopStartTick,
                        clip.loopEndTick,
                        preview
                    );
                } else {
                    const auto& pattern = seq::canonicalTrackPattern(
                        *props_.tracks,
                        *props_.sequencer,
                        track
                    );
                    projectMelodicPreview(
                        pattern,
                        clip,
                        pattern.length.get(),
                        pattern.stepsPerBeat.get(),
                        pattern.enabledMask.get(),
                        preview
                    );
                }
            } else if (const auto* document =
                           props_.clips->inactiveDocument(address)) {
                if (document->trackKind == seq::SequencerTrackKind::DRUM &&
                    document->drum != nullptr) {
                    projectDrumPreview(
                        *document->drum,
                        document->clip.playStartTick,
                        document->clip.loopStartTick,
                        document->clip.loopEndTick,
                        preview
                    );
                } else {
                    projectMelodicPreview(
                        document->pattern,
                        document->clip,
                        document->pattern.length,
                        document->pattern.stepsPerBeat,
                        document->pattern.enabledMask,
                        preview
                    );
                }
            }
        }
    }
}

FLASHMEM void SequencerClipLauncherSurface::invalidatePlaybackProgress() {
    if (!root_ || !props_.visible || props_.ui == nullptr ||
        props_.launches == nullptr ||
        lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    lv_area_t surface{};
    lv_obj_get_content_coords(root_, &surface);
    const auto layout = launcherLayout(surface);
    const auto& ui = *props_.ui;
    const auto scene = props_.launches->sceneTelemetry();
    const uint8_t sceneSlot = scene.queuedScene <
            seq::SequencerClipGridState::SLOT_COUNT
        ? scene.queuedScene
        : scene.activeScene;
    if (sceneSlot >= ui.firstVisibleSlot &&
        sceneSlot < ui.firstVisibleSlot +
            seq::ClipWorkspaceUiState::VISIBLE_ROWS) {
        const uint8_t row = static_cast<uint8_t>(
            sceneSlot - ui.firstVisibleSlot
        );
        const lv_coord_t y = static_cast<lv_coord_t>(
            surface.y1 + LauncherLayout::HEADER_HEIGHT +
            LauncherLayout::GAP + row * layout.rowHeight
        );
        const lv_area_t rail{
            .x1 = surface.x1,
            .y1 = y,
            .x2 = static_cast<lv_coord_t>(
                surface.x1 + LauncherLayout::SCENE_RAIL_WIDTH - 1
            ),
            .y2 = static_cast<lv_coord_t>(
                y + layout.rowHeight - LauncherLayout::GAP - 1
            ),
        };
        // The full 42 px cell is cheap to redraw and cleanly replaces the Play
        // glyph with the transient countdown ring.
        lv_obj_invalidate_area(root_, &rail);
    }
    for (uint8_t column = 0U;
         column < seq::ClipWorkspaceUiState::VISIBLE_TRACKS;
         ++column) {
        const uint8_t track = static_cast<uint8_t>(
            ui.firstVisibleTrack + column
        );
        if (track >= seq::ClipWorkspaceUiState::TRACK_COUNT) continue;
        const lv_coord_t x = static_cast<lv_coord_t>(
            layout.gridX + column *
                (layout.columnWidth + LauncherLayout::GAP)
        );
        const auto telemetry = props_.launches->telemetry(track);
        if (telemetry.status == seq::SequencerClipLaunchStatus::QUEUED &&
            telemetry.action == seq::SequencerClipLaunchAction::STOP &&
            telemetry.queuedSlot ==
                seq::SequencerClipGridState::INVALID_SLOT) {
            const lv_area_t stopRing{
                .x1 = static_cast<lv_coord_t>(x + layout.columnWidth - 20),
                .y1 = surface.y1,
                .x2 = static_cast<lv_coord_t>(x + layout.columnWidth - 1),
                .y2 = static_cast<lv_coord_t>(
                    surface.y1 + LauncherLayout::HEADER_HEIGHT - 1
                ),
            };
            lv_obj_invalidate_area(root_, &stopRing);
        }
        if (telemetry.status == seq::SequencerClipLaunchStatus::QUEUED &&
            telemetry.queuedSlot >= ui.firstVisibleSlot &&
            telemetry.queuedSlot < ui.firstVisibleSlot +
                seq::ClipWorkspaceUiState::VISIBLE_ROWS) {
            const uint8_t queuedRow = static_cast<uint8_t>(
                telemetry.queuedSlot - ui.firstVisibleSlot
            );
            const lv_coord_t queuedY = static_cast<lv_coord_t>(
                surface.y1 + LauncherLayout::HEADER_HEIGHT +
                LauncherLayout::GAP + queuedRow * layout.rowHeight
            );
            const lv_area_t queuedRing{
                .x1 = static_cast<lv_coord_t>(x + layout.columnWidth - 19),
                .y1 = queuedY,
                .x2 = static_cast<lv_coord_t>(x + layout.columnWidth - 1),
                .y2 = static_cast<lv_coord_t>(queuedY + 18),
            };
            lv_obj_invalidate_area(root_, &queuedRing);
        }
        if (telemetry.activeRemainingQ8 != 0U &&
            telemetry.activeSlot >= ui.firstVisibleSlot &&
            telemetry.activeSlot < ui.firstVisibleSlot +
                seq::ClipWorkspaceUiState::VISIBLE_ROWS) {
            const uint8_t activeRow = static_cast<uint8_t>(
                telemetry.activeSlot - ui.firstVisibleSlot
            );
            const lv_coord_t activeY = static_cast<lv_coord_t>(
                surface.y1 + LauncherLayout::HEADER_HEIGHT +
                LauncherLayout::GAP + activeRow * layout.rowHeight
            );
            const lv_area_t activeRing{
                .x1 = static_cast<lv_coord_t>(x + layout.columnWidth - 19),
                .y1 = activeY,
                .x2 = static_cast<lv_coord_t>(x + layout.columnWidth - 1),
                .y2 = static_cast<lv_coord_t>(activeY + 18),
            };
            lv_obj_invalidate_area(root_, &activeRing);
        }
        if (telemetry.stopped ||
            telemetry.activeSlot < ui.firstVisibleSlot ||
            telemetry.activeSlot >=
                ui.firstVisibleSlot + seq::ClipWorkspaceUiState::VISIBLE_ROWS) {
            continue;
        }
        const uint8_t row = static_cast<uint8_t>(
            telemetry.activeSlot - ui.firstVisibleSlot
        );
        const lv_coord_t y = static_cast<lv_coord_t>(
            surface.y1 + LauncherLayout::HEADER_HEIGHT +
            LauncherLayout::GAP + row * layout.rowHeight
        );
        const lv_area_t progress{
            .x1 = static_cast<lv_coord_t>(x + 4),
            .y1 = static_cast<lv_coord_t>(
                y + layout.rowHeight - LauncherLayout::GAP - 3
            ),
            .x2 = static_cast<lv_coord_t>(x + layout.columnWidth - 5),
            .y2 = static_cast<lv_coord_t>(
                y + layout.rowHeight - LauncherLayout::GAP - 1
            ),
        };
        lv_obj_invalidate_area(root_, &progress);
    }
}

FLASHMEM void SequencerClipLauncherSurface::invalidateTrackActivity() {
    if (!root_ || !props_.visible || props_.ui == nullptr ||
        props_.statusBar == nullptr ||
        props_.ui->editorActive() ||
        lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    lv_area_t surface{};
    lv_obj_get_content_coords(root_, &surface);
    const auto layout = launcherLayout(surface);
    const lv_area_t headers{
        .x1 = layout.gridX,
        .y1 = surface.y1,
        .x2 = surface.x2,
        .y2 = static_cast<lv_coord_t>(
            surface.y1 + LauncherLayout::HEADER_HEIGHT - 1
        ),
    };
    lv_obj_invalidate_area(root_, &headers);
}

FLASHMEM void SequencerClipLauncherSurface::onDraw(lv_event_t* event) {
    auto* self = static_cast<SequencerClipLauncherSurface*>(
        lv_event_get_user_data(event)
    );
    if (self) self->draw(lv_event_get_layer(event));
}

FLASHMEM void SequencerClipLauncherSurface::draw(lv_layer_t* layer) const {
    if (!root_ || !layer || !props_.visible || props_.ui == nullptr ||
        props_.clips == nullptr || props_.launches == nullptr ||
        props_.tracks == nullptr) {
        return;
    }

    lv_area_t surface{};
    lv_obj_get_content_coords(root_, &surface);
    const auto& ui = *props_.ui;

    if (ui.editorActive()) {
        drawRect(
            layer,
            surface,
            theme::color::BACKGROUND,
            LV_OPA_COVER,
            theme::color::BORDER_SUBTLE,
            0,
            LV_OPA_TRANSP,
            0
        );
        constexpr lv_coord_t margin = theme::layout::PAD_SM;
        constexpr lv_coord_t gap = theme::layout::GAP_SM;
        constexpr uint8_t rowCount = 3U;
        const lv_coord_t availableHeight = static_cast<lv_coord_t>(
            lv_area_get_height(&surface) - 2 * margin - 2 * gap
        );
        const lv_coord_t rowHeight = static_cast<lv_coord_t>(
            availableHeight / rowCount
        );
        constexpr std::array<const char*, rowCount> behaviorIcons{{
            standalone::icons::LENGTH,
            standalone::icons::ROUTING,
            standalone::icons::DIVISION,
        }};
        constexpr std::array<uint32_t, rowCount> behaviorColors{{
            theme::color::STEP_LENGTH,
            theme::color::ROUTING,
            theme::color::STEP_DIVISION,
        }};
        constexpr std::array<const char*, rowCount> actionIcons{{
            standalone::icons::ACTION_CREATE,
            "",
            standalone::icons::ACTION_CLEAR,
        }};
        constexpr std::array<const char*, rowCount> actionLabels{{
            "Create clip", "Set stop", "Clear stop",
        }};
        constexpr std::array<uint32_t, rowCount> actionColors{{
            theme::color::POSITIVE,
            theme::color::DESTRUCTIVE,
            theme::color::SECONDARY,
        }};

        for (uint8_t row = 0U; row < rowCount; ++row) {
            const lv_coord_t y = static_cast<lv_coord_t>(
                surface.y1 + margin + row * (rowHeight + gap)
            );
            const lv_area_t item{
                static_cast<lv_coord_t>(surface.x1 + margin),
                y,
                static_cast<lv_coord_t>(surface.x2 - margin),
                row + 1U == rowCount
                    ? static_cast<lv_coord_t>(surface.y2 - margin)
                    : static_cast<lv_coord_t>(y + rowHeight - 1),
            };
            const bool slotAction =
                ui.editor == seq::ClipWorkspaceEditor::SLOT_ACTION;
            const bool focused = slotAction
                ? static_cast<uint8_t>(ui.slotAction) == row
                : static_cast<uint8_t>(ui.editorField) == row;
            drawRect(
                layer,
                item,
                focused ? theme::color::SURFACE_RAISED
                        : theme::color::SURFACE_IDLE,
                LV_OPA_COVER,
                focused ? theme::color::FOCUS_EDIT
                        : theme::color::BORDER_SUBTLE,
                focused ? 2 : 1,
                LV_OPA_COVER
            );

            const char* icon = slotAction
                ? actionIcons[row] : behaviorIcons[row];
            const uint32_t iconColor = slotAction
                ? actionColors[row] : behaviorColors[row];
            if (slotAction && row == 1U) {
                drawSquare(
                    layer,
                    static_cast<lv_coord_t>(item.x1 + 24),
                    static_cast<lv_coord_t>((item.y1 + item.y2) / 2),
                    9,
                    iconColor
                );
            } else {
                drawText(
                    layer,
                    {static_cast<lv_coord_t>(item.x1 + 8), item.y1,
                     static_cast<lv_coord_t>(item.x1 + 40), item.y2},
                    icon,
                    iconColor,
                    focused ? LV_OPA_COVER : LV_OPA_70,
                    standalone_fonts.icons_16
                );
            }

            if (slotAction) {
                drawText(
                    layer,
                    {static_cast<lv_coord_t>(item.x1 + 48), item.y1,
                     static_cast<lv_coord_t>(item.x2 - 8), item.y2},
                    actionLabels[row],
                    actionColors[row],
                    focused ? LV_OPA_COVER : LV_OPA_70,
                    fonts.compact_selected(),
                    LV_TEXT_ALIGN_LEFT
                );
                continue;
            }

            std::array<char, 20> value{};
            if (row == 0U) {
                if (ui.editorLength == 0U) {
                    std::snprintf(value.data(), value.size(), "Off");
                } else {
                    const bool clip = ui.editor ==
                        seq::ClipWorkspaceEditor::CLIP_BEHAVIOR;
                    const bool singular = ui.editorLength == 1U;
                    const char* unit = clip
                        ? singular ? "loop" : "loops"
                        : singular ? "bar" : "bars";
                    std::snprintf(
                        value.data(), value.size(), "%u %s",
                        static_cast<unsigned>(ui.editorLength),
                        unit
                    );
                }
            } else if (row == 1U) {
                visual::formatLauncherFollowChoice(
                    value.data(),
                    value.size(),
                    static_cast<seq::SequencerLauncherFollowChoice>(
                        ui.editorFollowChoice
                    ),
                    ui.editor == seq::ClipWorkspaceEditor::SCENE_BEHAVIOR
                );
            } else {
                std::snprintf(
                    value.data(), value.size(), "%s",
                    quantizationLabel(ui.editorQuantization)
                );
            }
            drawText(
                layer,
                {static_cast<lv_coord_t>(item.x1 + 48), item.y1,
                 static_cast<lv_coord_t>(item.x2 - 8), item.y2},
                value.data(),
                theme::color::TEXT_PRIMARY,
                LV_OPA_COVER,
                fonts.compact_selected(),
                LV_TEXT_ALIGN_RIGHT
            );
        }
        return;
    }

    const auto layout = launcherLayout(surface);
    constexpr lv_coord_t gap = LauncherLayout::GAP;
    constexpr lv_coord_t headerHeight = LauncherLayout::HEADER_HEIGHT;
    constexpr lv_coord_t sceneRailWidth = LauncherLayout::SCENE_RAIL_WIDTH;
    const lv_coord_t gridX = layout.gridX;
    const lv_coord_t columnWidth = layout.columnWidth;
    const lv_coord_t rowHeight = layout.rowHeight;
    const uint8_t addTrack = seq::ClipWorkspaceUiState::addTrackIndex(
        props_.enabledTrackMask);
    const uint8_t lastScene = props_.clips->lastNavigableScene();
    const auto sceneTelemetry = props_.launches->sceneTelemetry();
    const bool selectingTracks = props_.trackNavigation != nullptr &&
        props_.trackNavigation->selection.active.get() &&
        props_.trackNavigation->selection.scope.get() ==
            core::state::StructureSelectionScope::TRACK;

    const lv_area_t areaHeader{
        surface.x1,
        surface.y1,
        static_cast<lv_coord_t>(surface.x1 + sceneRailWidth - 1),
        static_cast<lv_coord_t>(surface.y1 + headerHeight - 1),
    };
    drawRect(
        layer,
        areaHeader,
        theme::color::SURFACE_RAISED,
        LV_OPA_COVER,
        theme::color::BORDER_SUBTLE,
        1,
        LV_OPA_COVER
    );
    for (uint8_t row = 0U;
         row < seq::ClipWorkspaceUiState::VISIBLE_ROWS;
         ++row) {
        const uint8_t slot = static_cast<uint8_t>(ui.firstVisibleSlot + row);
        const lv_coord_t y = static_cast<lv_coord_t>(
            surface.y1 + headerHeight + gap + row * rowHeight);
        const lv_area_t rail{
            surface.x1,
            y,
            static_cast<lv_coord_t>(surface.x1 + sceneRailWidth - 1),
            static_cast<lv_coord_t>(y + rowHeight - gap - 1),
        };
        bool sceneUsed = false;
        for (uint8_t track = 0U;
             track < seq::SequencerClipGridState::TRACK_COUNT;
             ++track) {
            if ((props_.enabledTrackMask &
                 static_cast<uint16_t>(1U << track)) != 0U &&
                props_.clips->slotKind({track, slot}) !=
                    seq::SequencerLauncherSlotKind::EMPTY) {
                sceneUsed = true;
                break;
            }
        }
        const bool visibleScene = slot <= lastScene;
        const bool addScene = visibleScene && slot == lastScene && !sceneUsed;
        const bool focused = ui.sceneFocused() && ui.focusedSlot == slot;
        const bool playing = sceneTelemetry.activeScene == slot;
        const bool queued = sceneTelemetry.queuedScene == slot &&
            sceneTelemetry.status == seq::SequencerClipLaunchStatus::QUEUED;
        drawRect(
            layer,
            rail,
            theme::color::SURFACE_IDLE,
            visibleScene ? LV_OPA_COVER : LV_OPA_20,
            focused ? theme::color::TEXT_PRIMARY
                    : playing ? theme::color::LIVE_TIME
                    : theme::color::BORDER_SUBTLE,
            focused || playing ? 2 : 1,
            visibleScene ? LV_OPA_COVER : LV_OPA_20
        );
        if (queued) {
            drawQueuedCorners(layer, rail, theme::color::ROUTING);
            drawCountdownRing(
                layer,
                static_cast<lv_coord_t>((rail.x1 + rail.x2) / 2),
                static_cast<lv_coord_t>((rail.y1 + rail.y2) / 2),
                11,
                sceneTelemetry.queuedRemainingQ8,
                theme::color::ROUTING
            );
        } else if (playing && sceneTelemetry.activeRemainingQ8 != 0U) {
            drawCountdownRing(
                layer,
                static_cast<lv_coord_t>((rail.x1 + rail.x2) / 2),
                static_cast<lv_coord_t>((rail.y1 + rail.y2) / 2),
                11,
                sceneTelemetry.activeRemainingQ8,
                theme::color::LIVE_TIME
            );
        }
        const bool countdown = queued ||
            (playing && sceneTelemetry.activeRemainingQ8 != 0U);
        if (visibleScene && (addScene || !countdown)) {
            drawText(
                layer,
                rail,
                addScene
                    ? standalone::icons::ACTION_CREATE
                    : standalone::icons::TRANSPORT_PLAY,
                addScene ? theme::color::TEXT_DISABLED
                         : playing ? theme::color::LIVE_TIME
                         : queued ? theme::color::ROUTING
                                  : theme::color::TEXT_SECONDARY,
                addScene ? LV_OPA_60 : LV_OPA_COVER,
                standalone_fonts.icons_16
            );
        }
    }

    for (uint8_t column = 0U;
         column < seq::ClipWorkspaceUiState::VISIBLE_TRACKS;
         ++column) {
        const uint8_t track = static_cast<uint8_t>(
            ui.firstVisibleTrack + column
        );
        const bool enabled = track < seq::ClipWorkspaceUiState::TRACK_COUNT &&
            (props_.enabledTrackMask &
            static_cast<uint16_t>(1U << track)) != 0U;
        const bool addSlot = track == addTrack;
        const bool navigable = enabled || addSlot;
        const uint32_t trackColor = theme::color::trackColor(track);
        const lv_coord_t x = static_cast<lv_coord_t>(
            gridX + column * (columnWidth + gap)
        );
        const lv_area_t header{
            .x1 = x,
            .y1 = surface.y1,
            .x2 = static_cast<lv_coord_t>(x + columnWidth - 1),
            .y2 = static_cast<lv_coord_t>(surface.y1 + headerHeight - 1),
        };
        const bool headerFocused = selectingTracks
            ? props_.trackNavigation->selection.cursorIndex.get() == track
            : ui.trackHeaderFocused() && ui.focusedTrack == track;
        const bool headerSelected = selectingTracks &&
            (props_.trackNavigation->selection.selectedMask.get() &
             static_cast<uint16_t>(1U << track)) != 0U;
        const uint8_t activity = enabled && props_.statusBar != nullptr
            ? props_.statusBar->trackNoteActivity[track].get()
            : 0U;
        drawRect(
            layer,
            header,
            headerSelected
                ? theme::color::SURFACE_RAISED
                : theme::color::SURFACE_IDLE,
            navigable ? LV_OPA_COVER : LV_OPA_20,
            headerFocused
                ? theme::color::TEXT_PRIMARY
                : theme::color::BORDER_SUBTLE,
            headerFocused ? 2 : 1,
            navigable ? LV_OPA_COVER : LV_OPA_20
        );
        if (activity != 0U) {
            const lv_area_t pulse{
                static_cast<lv_coord_t>(header.x1 + 1),
                static_cast<lv_coord_t>(header.y1 + 1),
                static_cast<lv_coord_t>(header.x2 - 1),
                static_cast<lv_coord_t>(header.y2 - 1),
            };
            drawRect(
                layer,
                pulse,
                trackColor,
                static_cast<lv_opa_t>(
                    32U + (static_cast<uint16_t>(activity) * 64U) / 127U
                ),
                trackColor,
                0,
                LV_OPA_TRANSP,
                2
            );
        }
        const auto telemetry = props_.launches->telemetry(track);
        const bool queuedStop = enabled &&
            telemetry.status == seq::SequencerClipLaunchStatus::QUEUED &&
            telemetry.action == seq::SequencerClipLaunchAction::STOP &&
            telemetry.queuedSlot ==
                seq::SequencerClipGridState::INVALID_SLOT;
        drawRect(
            layer,
            lv_area_t{
                .x1 = header.x1,
                .y1 = header.y1,
                .x2 = static_cast<lv_coord_t>(header.x1 + 2),
                .y2 = header.y2,
            },
            trackColor,
            enabled ? LV_OPA_COVER : LV_OPA_30,
            trackColor,
            0,
            LV_OPA_TRANSP,
            0
        );
        const lv_coord_t headerCenterX = static_cast<lv_coord_t>(
            (header.x1 + header.x2) / 2
        );
        const lv_coord_t headerCenterY = static_cast<lv_coord_t>(
            (header.y1 + header.y2) / 2
        );
        if (addSlot) {
            drawText(
                layer,
                header,
                standalone::icons::ACTION_CREATE,
                theme::color::TEXT_SECONDARY,
                LV_OPA_60,
                standalone_fonts.icons_16
            );
        } else if (enabled && telemetry.stopped) {
            drawSquare(
                layer,
                headerCenterX,
                headerCenterY,
                7,
                theme::color::DESTRUCTIVE
            );
        } else if (queuedStop) {
            drawSquare(
                layer,
                headerCenterX,
                headerCenterY,
                6,
                theme::color::ROUTING
            );
            drawCountdownRing(
                layer,
                headerCenterX,
                headerCenterY,
                8,
                telemetry.queuedRemainingQ8,
                theme::color::ROUTING
            );
        } else if (enabled) {
            drawText(
                layer,
                header,
                props_.tracks->isDrumTrack(track)
                    ? standalone::icons::DRUM_GENERIC
                    : standalone::icons::NOTE,
                activity != 0U ? theme::color::TEXT_PRIMARY : trackColor,
                LV_OPA_COVER,
                standalone_fonts.icons_16
            );
        }
        for (uint8_t row = 0U;
             row < seq::ClipWorkspaceUiState::VISIBLE_ROWS;
             ++row) {
            const uint8_t slot = static_cast<uint8_t>(
                ui.firstVisibleSlot + row
            );
            const seq::SequencerClipAddress address{track, slot};
            const auto kind = enabled
                ? props_.clips->slotKind(address)
                : seq::SequencerLauncherSlotKind::EMPTY;
            const bool occupied = kind == seq::SequencerLauncherSlotKind::CLIP;
            const bool stop = kind == seq::SequencerLauncherSlotKind::STOP;
            const bool focused = ui.clipFocused() &&
                ui.focusedTrack == track &&
                ui.focusedSlot == slot;
            const bool sourceSelected = ui.selectionActive() &&
                ui.sourceTrack == track && ui.sourceSlot == slot;
            const bool destinationFocused = ui.placementActive() && focused;
            const bool active = occupied && !telemetry.stopped &&
                telemetry.activeSlot == slot;
            const bool trackQueued =
                telemetry.status == seq::SequencerClipLaunchStatus::QUEUED &&
                telemetry.action != seq::SequencerClipLaunchAction::NONE;
            const bool queued = trackQueued && telemetry.queuedSlot == slot;
            const bool outgoing = active && trackQueued &&
                telemetry.queuedSlot != slot;
            const lv_coord_t y = static_cast<lv_coord_t>(
                surface.y1 + headerHeight + gap + row * rowHeight
            );
            const lv_area_t cell{
                .x1 = x,
                .y1 = y,
                .x2 = static_cast<lv_coord_t>(x + columnWidth - 1),
                .y2 = static_cast<lv_coord_t>(y + rowHeight - gap - 1),
            };
            const bool rowAvailable = slot <= lastScene;
            const bool disabledSecondary = !enabled || !rowAvailable;
            drawRect(
                layer,
                cell,
                destinationFocused
                    ? theme::color::SURFACE_RAISED
                    : theme::color::SURFACE_IDLE,
                disabledSecondary ? LV_OPA_20 : LV_OPA_COVER,
                focused ? theme::color::TEXT_PRIMARY
                        : outgoing ? theme::color::WARNING
                        : active ? theme::color::LIVE_TIME
                        : stop ? theme::color::DESTRUCTIVE
                        : sourceSelected
                            ? theme::color::BORDER_STRONG
                            : theme::color::BORDER_SUBTLE,
                focused || sourceSelected || active || stop ? 2 : 1,
                disabledSecondary ? LV_OPA_20 : LV_OPA_80
            );

            if (occupied) {
                drawRect(
                    layer,
                    cell,
                    trackColor,
                    active ? LV_OPA_30 : LV_OPA_20,
                    trackColor,
                    0,
                    LV_OPA_TRANSP
                );
            }
            if (occupied || stop) {
                if (stop) {
                    drawSquare(
                        layer,
                        static_cast<lv_coord_t>((cell.x1 + cell.x2) / 2),
                        static_cast<lv_coord_t>((cell.y1 + cell.y2) / 2),
                        9,
                        theme::color::DESTRUCTIVE
                    );
                } else {
                    const auto& preview = previews_[
                        column * seq::ClipWorkspaceUiState::VISIBLE_ROWS + row
                    ];
                    const lv_area_t previewArea{
                        static_cast<lv_coord_t>(cell.x1 + 4),
                        static_cast<lv_coord_t>(cell.y1 + 3),
                        static_cast<lv_coord_t>(cell.x2 - 4),
                        static_cast<lv_coord_t>(cell.y2 - 5),
                    };
                    if (preview.content &&
                        areasOverlap(previewArea, layer->_clip_area)) {
                        const lv_coord_t previewWidth = static_cast<lv_coord_t>(
                            lv_area_get_width(&previewArea)
                        );
                        const lv_coord_t previewHeight = static_cast<lv_coord_t>(
                            lv_area_get_height(&previewArea)
                        );
                        for (uint8_t previewRow = 0U;
                             previewRow < ClipPreview::ROWS;
                             ++previewRow) {
                            uint8_t previewColumn = 0U;
                            while (previewColumn < ClipPreview::COLUMNS) {
                                const std::size_t index =
                                    static_cast<std::size_t>(previewRow) *
                                        ClipPreview::COLUMNS + previewColumn;
                                const uint8_t velocity = preview.velocity[index];
                                if (velocity == 0U) {
                                    ++previewColumn;
                                    continue;
                                }
                                uint8_t runEnd = static_cast<uint8_t>(
                                    previewColumn + 1U
                                );
                                while (runEnd < ClipPreview::COLUMNS) {
                                    const std::size_t next =
                                        static_cast<std::size_t>(previewRow) *
                                            ClipPreview::COLUMNS + runEnd;
                                    if (preview.velocity[next] != velocity ||
                                        (preview.onsetMask[previewRow] &
                                         static_cast<uint32_t>(1UL << runEnd)) !=
                                            0U) {
                                        break;
                                    }
                                    ++runEnd;
                                }
                                const lv_coord_t x1 = static_cast<lv_coord_t>(
                                    previewArea.x1 +
                                    (static_cast<uint32_t>(previewWidth) *
                                     previewColumn) / ClipPreview::COLUMNS
                                );
                                const lv_coord_t x2 = static_cast<lv_coord_t>(
                                    previewArea.x1 +
                                    (static_cast<uint32_t>(previewWidth) *
                                     runEnd) /
                                        ClipPreview::COLUMNS - 1
                                );
                                const lv_coord_t y1 = static_cast<lv_coord_t>(
                                    previewArea.y1 +
                                    (static_cast<uint32_t>(previewHeight) *
                                     previewRow) / ClipPreview::ROWS
                                );
                                const lv_coord_t y2 = static_cast<lv_coord_t>(
                                    previewArea.y1 +
                                    (static_cast<uint32_t>(previewHeight) *
                                     (previewRow + 1U)) /
                                        ClipPreview::ROWS - 2
                                );
                                drawRect(
                                    layer,
                                    {x1, y1, std::max(x1, x2), std::max(y1, y2)},
                                    trackColor,
                                    static_cast<lv_opa_t>(std::min<uint16_t>(
                                        255U,
                                        55U +
                                            (static_cast<uint16_t>(velocity) *
                                             200U) / 127U
                                    )),
                                    trackColor,
                                    0,
                                    LV_OPA_TRANSP,
                                    0
                                );
                                if ((preview.onsetMask[previewRow] &
                                     static_cast<uint32_t>(
                                         1UL << previewColumn
                                     )) != 0U) {
                                    drawRect(
                                        layer,
                                        {x1, y1, x1, std::max(y1, y2)},
                                        trackColor,
                                        LV_OPA_COVER,
                                        trackColor,
                                        0,
                                        LV_OPA_TRANSP,
                                        0
                                    );
                                }
                                previewColumn = runEnd;
                            }
                        }
                        if (preview.loopColumn != ClipPreview::INVALID_COLUMN) {
                            const lv_coord_t loopX = static_cast<lv_coord_t>(
                                previewArea.x1 +
                                (static_cast<uint32_t>(previewWidth) *
                                 preview.loopColumn) / ClipPreview::COLUMNS
                            );
                            drawRect(
                                layer,
                                {loopX, previewArea.y1, loopX, previewArea.y2},
                                theme::color::TEXT_SECONDARY,
                                LV_OPA_60,
                                theme::color::TEXT_SECONDARY,
                                0,
                                LV_OPA_TRANSP,
                                0
                            );
                        }
                    }
                }
            } else if (!disabledSecondary) {
                drawText(
                    layer,
                    cell,
                    standalone::icons::ACTION_CREATE,
                    theme::color::TEXT_DISABLED,
                    LV_OPA_50,
                    standalone_fonts.icons_16
                );
            }
            if (active) {
                const lv_coord_t progressX1 = static_cast<lv_coord_t>(
                    cell.x1 + 4
                );
                const lv_coord_t progressX2 = static_cast<lv_coord_t>(
                    cell.x2 - 4
                );
                const lv_coord_t progressWidth = static_cast<lv_coord_t>(
                    progressX2 - progressX1 + 1
                );
                const lv_coord_t progressHead = static_cast<lv_coord_t>(
                    progressX1 +
                    (static_cast<uint32_t>(progressWidth - 1) *
                        telemetry.activePhaseQ8) /
                        255U
                );
                drawRect(
                    layer,
                    lv_area_t{
                        .x1 = progressX1,
                        .y1 = static_cast<lv_coord_t>(cell.y2 - 2),
                        .x2 = progressX2,
                        .y2 = cell.y2,
                    },
                    theme::color::BORDER_SUBTLE,
                    LV_OPA_60,
                    theme::color::BORDER_SUBTLE,
                    0,
                    LV_OPA_TRANSP,
                    1
                );
                drawRect(
                    layer,
                    lv_area_t{
                        .x1 = progressX1,
                        .y1 = static_cast<lv_coord_t>(cell.y2 - 2),
                        .x2 = progressHead,
                        .y2 = cell.y2,
                    },
                    trackColor,
                    LV_OPA_COVER,
                    trackColor,
                    0,
                    LV_OPA_TRANSP,
                    1
                );
            }
            if (queued) {
                drawQueuedCorners(layer, cell, theme::color::ROUTING);
                const lv_area_t countArea{
                    .x1 = static_cast<lv_coord_t>(cell.x2 - 16),
                    .y1 = static_cast<lv_coord_t>(cell.y1 + 2),
                    .x2 = static_cast<lv_coord_t>(cell.x2 - 2),
                    .y2 = static_cast<lv_coord_t>(cell.y1 + 16),
                };
                drawRect(
                    layer,
                    countArea,
                    theme::color::BACKGROUND,
                    LV_OPA_80,
                    theme::color::ROUTING,
                    0,
                    LV_OPA_TRANSP,
                    6
                );
                drawCountdownRing(
                    layer,
                    static_cast<lv_coord_t>((countArea.x1 + countArea.x2) / 2),
                    static_cast<lv_coord_t>((countArea.y1 + countArea.y2) / 2),
                    6,
                    telemetry.queuedRemainingQ8,
                    theme::color::ROUTING
                );
                drawSquare(
                    layer,
                    static_cast<lv_coord_t>((countArea.x1 + countArea.x2) / 2),
                    static_cast<lv_coord_t>((countArea.y1 + countArea.y2) / 2),
                    2,
                    theme::color::ROUTING
                );
            } else if (active && telemetry.activeRemainingQ8 != 0U) {
                const lv_area_t ringArea{
                    .x1 = static_cast<lv_coord_t>(cell.x2 - 16),
                    .y1 = static_cast<lv_coord_t>(cell.y1 + 2),
                    .x2 = static_cast<lv_coord_t>(cell.x2 - 2),
                    .y2 = static_cast<lv_coord_t>(cell.y1 + 16),
                };
                drawRect(
                    layer,
                    ringArea,
                    theme::color::BACKGROUND,
                    LV_OPA_80,
                    theme::color::BACKGROUND,
                    0,
                    LV_OPA_TRANSP,
                    6
                );
                drawCountdownRing(
                    layer,
                    static_cast<lv_coord_t>((ringArea.x1 + ringArea.x2) / 2),
                    static_cast<lv_coord_t>((ringArea.y1 + ringArea.y2) / 2),
                    6,
                    telemetry.activeRemainingQ8,
                    theme::color::LIVE_TIME
                );
                drawSquare(
                    layer,
                    static_cast<lv_coord_t>((ringArea.x1 + ringArea.x2) / 2),
                    static_cast<lv_coord_t>((ringArea.y1 + ringArea.y2) / 2),
                    2,
                    theme::color::LIVE_TIME
                );
            }
            if (sourceSelected) {
                const char* sourceIcon = ui.operation ==
                        seq::ClipWorkspaceOperation::MOVE_DESTINATION
                    ? standalone::icons::ACTION_MOVE
                    : standalone::icons::ACTION_COPY;
                drawText(
                    layer,
                    lv_area_t{
                        .x1 = static_cast<lv_coord_t>(cell.x1 + 2),
                        .y1 = static_cast<lv_coord_t>(cell.y1 + 2),
                        .x2 = static_cast<lv_coord_t>(cell.x1 + 19),
                        .y2 = static_cast<lv_coord_t>(cell.y1 + 19),
                    },
                    sourceIcon,
                    theme::color::TEXT_SECONDARY,
                    LV_OPA_COVER,
                    standalone_fonts.icons_14
                );
            }
            if (destinationFocused) {
                drawText(
                    layer,
                    lv_area_t{
                        .x1 = static_cast<lv_coord_t>(cell.x2 - 19),
                        .y1 = static_cast<lv_coord_t>(cell.y1 + 2),
                        .x2 = static_cast<lv_coord_t>(cell.x2 - 2),
                        .y2 = static_cast<lv_coord_t>(cell.y1 + 19),
                    },
                    occupied
                        ? standalone::icons::STATUS_CONFLICT
                        : standalone::icons::ACTION_PLACE_TARGET,
                    occupied
                        ? theme::color::DESTRUCTIVE
                        : theme::color::POSITIVE,
                    LV_OPA_COVER,
                    standalone_fonts.icons_14
                );
            }
        }
        if (ui.trackHeaderFocused() && ui.focusedTrack == track && navigable) {
            drawRect(
                layer,
                {header.x1, header.y1, header.x2, surface.y2},
                theme::color::BACKGROUND,
                LV_OPA_TRANSP,
                theme::color::TEXT_PRIMARY,
                2,
                LV_OPA_COVER
            );
        }
    }

    if (ui.sceneFocused()) {
        const uint8_t local = static_cast<uint8_t>(
            ui.focusedSlot - ui.firstVisibleSlot);
        if (local < seq::ClipWorkspaceUiState::VISIBLE_ROWS) {
            const lv_coord_t y = static_cast<lv_coord_t>(
                surface.y1 + headerHeight + gap + local * rowHeight);
            drawRect(
                layer,
                {surface.x1, y, surface.x2,
                 static_cast<lv_coord_t>(y + rowHeight - gap - 1)},
                theme::color::BACKGROUND,
                LV_OPA_TRANSP,
                theme::color::TEXT_PRIMARY,
                2,
                LV_OPA_COVER
            );
        }
    }

    if (sceneTelemetry.replaced) {
        const lv_area_t toast{
            static_cast<lv_coord_t>(surface.x1 + 56),
            static_cast<lv_coord_t>(surface.y2 - 23),
            static_cast<lv_coord_t>(surface.x2 - 6),
            static_cast<lv_coord_t>(surface.y2 - 3),
        };
        drawRect(
            layer,
            toast,
            theme::color::SURFACE_RAISED,
            LV_OPA_COVER,
            theme::color::ROUTING,
            1,
            LV_OPA_COVER
        );
        drawText(
            layer,
            toast,
            "QUEUE REPLACED",
            theme::color::ROUTING,
            LV_OPA_COVER,
            fonts.meta_label()
        );
    }
}

}  // namespace core::ui::sequencer

#include "ui/sequencer/SequencerClipLauncherSurface.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

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
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = font ? font : LV_FONT_DEFAULT;
    dsc.color = lv_color_hex(color);
    dsc.opa = opacity;
    dsc.align = alignment;
    lv_draw_label(layer, &dsc, &area);
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

FLASHMEM const char* areaLabel(seq::ClipWorkspaceFocus focus) {
    switch (focus) {
        case seq::ClipWorkspaceFocus::SCENE: return "Scene";
        case seq::ClipWorkspaceFocus::TRACK_HEADER: return "Track";
        case seq::ClipWorkspaceFocus::CLIP:
        default: return "Clip";
    }
}

FLASHMEM const char* quantizationLabel(uint8_t value) {
    switch (value) {
        case 1U: return "1 BEAT";
        case 2U: return "1 BAR";
        default: return "GLOBAL";
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
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(root_);
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
    for (uint8_t column = 0U;
         column < seq::ClipWorkspaceUiState::VISIBLE_TRACKS;
         ++column) {
        const uint8_t track = static_cast<uint8_t>(
            ui.firstVisibleTrack + column
        );
        if (track >= seq::ClipWorkspaceUiState::TRACK_COUNT) continue;
        const auto telemetry = props_.launches->telemetry(track);
        if (telemetry.stopped ||
            telemetry.activeSlot < ui.firstVisibleSlot ||
            telemetry.activeSlot >=
                ui.firstVisibleSlot + seq::ClipWorkspaceUiState::VISIBLE_ROWS) {
            continue;
        }
        const uint8_t row = static_cast<uint8_t>(
            telemetry.activeSlot - ui.firstVisibleSlot
        );
        const lv_coord_t x = static_cast<lv_coord_t>(
            layout.gridX + column *
                (layout.columnWidth + LauncherLayout::GAP)
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
        std::array<char, 32> title{};
        if (ui.editor == seq::ClipWorkspaceEditor::SLOT_ACTION) {
            std::snprintf(
                title.data(), title.size(), "SLOT T%u / S%u",
                static_cast<unsigned>(ui.focusedTrack + 1U),
                static_cast<unsigned>(ui.focusedSlot + 1U)
            );
        } else if (ui.editor == seq::ClipWorkspaceEditor::CLIP_BEHAVIOR) {
            std::snprintf(
                title.data(), title.size(), "CLIP T%u / C%u",
                static_cast<unsigned>(ui.focusedTrack + 1U),
                static_cast<unsigned>(ui.focusedSlot + 1U)
            );
        } else {
            std::snprintf(
                title.data(), title.size(), "SCENE S%u",
                static_cast<unsigned>(ui.focusedSlot + 1U)
            );
        }
        drawText(
            layer,
            {surface.x1, surface.y1, surface.x2,
             static_cast<lv_coord_t>(surface.y1 + 20)},
            title.data(),
            theme::color::TEXT_PRIMARY,
            LV_OPA_COVER,
            fonts.compact_selected(),
            LV_TEXT_ALIGN_LEFT
        );

        constexpr lv_coord_t editorTop = 26;
        constexpr lv_coord_t editorRowHeight = 34;
        if (ui.editor == seq::ClipWorkspaceEditor::SLOT_ACTION) {
            constexpr std::array<const char*, 3> labels{{
                "CREATE CLIP", "SET STOP", "CLEAR STOP"
            }};
            for (uint8_t row = 0U; row < labels.size(); ++row) {
                const lv_area_t item{
                    static_cast<lv_coord_t>(surface.x1 + 4),
                    static_cast<lv_coord_t>(surface.y1 + editorTop +
                        row * editorRowHeight),
                    static_cast<lv_coord_t>(surface.x2 - 4),
                    static_cast<lv_coord_t>(surface.y1 + editorTop +
                        row * editorRowHeight + editorRowHeight - 5),
                };
                const bool focused = static_cast<uint8_t>(ui.slotAction) == row;
                drawRect(
                    layer,
                    item,
                    focused ? theme::color::SURFACE_RAISED
                            : theme::color::SURFACE_IDLE,
                    LV_OPA_COVER,
                    focused ? theme::color::TEXT_PRIMARY
                            : theme::color::BORDER_SUBTLE,
                    focused ? 2 : 1,
                    LV_OPA_COVER
                );
                drawText(
                    layer,
                    item,
                    labels[row],
                    row == 1U ? theme::color::DESTRUCTIVE
                              : theme::color::TEXT_PRIMARY,
                    focused ? LV_OPA_COVER : LV_OPA_70,
                    fonts.meta_label()
                );
            }
        } else {
            constexpr std::array<const char*, 3> fieldLabels{{
                "LENGTH", "THEN", "QUANTIZE"
            }};
            for (uint8_t row = 0U; row < fieldLabels.size(); ++row) {
                const lv_area_t item{
                    static_cast<lv_coord_t>(surface.x1 + 4),
                    static_cast<lv_coord_t>(surface.y1 + editorTop +
                        row * editorRowHeight),
                    static_cast<lv_coord_t>(surface.x2 - 4),
                    static_cast<lv_coord_t>(surface.y1 + editorTop +
                        row * editorRowHeight + editorRowHeight - 5),
                };
                const bool focused = static_cast<uint8_t>(ui.editorField) == row;
                drawRect(
                    layer,
                    item,
                    focused ? theme::color::SURFACE_RAISED
                            : theme::color::SURFACE_IDLE,
                    LV_OPA_COVER,
                    focused ? theme::color::TEXT_PRIMARY
                            : theme::color::BORDER_SUBTLE,
                    focused ? 2 : 1,
                    LV_OPA_COVER
                );
                drawText(
                    layer,
                    {static_cast<lv_coord_t>(item.x1 + 8), item.y1,
                     static_cast<lv_coord_t>(item.x1 + 92), item.y2},
                    fieldLabels[row],
                    theme::color::TEXT_SECONDARY,
                    LV_OPA_COVER,
                    fonts.meta_label(),
                    LV_TEXT_ALIGN_LEFT
                );
                std::array<char, 20> value{};
                if (row == 0U) {
                    if (ui.editorLength == 0U) {
                        std::snprintf(value.data(), value.size(), "OFF");
                    } else {
                        std::snprintf(
                            value.data(), value.size(), "%u %s",
                            static_cast<unsigned>(ui.editorLength),
                            ui.editor ==
                                    seq::ClipWorkspaceEditor::CLIP_BEHAVIOR
                                ? "LOOPS" : "BARS"
                        );
                    }
                } else if (row == 1U) {
                    if (ui.editorThenTarget ==
                        seq::SequencerLauncherBehavior::NO_TARGET) {
                        std::snprintf(value.data(), value.size(), "NONE");
                    } else {
                        std::snprintf(
                            value.data(), value.size(), "%s %u",
                            ui.editor == seq::ClipWorkspaceEditor::CLIP_BEHAVIOR
                                ? "CLIP" : "SCENE",
                            static_cast<unsigned>(ui.editorThenTarget + 1U)
                        );
                    }
                } else {
                    std::snprintf(
                        value.data(), value.size(), "%s",
                        quantizationLabel(ui.editorQuantization)
                    );
                }
                drawText(
                    layer,
                    {static_cast<lv_coord_t>(item.x1 + 96), item.y1,
                     static_cast<lv_coord_t>(item.x2 - 8), item.y2},
                    value.data(),
                    theme::color::TEXT_PRIMARY,
                    LV_OPA_COVER,
                    fonts.compact_selected(),
                    LV_TEXT_ALIGN_RIGHT
                );
            }
        }
        drawText(
            layer,
            {surface.x1,
             static_cast<lv_coord_t>(surface.y2 - 20),
             surface.x2,
             surface.y2},
            "NAV: FIELD   LC+NAV: VALUE   NAV: APPLY",
            theme::color::TEXT_SECONDARY,
            LV_OPA_80,
            fonts.meta_label()
        );
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
    drawText(
        layer,
        areaHeader,
        areaLabel(ui.focusArea),
        theme::color::TEXT_PRIMARY,
        LV_OPA_COVER,
        fonts.meta_label()
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
            playing ? 0x10271F : theme::color::SURFACE_IDLE,
            visibleScene ? LV_OPA_COVER : LV_OPA_20,
            focused ? theme::color::TEXT_PRIMARY
                    : playing ? theme::color::LIVE_TIME
                    : theme::color::BORDER_SUBTLE,
            focused || playing ? 2 : 1,
            visibleScene ? LV_OPA_COVER : LV_OPA_20
        );
        std::array<char, 8> sceneLabel{};
        std::snprintf(
            sceneLabel.data(), sceneLabel.size(), addScene ? "+S%u" : "S%u",
            static_cast<unsigned>(slot + 1U));
        drawText(
            layer,
            rail,
            visibleScene ? sceneLabel.data() : "",
            addScene ? theme::color::TEXT_DISABLED
                     : playing ? theme::color::LIVE_TIME
                     : theme::color::TEXT_PRIMARY,
            LV_OPA_COVER,
            fonts.meta_label()
        );
        if (queued) {
            drawQueuedCorners(layer, rail, theme::color::ROUTING);
            std::array<char, 4> count{};
            std::snprintf(
                count.data(), count.size(), "%u",
                static_cast<unsigned>(sceneTelemetry.beatsRemaining));
            drawText(
                layer,
                {static_cast<lv_coord_t>(rail.x2 - 12), rail.y1,
                 rail.x2, static_cast<lv_coord_t>(rail.y1 + 12)},
                count.data(),
                theme::color::ROUTING,
                LV_OPA_COVER,
                fonts.meta_label()
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
        const auto telemetry = props_.launches->telemetry(track);
        const bool queuedStop = enabled &&
            telemetry.status == seq::SequencerClipLaunchStatus::QUEUED &&
            telemetry.action == seq::SequencerClipLaunchAction::STOP &&
            telemetry.queuedSlot ==
                seq::SequencerClipGridState::INVALID_SLOT;
        const bool stopStatus = enabled &&
            (telemetry.stopped || queuedStop);
        std::array<char, 8> trackLabel{};
        std::snprintf(
            trackLabel.data(), trackLabel.size(), addSlot ? "+T%u" : "T%u",
            static_cast<unsigned>(track + 1U)
        );
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
        drawText(
            layer,
            stopStatus
                ? lv_area_t{
                    static_cast<lv_coord_t>(header.x1 + 5),
                    header.y1,
                    static_cast<lv_coord_t>(header.x1 + 26),
                    header.y2,
                }
                : header,
            trackLabel.data(),
            enabled ? theme::color::TEXT_PRIMARY
                    : addSlot ? theme::color::TEXT_SECONDARY
                              : theme::color::TEXT_DISABLED,
            LV_OPA_COVER,
            fonts.compact_selected(),
            stopStatus ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER
        );

        const char* kindIcon = enabled && !stopStatus &&
                props_.tracks->isDrumTrack(track)
            ? standalone::icons::DRUM_GENERIC
            : enabled && !stopStatus ? standalone::icons::NOTE : "";
        const lv_area_t kindArea{
            .x1 = static_cast<lv_coord_t>(header.x2 - 17),
            .y1 = header.y1,
            .x2 = header.x2,
            .y2 = header.y2,
        };
        drawText(
            layer,
            kindArea,
            kindIcon,
            trackColor,
            enabled ? LV_OPA_80 : LV_OPA_TRANSP,
            standalone_fonts.icons_14
        );

        if (enabled && telemetry.stopped) {
            drawText(
                layer,
                {static_cast<lv_coord_t>(header.x1 + 2), header.y1,
                 static_cast<lv_coord_t>(header.x2 - 2), header.y2},
                "STOP",
                theme::color::DESTRUCTIVE,
                LV_OPA_COVER,
                fonts.meta_label(),
                LV_TEXT_ALIGN_RIGHT
            );
        } else if (queuedStop) {
            // "STOP " + the full uint8_t range + terminator.
            std::array<char, 9> stopCount{};
            std::snprintf(
                stopCount.data(), stopCount.size(), "STOP %u",
                static_cast<unsigned>(telemetry.beatsRemaining));
            drawText(
                layer,
                {header.x1, header.y1, static_cast<lv_coord_t>(header.x2 - 2),
                 header.y2},
                stopCount.data(),
                theme::color::ROUTING,
                LV_OPA_COVER,
                fonts.meta_label(),
                LV_TEXT_ALIGN_RIGHT
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
                std::array<char, 12> clipLabel{};
                if (stop) {
                    std::snprintf(clipLabel.data(), clipLabel.size(), "Stop");
                } else {
                    std::snprintf(
                        clipLabel.data(),
                        clipLabel.size(),
                        "Clip %u",
                        static_cast<unsigned>(slot + 1U)
                    );
                }
                drawText(
                    layer,
                    lv_area_t{
                        .x1 = static_cast<lv_coord_t>(cell.x1 + 3),
                        .y1 = cell.y1,
                        .x2 = static_cast<lv_coord_t>(
                            cell.x2 - (queued ? 18 : 3)
                        ),
                        .y2 = static_cast<lv_coord_t>(cell.y2 - 4),
                    },
                    clipLabel.data(),
                    stop ? theme::color::DESTRUCTIVE
                         : theme::color::TEXT_PRIMARY,
                    focused || active ? LV_OPA_COVER : LV_OPA_80,
                    fonts.compact_selected()
                );
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
                std::array<char, 4> count{};
                std::snprintf(
                    count.data(), count.size(), "%u",
                    static_cast<unsigned>(telemetry.beatsRemaining));
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
                    1,
                    LV_OPA_COVER,
                    6
                );
                drawText(
                    layer,
                    countArea,
                    count.data(),
                    theme::color::ROUTING,
                    LV_OPA_COVER,
                    fonts.meta_label()
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

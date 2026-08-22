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
    const lv_coord_t width = lv_area_get_width(&surface);
    const lv_coord_t height = lv_area_get_height(&surface);
    constexpr lv_coord_t gap = 3;
    constexpr lv_coord_t headerHeight = 18;
    const lv_coord_t columnWidth = static_cast<lv_coord_t>(
        (width - gap * 3) / seq::ClipWorkspaceUiState::VISIBLE_TRACKS
    );
    const lv_coord_t rowHeight = std::max<lv_coord_t>(
        1,
        static_cast<lv_coord_t>(
            (height - headerHeight - gap) /
            seq::ClipWorkspaceUiState::VISIBLE_ROWS
        )
    );
    const auto& ui = *props_.ui;
    const bool selectingTracks = props_.trackNavigation != nullptr &&
        props_.trackNavigation->selection.active.get() &&
        props_.trackNavigation->selection.scope.get() ==
            core::state::StructureSelectionScope::TRACK;

    for (uint8_t column = 0U;
         column < seq::ClipWorkspaceUiState::VISIBLE_TRACKS;
         ++column) {
        const uint8_t track = static_cast<uint8_t>(
            ui.firstVisibleTrack + column
        );
        const bool enabled = (props_.enabledTrackMask &
            static_cast<uint16_t>(1U << track)) != 0U;
        const uint32_t trackColor = theme::color::trackColor(track);
        const lv_coord_t x = static_cast<lv_coord_t>(
            surface.x1 + column * (columnWidth + gap)
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
            LV_OPA_COVER,
            headerFocused
                ? theme::color::FOCUS_EDIT
                : theme::color::BORDER_SUBTLE,
            headerFocused ? 2 : 0,
            headerFocused ? LV_OPA_COVER : LV_OPA_TRANSP
        );
        std::array<char, 8> trackLabel{};
        std::snprintf(
            trackLabel.data(), trackLabel.size(), "T%u",
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
            header,
            trackLabel.data(),
            enabled ? theme::color::TEXT_PRIMARY
                    : theme::color::TEXT_DISABLED,
            LV_OPA_COVER,
            fonts.compact_selected()
        );

        const char* kindIcon = enabled && props_.tracks->isDrumTrack(track)
            ? standalone::icons::DRUM_GENERIC
            : standalone::icons::NOTE;
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
            enabled ? LV_OPA_80 : LV_OPA_20,
            standalone_fonts.icons_14
        );

        const auto telemetry = props_.launches->telemetry(track);
        for (uint8_t row = 0U;
             row < seq::ClipWorkspaceUiState::VISIBLE_ROWS;
             ++row) {
            const uint8_t slot = static_cast<uint8_t>(
                ui.firstVisibleSlot + row
            );
            const seq::SequencerClipAddress address{track, slot};
            const bool occupied = enabled && props_.clips->isOccupied(address);
            const bool focused = ui.clipFocused() &&
                ui.focusedTrack == track &&
                ui.focusedSlot == slot;
            const bool sourceSelected = ui.selectionActive() &&
                ui.sourceTrack == track && ui.sourceSlot == slot;
            const bool destinationFocused = ui.placementActive() && focused;
            const bool active = occupied && telemetry.activeSlot == slot;
            const bool queued = occupied &&
                telemetry.status == seq::SequencerClipLaunchStatus::QUEUED &&
                telemetry.queuedSlot == slot;
            const lv_coord_t y = static_cast<lv_coord_t>(
                surface.y1 + headerHeight + gap + row * rowHeight
            );
            const lv_area_t cell{
                .x1 = x,
                .y1 = y,
                .x2 = static_cast<lv_coord_t>(x + columnWidth - 1),
                .y2 = static_cast<lv_coord_t>(y + rowHeight - gap - 1),
            };
            const bool disabledSecondary = !enabled && slot != 0U;
            drawRect(
                layer,
                cell,
                destinationFocused
                    ? theme::color::SURFACE_RAISED
                    : theme::color::SURFACE_IDLE,
                disabledSecondary ? LV_OPA_20 : LV_OPA_COVER,
                focused ? theme::color::FOCUS_EDIT
                        : sourceSelected
                            ? theme::color::BORDER_STRONG
                            : theme::color::BORDER_SUBTLE,
                focused || sourceSelected ? 2 : 1,
                disabledSecondary ? LV_OPA_20 : LV_OPA_80
            );

            const char* icon = occupied
                ? standalone::icons::CLIP
                : (!enabled && slot != 0U)
                    ? ""
                    : standalone::icons::ACTION_CREATE;
            drawText(
                layer,
                lv_area_t{
                    .x1 = cell.x1,
                    .y1 = static_cast<lv_coord_t>(cell.y1 + 5),
                    .x2 = cell.x2,
                    .y2 = static_cast<lv_coord_t>(cell.y1 + 26),
                },
                icon,
                occupied ? trackColor : theme::color::TEXT_DISABLED,
                occupied ? LV_OPA_COVER : LV_OPA_70,
                standalone_fonts.icons_16
            );
            if (occupied) {
                std::array<char, 8> clipLabel{};
                std::snprintf(
                    clipLabel.data(), clipLabel.size(), "C%u",
                    static_cast<unsigned>(slot + 1U)
                );
                drawText(
                    layer,
                    lv_area_t{
                        .x1 = cell.x1,
                        .y1 = static_cast<lv_coord_t>(cell.y1 + 25),
                        .x2 = cell.x2,
                        .y2 = static_cast<lv_coord_t>(cell.y2 - 4),
                    },
                    clipLabel.data(),
                    theme::color::TEXT_PRIMARY,
                    focused ? LV_OPA_COVER : LV_OPA_70,
                    fonts.meta_label()
                );
            }
            if (active) {
                drawRect(
                    layer,
                    lv_area_t{
                        .x1 = static_cast<lv_coord_t>(cell.x1 + 4),
                        .y1 = static_cast<lv_coord_t>(cell.y2 - 2),
                        .x2 = static_cast<lv_coord_t>(cell.x2 - 4),
                        .y2 = cell.y2,
                    },
                    theme::color::LIVE_TIME,
                    LV_OPA_COVER,
                    theme::color::LIVE_TIME,
                    0,
                    LV_OPA_TRANSP,
                    1
                );
            }
            if (queued) {
                drawText(
                    layer,
                    lv_area_t{
                        .x1 = static_cast<lv_coord_t>(cell.x2 - 19),
                        .y1 = static_cast<lv_coord_t>(cell.y1 + 2),
                        .x2 = static_cast<lv_coord_t>(cell.x2 - 2),
                        .y2 = static_cast<lv_coord_t>(cell.y1 + 19),
                    },
                    standalone::icons::STATUS_QUEUED,
                    theme::color::FOCUS_EDIT,
                    LV_OPA_COVER,
                    standalone_fonts.icons_14
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
    }
}

}  // namespace core::ui::sequencer

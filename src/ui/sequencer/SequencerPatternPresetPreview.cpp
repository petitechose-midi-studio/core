#include "ui/sequencer/SequencerPatternPresetPreview.hpp"

#include <algorithm>
#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/font/StandaloneFonts.hpp"
#include "ui/sequencer/DrumLaneVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer {
namespace {

namespace seq = core::state::sequencer;
namespace theme = standalone::theme;

constexpr lv_coord_t SURFACE_HEIGHT = 162;
constexpr lv_coord_t PAD = 6;
constexpr lv_coord_t DRUM_LABEL_WIDTH = 54;
constexpr lv_coord_t DRUM_ROW_HEIGHT = 10;
constexpr lv_coord_t MELODIC_GRID_HEIGHT = 82;
constexpr lv_opa_t GRID_OPACITY = static_cast<lv_opa_t>(150);

void drawRect(
    lv_layer_t* layer,
    const lv_area_t& area,
    uint32_t color,
    lv_opa_t opacity,
    uint8_t radius = 0U
) {
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = lv_color_hex(color);
    descriptor.bg_opa = opacity;
    descriptor.border_width = 0;
    descriptor.radius = radius;
    lv_draw_rect(layer, &descriptor, &area);
}

void drawLabel(
    lv_layer_t* layer,
    const lv_area_t& area,
    const char* text,
    const lv_font_t* font,
    uint32_t color,
    lv_opa_t opacity = LV_OPA_COVER,
    lv_text_align_t align = LV_TEXT_ALIGN_LEFT
) {
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.text = text ? text : "";
    descriptor.font = font ? font : LV_FONT_DEFAULT;
    descriptor.color = lv_color_hex(color);
    descriptor.opa = opacity;
    descriptor.align = align;
    lv_draw_label(layer, &descriptor, &area);
}

lv_coord_t cellLeft(
    lv_coord_t gridLeft,
    lv_coord_t gridWidth,
    uint8_t step,
    uint8_t stepCount
) {
    return static_cast<lv_coord_t>(
        gridLeft + (static_cast<int32_t>(gridWidth) * step) / stepCount
    );
}

lv_coord_t cellRight(
    lv_coord_t gridLeft,
    lv_coord_t gridWidth,
    uint8_t step,
    uint8_t stepCount
) {
    return static_cast<lv_coord_t>(
        gridLeft +
        (static_cast<int32_t>(gridWidth) * (step + 1U)) / stepCount - 1
    );
}

void drawBeatGuides(
    lv_layer_t* layer,
    lv_coord_t gridLeft,
    lv_coord_t gridTop,
    lv_coord_t gridWidth,
    lv_coord_t gridBottom,
    uint8_t stepCount
) {
    for (uint8_t step = 4U; step < stepCount; step += 4U) {
        const lv_coord_t x = cellLeft(gridLeft, gridWidth, step, stepCount);
        drawRect(
            layer,
            {x, gridTop, x, gridBottom},
            theme::color::BORDER_STRONG,
            LV_OPA_60
        );
    }
}

lv_coord_t drawDrumPreview(
    lv_layer_t* layer,
    const lv_area_t& area,
    const seq::SequencerPatternPresetVisualSummary& visual
) {
    const uint8_t laneCount = std::max<uint8_t>(1U, visual.laneCount);
    const uint8_t stepCount = std::max<uint8_t>(1U, visual.visibleStepCount);
    const lv_coord_t gridLeft = static_cast<lv_coord_t>(
        area.x1 + PAD + DRUM_LABEL_WIDTH
    );
    const lv_coord_t gridWidth = static_cast<lv_coord_t>(
        lv_area_get_width(&area) - (2 * PAD) - DRUM_LABEL_WIDTH
    );
    const lv_coord_t gridTop = static_cast<lv_coord_t>(area.y1 + 3);
    const lv_coord_t gridBottom = static_cast<lv_coord_t>(
        gridTop + laneCount * DRUM_ROW_HEIGHT - 1
    );

    drawBeatGuides(
        layer,
        gridLeft,
        gridTop,
        gridWidth,
        gridBottom,
        stepCount
    );
    for (uint8_t lane = 0U; lane < laneCount; ++lane) {
        const lv_coord_t y = static_cast<lv_coord_t>(
            gridTop + lane * DRUM_ROW_HEIGHT
        );
        const uint32_t laneColor = theme::color::trackColor(
            visual.drumColorIndices[lane]
        );
        drawLabel(
            layer,
            {
                static_cast<lv_coord_t>(area.x1 + PAD),
                y,
                static_cast<lv_coord_t>(area.x1 + PAD + 13),
                static_cast<lv_coord_t>(y + DRUM_ROW_HEIGHT - 1),
            },
            drumLaneIconGlyph(visual.drumIcons[lane]),
            standalone_fonts.icons_12,
            laneColor
        );
        drawLabel(
            layer,
            {
                static_cast<lv_coord_t>(area.x1 + PAD + 15),
                y,
                static_cast<lv_coord_t>(gridLeft - 3),
                static_cast<lv_coord_t>(y + DRUM_ROW_HEIGHT - 1),
            },
            visual.drumNames[lane].data(),
            fonts.meta_label(),
            lane == 0U
                ? theme::color::TEXT_PRIMARY
                : theme::color::TEXT_SECONDARY,
            lane == 0U ? LV_OPA_COVER : LV_OPA_80
        );

        for (uint8_t step = 0U; step < stepCount; ++step) {
            const lv_coord_t left = cellLeft(
                gridLeft, gridWidth, step, stepCount
            );
            const lv_coord_t right = cellRight(
                gridLeft, gridWidth, step, stepCount
            );
            drawRect(
                layer,
                {
                    static_cast<lv_coord_t>(left + 1),
                    static_cast<lv_coord_t>(y + 1),
                    static_cast<lv_coord_t>(right - 1),
                    static_cast<lv_coord_t>(y + DRUM_ROW_HEIGHT - 2),
                },
                theme::color::SURFACE_RAISED,
                GRID_OPACITY,
                1U
            );
            if ((visual.drumEnabledMasks[lane] &
                 static_cast<uint16_t>(UINT16_C(1) << step)) == 0U) {
                continue;
            }
            drawRect(
                layer,
                {
                    static_cast<lv_coord_t>(left + 2),
                    static_cast<lv_coord_t>(y + 2),
                    static_cast<lv_coord_t>(right - 2),
                    static_cast<lv_coord_t>(y + DRUM_ROW_HEIGHT - 3),
                },
                laneColor,
                LV_OPA_COVER,
                1U
            );
        }
    }
    return static_cast<lv_coord_t>(gridBottom + 6);
}

lv_coord_t drawMelodicPreview(
    lv_layer_t* layer,
    const lv_area_t& area,
    const seq::SequencerPatternPresetVisualSummary& visual
) {
    const uint8_t stepCount = std::max<uint8_t>(1U, visual.visibleStepCount);
    const lv_coord_t gridLeft = static_cast<lv_coord_t>(area.x1 + PAD);
    const lv_coord_t gridTop = static_cast<lv_coord_t>(area.y1 + 4);
    const lv_coord_t gridWidth = static_cast<lv_coord_t>(
        lv_area_get_width(&area) - 2 * PAD
    );
    const lv_coord_t gridBottom = static_cast<lv_coord_t>(
        gridTop + MELODIC_GRID_HEIGHT - 1
    );

    uint8_t minNote = 127U;
    uint8_t maxNote = 0U;
    for (uint8_t step = 0U; step < stepCount; ++step) {
        if ((visual.melodicEnabledMask &
             static_cast<uint16_t>(UINT16_C(1) << step)) == 0U) {
            continue;
        }
        minNote = std::min<uint8_t>(minNote, visual.notes[step]);
        maxNote = std::max<uint8_t>(maxNote, visual.notes[step]);
    }
    if (minNote > maxNote) {
        minNote = 60U;
        maxNote = 71U;
    }
    if (static_cast<uint8_t>(maxNote - minNote) < 11U) {
        const uint8_t center = static_cast<uint8_t>(
            (static_cast<uint16_t>(minNote) + maxNote) / 2U
        );
        minNote = center > 5U ? static_cast<uint8_t>(center - 5U) : 0U;
        maxNote = static_cast<uint8_t>(
            std::min<unsigned>(127U, minNote + 11U)
        );
    }
    const uint8_t pitchSpan = std::max<uint8_t>(1U, maxNote - minNote);

    for (uint8_t row = 0U; row <= 6U; ++row) {
        const lv_coord_t y = static_cast<lv_coord_t>(
            gridTop + (static_cast<int32_t>(MELODIC_GRID_HEIGHT - 1) * row) / 6
        );
        drawRect(
            layer,
            {gridLeft, y, static_cast<lv_coord_t>(gridLeft + gridWidth - 1), y},
            theme::color::BORDER_SUBTLE,
            LV_OPA_30
        );
    }
    drawBeatGuides(
        layer,
        gridLeft,
        gridTop,
        gridWidth,
        gridBottom,
        stepCount
    );

    for (uint8_t step = 0U; step < stepCount; ++step) {
        const lv_coord_t left = cellLeft(
            gridLeft, gridWidth, step, stepCount
        );
        const lv_coord_t right = cellRight(
            gridLeft, gridWidth, step, stepCount
        );
        drawRect(
            layer,
            {
                static_cast<lv_coord_t>(left + 1),
                gridTop,
                static_cast<lv_coord_t>(right - 1),
                gridBottom,
            },
            theme::color::SURFACE_IDLE,
            LV_OPA_60,
            1U
        );
        if ((visual.melodicEnabledMask &
             static_cast<uint16_t>(UINT16_C(1) << step)) == 0U) {
            continue;
        }
        const uint8_t pitch = std::clamp<uint8_t>(
            visual.notes[step], minNote, maxNote
        );
        const lv_coord_t y = static_cast<lv_coord_t>(
            gridBottom -
            (static_cast<int32_t>(pitch - minNote) *
             (MELODIC_GRID_HEIGHT - 5)) /
                pitchSpan -
            2
        );
        const lv_opa_t velocityOpacity = static_cast<lv_opa_t>(
            70U +
            (static_cast<uint16_t>(visual.velocities[step]) * 185U) / 127U
        );
        drawRect(
            layer,
            {
                static_cast<lv_coord_t>(left + 2),
                y,
                static_cast<lv_coord_t>(right - 2),
                static_cast<lv_coord_t>(y + 3),
            },
            theme::color::STEP_PITCH,
            velocityOpacity,
            1U
        );
        drawRect(
            layer,
            {
                static_cast<lv_coord_t>(left + 2),
                static_cast<lv_coord_t>(y - 1),
                static_cast<lv_coord_t>(left + 3),
                static_cast<lv_coord_t>(y + 4),
            },
            theme::color::STEP_PITCH,
            LV_OPA_COVER,
            1U
        );
    }
    return static_cast<lv_coord_t>(gridBottom + 7);
}

void drawMetadata(
    lv_layer_t* layer,
    const lv_area_t& area,
    lv_coord_t y,
    const seq::SequencerPatternPresetDescriptor& descriptor
) {
    const auto& visual = descriptor.visual;
    char timing[48]{};
    char content[48]{};
    const unsigned division = 4U * descriptor.stepsPerBeat;
    if (descriptor.metadata.trackKind == seq::SequencerTrackKind::DRUM) {
        std::snprintf(
            timing,
            sizeof(timing),
            "%u lanes · %u steps · 1/%u",
            static_cast<unsigned>(descriptor.drumLaneCount),
            static_cast<unsigned>(descriptor.patternLength),
            division
        );
    } else {
        std::snprintf(
            timing,
            sizeof(timing),
            "Instrument · %u steps · 1/%u",
            static_cast<unsigned>(descriptor.patternLength),
            division
        );
    }
    std::snprintf(
        content,
        sizeof(content),
        "Micro %u · Cycle %u · CC %u",
        static_cast<unsigned>(visual.microSequenceCount),
        static_cast<unsigned>(visual.cycleStateCount),
        static_cast<unsigned>(visual.ccLaneCount)
    );

    constexpr lv_coord_t LINE_HEIGHT = 15;
    const lv_coord_t left = static_cast<lv_coord_t>(area.x1 + PAD);
    const lv_coord_t right = static_cast<lv_coord_t>(area.x2 - PAD);
    drawLabel(
        layer,
        {left, y, right, static_cast<lv_coord_t>(y + LINE_HEIGHT - 1)},
        timing,
        fonts.meta_label(),
        theme::color::TEXT_PRIMARY
    );
    y = static_cast<lv_coord_t>(y + LINE_HEIGHT);
    drawLabel(
        layer,
        {left, y, right, static_cast<lv_coord_t>(y + LINE_HEIGHT - 1)},
        content,
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY
    );
    y = static_cast<lv_coord_t>(y + LINE_HEIGHT);
    drawLabel(
        layer,
        {left, y, right, static_cast<lv_coord_t>(y + LINE_HEIGHT - 1)},
        "Replace pattern · route kept",
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY,
        LV_OPA_80
    );
}

}  // namespace

FLASHMEM void SequencerPatternPresetPreview::create(lv_obj_t* parent) {
    if (!parent || surface_) return;
    surface_ = lv_obj_create(parent);
    lv_obj_remove_style_all(surface_);
    lv_obj_set_size(surface_, LV_PCT(96), SURFACE_HEIGHT);
    lv_obj_align(surface_, LV_ALIGN_TOP_MID, 0, 34);
    lv_obj_clear_flag(surface_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(surface_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(surface_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(surface_, onDraw, LV_EVENT_DRAW_MAIN, this);
}

FLASHMEM void SequencerPatternPresetPreview::render(
    const SequencerPatternPresetPreviewProps& props
) {
    if (!surface_) return;
    const bool nextVisible = props.visible && props.descriptor != nullptr &&
        props.descriptor->visual.valid;
    if (!nextVisible) {
        if (visible_) lv_obj_add_flag(surface_, LV_OBJ_FLAG_HIDDEN);
        visible_ = false;
        return;
    }

    const bool changed = !visible_ || revision_ != props.revision;
    if (changed) {
        descriptor_ = *props.descriptor;
        revision_ = props.revision;
    }
    if (!visible_) {
        lv_obj_clear_flag(surface_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(surface_);
        visible_ = true;
    }
    if (changed) lv_obj_invalidate(surface_);
}

FLASHMEM void SequencerPatternPresetPreview::draw(lv_layer_t* layer) const {
    if (!layer || !surface_ || !descriptor_.visual.valid) return;
    lv_area_t area{};
    lv_obj_get_coords(surface_, &area);
    const lv_coord_t metadataY =
        descriptor_.metadata.trackKind == seq::SequencerTrackKind::DRUM
        ? drawDrumPreview(layer, area, descriptor_.visual)
        : drawMelodicPreview(layer, area, descriptor_.visual);
    drawMetadata(layer, area, metadataY, descriptor_);
}

FLASHMEM void SequencerPatternPresetPreview::onDraw(lv_event_t* event) {
    auto* self = static_cast<SequencerPatternPresetPreview*>(
        lv_event_get_user_data(event)
    );
    if (self) self->draw(lv_event_get_layer(event));
}

}  // namespace core::ui::sequencer

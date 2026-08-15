#include "ui/sequencer/SequencerCcLaneGrid.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/ui/lvgl/StaticSurfaceInvalidation.hpp>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {
namespace {

namespace theme = standalone::theme;

constexpr lv_coord_t GRID_X = 8;
constexpr lv_coord_t GRID_Y = 34;
constexpr lv_coord_t GRID_WIDTH = 304;
constexpr lv_coord_t GRID_HEIGHT = 130;
constexpr lv_coord_t CELL_WIDTH = 35;
constexpr lv_coord_t CELL_PITCH = 38;
constexpr lv_coord_t CURVE_FIRST_X = 17;
constexpr lv_coord_t CURVE_TOP = 24;
constexpr lv_coord_t CURVE_HEIGHT = 72;
constexpr lv_coord_t STEP_LABEL_TOP = 4;
constexpr lv_coord_t STEP_LABEL_HEIGHT = 15;
constexpr lv_coord_t VALUE_LABEL_TOP = 103;
constexpr lv_coord_t VALUE_LABEL_HEIGHT = 15;
constexpr lv_coord_t POINT_SIZE = 5;

template <size_t N>
FLASHMEM bool copyText(std::array<char, N>& destination, const char* source) {
    const char* text = source ? source : "";
    if (std::strncmp(destination.data(), text, N) == 0) return false;
    std::strncpy(destination.data(), text, N - 1U);
    destination[N - 1U] = '\0';
    return true;
}

FLASHMEM lv_obj_t* createLabel(
    lv_obj_t* parent,
    const lv_font_t* font,
    uint32_t color,
    lv_text_align_t align
) {
    auto* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font ? font : LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

FLASHMEM const char* transitionName(
    core::state::sequencer::SequencerCcLaneTransition transition
) {
    using Transition = core::state::sequencer::SequencerCcLaneTransition;
    switch (transition) {
        case Transition::HOLD: return "Hold";
        case Transition::LINEAR: return "Linear";
        case Transition::EASE_IN: return "Ease In";
        case Transition::EASE_OUT: return "Ease Out";
        case Transition::EASE_IN_OUT: return "Ease In/Out";
    }
    return "Hold";
}

FLASHMEM const char* transitionDescription(
    core::state::sequencer::SequencerCcLaneTransition transition
) {
    using Transition = core::state::sequencer::SequencerCcLaneTransition;
    switch (transition) {
        case Transition::HOLD: return "Step then jump";
        case Transition::LINEAR: return "Straight";
        case Transition::EASE_IN: return "Slow start";
        case Transition::EASE_OUT: return "Soft landing";
        case Transition::EASE_IN_OUT: return "Smooth both";
    }
    return "";
}

FLASHMEM lv_coord_t curveY(uint8_t value) {
    return static_cast<lv_coord_t>(
        CURVE_TOP + CURVE_HEIGHT - 1 -
        (static_cast<uint16_t>(value) * (CURVE_HEIGHT - 1)) / 127U
    );
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

FLASHMEM void drawLabel(
    lv_layer_t* layer,
    const lv_area_t& area,
    const char* text,
    uint32_t color,
    lv_opa_t opacity,
    lv_text_align_t alignment = LV_TEXT_ALIGN_CENTER
) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = fonts.meta_label() ? fonts.meta_label() : LV_FONT_DEFAULT;
    dsc.color = lv_color_hex(color);
    dsc.opa = opacity;
    dsc.align = alignment;
    lv_draw_label(layer, &dsc, &area);
}

FLASHMEM bool sameStaticCell(
    const SequencerCcLaneGridCell& left,
    const SequencerCcLaneGridCell& right
) {
    return left.visible == right.visible &&
           left.authored == right.authored &&
           left.focused == right.focused &&
           left.step == right.step &&
           left.value == right.value &&
           left.transition == right.transition;
}

FLASHMEM bool sameSegment(
    const SequencerCcLaneGridCurveSegment& left,
    const SequencerCcLaneGridCurveSegment& right
) {
    if (left.visible != right.visible || left.pointCount != right.pointCount) {
        return false;
    }
    const uint8_t count = std::min<uint8_t>(
        left.pointCount,
        static_cast<uint8_t>(left.points.size())
    );
    for (uint8_t point = 0; point < count; ++point) {
        if (left.points[point].position != right.points[point].position ||
            left.points[point].value != right.points[point].value) {
            return false;
        }
    }
    return true;
}

}  // namespace

FLASHMEM SequencerCcLaneGrid::SequencerCcLaneGrid(
    lv_obj_t* parent,
    SequencerCcLaneGridLayout layout
) : layout_(layout) {
    createUi(parent);
}

FLASHMEM SequencerCcLaneGrid::~SequencerCcLaneGrid() {
    if (root_) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

FLASHMEM void SequencerCcLaneGrid::createUi(lv_obj_t* parent) {
    if (!parent) return;

    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(theme::color::BACKGROUND), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    title_ = createLabel(
        root_,
        fonts.context_title(),
        theme::color::TEXT_PRIMARY,
        LV_TEXT_ALIGN_LEFT
    );
    lv_obj_set_pos(title_, 10, 8);
    lv_obj_set_size(title_, 140, 18);

    meta_ = createLabel(
        root_,
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY,
        LV_TEXT_ALIGN_RIGHT
    );
    lv_obj_set_pos(meta_, 150, 9);
    lv_obj_set_size(meta_, 160, 16);

    surface_ = lv_obj_create(root_);
    lv_obj_remove_style_all(surface_);
    lv_obj_add_flag(surface_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(surface_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(
        surface_,
        GRID_X,
        layout_ == SequencerCcLaneGridLayout::EMBEDDED ? 4 : GRID_Y
    );
    lv_obj_set_size(surface_, GRID_WIDTH, GRID_HEIGHT);
    lv_obj_clear_flag(surface_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(
        surface_,
        onSurfaceDrawEvent,
        LV_EVENT_DRAW_MAIN,
        this
    );

    hint_ = createLabel(
        root_,
        fonts.meta_label(),
        theme::color::TEXT_SECONDARY,
        LV_TEXT_ALIGN_CENTER
    );
    lv_obj_set_style_text_opa(hint_, LV_OPA_80, 0);
    lv_obj_set_pos(
        hint_,
        8,
        layout_ == SequencerCcLaneGridLayout::EMBEDDED ? 138 : 174
    );
    lv_obj_set_size(hint_, 304, 16);

    if (layout_ == SequencerCcLaneGridLayout::EMBEDDED) {
        lv_obj_add_flag(title_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(meta_, LV_OBJ_FLAG_HIDDEN);
    }
}

FLASHMEM bool SequencerCcLaneGrid::staticVisualChanged(
    const SequencerCcLaneGridProps& props
) const {
    const uint32_t accent = props.accentColor == 0
        ? theme::color::MACRO_CC_COLOR
        : props.accentColor;
    if (!rendered_ || accentColor_ != accent ||
        rendered_props_.transitionPicker != props.transitionPicker ||
        rendered_props_.compactTransitionPicker !=
            props.compactTransitionPicker ||
        rendered_props_.pickerSelection != props.pickerSelection) {
        return true;
    }
    for (size_t cell = 0; cell < props.cells.size(); ++cell) {
        if (!sameStaticCell(rendered_props_.cells[cell], props.cells[cell])) {
            return true;
        }
    }
    for (size_t segment = 0; segment < props.segments.size(); ++segment) {
        if (!sameSegment(
                rendered_props_.segments[segment],
                props.segments[segment]
            )) {
            return true;
        }
    }
    return false;
}

FLASHMEM void SequencerCcLaneGrid::drawTransitionChoice(
    lv_layer_t* layer,
    const lv_area_t& area,
    core::state::sequencer::SequencerCcLaneTransition transition,
    bool selected
) {
    using Transition = core::state::sequencer::SequencerCcLaneTransition;
    if (selected) {
        drawRect(
            layer,
            area,
            theme::color::FOCUS_EDIT,
            LV_OPA_10,
            1,
            LV_OPA_COVER,
            2
        );
    }

    constexpr lv_coord_t curveWidth = 46;
    constexpr lv_coord_t curveHeight = 16;
    const lv_coord_t height = static_cast<lv_coord_t>(area.y2 - area.y1 + 1);
    const lv_coord_t curveX = static_cast<lv_coord_t>(area.x1 + 8);
    const lv_coord_t curveYTop = static_cast<lv_coord_t>(
        area.y1 + std::max<lv_coord_t>(2, (height - curveHeight) / 2)
    );
    uint8_t pointCount = 0;
    if (transition == Transition::HOLD) {
        draw_points_[0] = {
            static_cast<lv_value_precise_t>(curveX),
            static_cast<lv_value_precise_t>(curveYTop + curveHeight - 1),
        };
        draw_points_[1] = {
            static_cast<lv_value_precise_t>(curveX + curveWidth - 1),
            static_cast<lv_value_precise_t>(curveYTop + curveHeight - 1),
        };
        draw_points_[2] = {
            static_cast<lv_value_precise_t>(curveX + curveWidth - 1),
            static_cast<lv_value_precise_t>(curveYTop),
        };
        pointCount = 3;
    } else {
        pointCount = static_cast<uint8_t>(draw_points_.size());
        for (uint8_t point = 0; point < pointCount; ++point) {
            const float progress = static_cast<float>(point) /
                static_cast<float>(pointCount - 1U);
            const uint8_t value =
                core::state::sequencer::interpolateSequencerCcLaneValue(
                    0, 127, transition, progress
                );
            draw_points_[point] = {
                static_cast<lv_value_precise_t>(
                    curveX + (static_cast<uint16_t>(point) *
                              (curveWidth - 1)) / (pointCount - 1U)
                ),
                static_cast<lv_value_precise_t>(
                    curveYTop + curveHeight - 1 -
                    (static_cast<uint16_t>(value) * (curveHeight - 1)) / 127U
                ),
            };
        }
    }
    lv_draw_line_dsc_t lineDsc;
    lv_draw_line_dsc_init(&lineDsc);
    lineDsc.base.layer = layer;
    lineDsc.points = draw_points_.data();
    lineDsc.point_cnt = pointCount;
    lineDsc.width = 2;
    lineDsc.color = lv_color_hex(accentColor_);
    lineDsc.opa = selected ? LV_OPA_COVER : LV_OPA_60;
    lv_draw_line(layer, &lineDsc);

    const lv_area_t nameArea{
        .x1 = static_cast<lv_coord_t>(area.x1 + 66),
        .y1 = static_cast<lv_coord_t>(area.y1 + 2),
        .x2 = static_cast<lv_coord_t>(area.x1 + 150),
        .y2 = static_cast<lv_coord_t>(area.y2 - 2),
    };
    drawLabel(layer, nameArea, transitionName(transition),
              theme::color::TEXT_PRIMARY,
              selected ? LV_OPA_COVER : LV_OPA_70,
              LV_TEXT_ALIGN_LEFT);
    const lv_area_t descriptionArea{
        .x1 = static_cast<lv_coord_t>(area.x1 + 154),
        .y1 = static_cast<lv_coord_t>(area.y1 + 2),
        .x2 = static_cast<lv_coord_t>(area.x2 - 5),
        .y2 = static_cast<lv_coord_t>(area.y2 - 2),
    };
    drawLabel(layer, descriptionArea, transitionDescription(transition),
              theme::color::TEXT_SECONDARY,
              selected ? LV_OPA_COVER : LV_OPA_60,
              LV_TEXT_ALIGN_LEFT);
}

FLASHMEM void SequencerCcLaneGrid::invalidatePlayheadCell(size_t index) {
    if (!surface_ || index >= CELL_COUNT) return;
    lv_area_t surfaceArea{};
    lv_obj_get_coords(surface_, &surfaceArea);
    const lv_coord_t x = static_cast<lv_coord_t>(
        surfaceArea.x1 + static_cast<lv_coord_t>(index) * CELL_PITCH
    );
    const lv_area_t markerArea{
        .x1 = x,
        .y1 = surfaceArea.y1,
        .x2 = static_cast<lv_coord_t>(x + CELL_WIDTH - 1),
        .y2 = static_cast<lv_coord_t>(surfaceArea.y1 + 2),
    };
    oc::ui::lvgl::invalidateStaticSurfaceArea(surface_, markerArea);
}

FLASHMEM void SequencerCcLaneGrid::drawCurveSegment(
    lv_layer_t* layer,
    const lv_area_t& surfaceArea,
    size_t index,
    lv_opa_t opacity,
    lv_coord_t width
) {
    if (!layer || index >= rendered_props_.segments.size()) return;
    const auto& segment = rendered_props_.segments[index];
    if (!segment.visible || segment.pointCount < 2U) return;
    const uint8_t count = std::min<uint8_t>(
        segment.pointCount,
        static_cast<uint8_t>(segment.points.size())
    );
    const lv_coord_t startX = static_cast<lv_coord_t>(
        surfaceArea.x1 + CURVE_FIRST_X +
        static_cast<lv_coord_t>(index) * CELL_PITCH
    );
    for (uint8_t point = 0; point < count; ++point) {
        const auto& sample = segment.points[point];
        draw_points_[point] = {
            static_cast<lv_value_precise_t>(
                startX +
                (static_cast<uint16_t>(sample.position) * CELL_PITCH) /
                    255U
            ),
            static_cast<lv_value_precise_t>(
                surfaceArea.y1 + curveY(sample.value)
            ),
        };
    }
    lv_draw_line_dsc_t lineDsc;
    lv_draw_line_dsc_init(&lineDsc);
    lineDsc.base.layer = layer;
    lineDsc.points = draw_points_.data();
    lineDsc.point_cnt = count;
    lineDsc.width = width;
    lineDsc.color = lv_color_hex(accentColor_);
    lineDsc.opa = opacity;
    lv_draw_line(layer, &lineDsc);
}

FLASHMEM void SequencerCcLaneGrid::drawSurface(lv_layer_t* layer) {
    if (!layer || !surface_ || !rendered_) return;

    lv_area_t surfaceArea{};
    lv_obj_get_coords(surface_, &surfaceArea);

    if (rendered_props_.transitionPicker) {
        using Transition =
            core::state::sequencer::SequencerCcLaneTransition;
        constexpr std::array<Transition, 5> transitions = {
            Transition::HOLD,
            Transition::LINEAR,
            Transition::EASE_IN,
            Transition::EASE_OUT,
            Transition::EASE_IN_OUT,
        };
        constexpr lv_coord_t rowHeight = 24;
        constexpr lv_coord_t rowGap = 2;
        if (rendered_props_.compactTransitionPicker) {
            constexpr lv_coord_t compactHeight = 40;
            const lv_coord_t compactY = static_cast<lv_coord_t>(
                surfaceArea.y1 + (GRID_HEIGHT - compactHeight) / 2
            );
            drawTransitionChoice(
                layer,
                {
                    .x1 = surfaceArea.x1,
                    .y1 = compactY,
                    .x2 = surfaceArea.x2,
                    .y2 = static_cast<lv_coord_t>(compactY + compactHeight - 1),
                },
                rendered_props_.pickerSelection,
                true
            );
            return;
        }
        for (size_t index = 0; index < transitions.size(); ++index) {
            const auto transition = transitions[index];
            const bool selected = transition ==
                rendered_props_.pickerSelection;
            const lv_coord_t rowY = static_cast<lv_coord_t>(
                surfaceArea.y1 + 1 +
                static_cast<lv_coord_t>(index) * (rowHeight + rowGap)
            );
            const lv_area_t rowArea{
                .x1 = surfaceArea.x1,
                .y1 = rowY,
                .x2 = surfaceArea.x2,
                .y2 = static_cast<lv_coord_t>(rowY + rowHeight - 1),
            };
            drawTransitionChoice(layer, rowArea, transition, selected);
        }
        return;
    }

    size_t focusedCell = CELL_COUNT;
    for (size_t cell = 0; cell < rendered_props_.cells.size(); ++cell) {
        if (rendered_props_.cells[cell].focused) {
            focusedCell = cell;
            break;
        }
    }

    // One dim trajectory establishes continuity. The focused step's outgoing
    // segment is then redrawn at full contrast, so interpolation direction is
    // readable without permanent transition glyphs.
    for (size_t segment = 0; segment < rendered_props_.segments.size(); ++segment) {
        drawCurveSegment(layer, surfaceArea, segment, LV_OPA_40, 2);
    }
    if (focusedCell < rendered_props_.segments.size()) {
        drawCurveSegment(layer, surfaceArea, focusedCell, LV_OPA_COVER, 2);
    }

    for (size_t index = 0; index < rendered_props_.cells.size(); ++index) {
        const auto& cell = rendered_props_.cells[index];
        if (!cell.visible) continue;
        const lv_coord_t x = static_cast<lv_coord_t>(
            surfaceArea.x1 + static_cast<lv_coord_t>(index) * CELL_PITCH
        );
        const lv_area_t cellArea{
            .x1 = x,
            .y1 = surfaceArea.y1,
            .x2 = static_cast<lv_coord_t>(x + CELL_WIDTH - 1),
            .y2 = static_cast<lv_coord_t>(surfaceArea.y1 + GRID_HEIGHT - 2),
        };
        if (cell.focused) {
            drawRect(
                layer,
                cellArea,
                theme::color::FOCUS_EDIT,
                LV_OPA_10,
                1,
                LV_OPA_COVER,
                2
            );
        }
        if (cell.playhead) {
            const lv_area_t playheadArea{
                .x1 = static_cast<lv_coord_t>(x + 3),
                .y1 = surfaceArea.y1,
                .x2 = static_cast<lv_coord_t>(x + CELL_WIDTH - 4),
                .y2 = static_cast<lv_coord_t>(surfaceArea.y1 + 1),
            };
            drawRect(
                layer,
                playheadArea,
                theme::color::LIVE_TIME,
                LV_OPA_COVER
            );
        }

        char stepText[4] = {};
        std::snprintf(
            stepText,
            sizeof(stepText),
            "%u",
            static_cast<unsigned>(cell.step + 1U)
        );
        const lv_area_t stepArea{
            .x1 = x,
            .y1 = static_cast<lv_coord_t>(surfaceArea.y1 + STEP_LABEL_TOP),
            .x2 = static_cast<lv_coord_t>(x + CELL_WIDTH - 1),
            .y2 = static_cast<lv_coord_t>(
                surfaceArea.y1 + STEP_LABEL_TOP + STEP_LABEL_HEIGHT - 1
            ),
        };
        drawLabel(
            layer,
            stepArea,
            stepText,
            theme::color::TEXT_SECONDARY,
            cell.focused ? LV_OPA_COVER : LV_OPA_70
        );

        char valueText[5] = "--";
        if (cell.authored) {
            std::snprintf(
                valueText,
                sizeof(valueText),
                "%u",
                static_cast<unsigned>(cell.value)
            );
            const lv_coord_t pointX = static_cast<lv_coord_t>(
                x + CURVE_FIRST_X - POINT_SIZE / 2
            );
            const lv_coord_t pointY = static_cast<lv_coord_t>(
                surfaceArea.y1 + curveY(cell.value) - POINT_SIZE / 2
            );
            const lv_area_t pointArea{
                .x1 = pointX,
                .y1 = pointY,
                .x2 = static_cast<lv_coord_t>(pointX + POINT_SIZE - 1),
                .y2 = static_cast<lv_coord_t>(pointY + POINT_SIZE - 1),
            };
            drawRect(
                layer,
                pointArea,
                accentColor_,
                cell.focused ? LV_OPA_COVER : LV_OPA_80
            );
        }
        const lv_area_t valueArea{
            .x1 = x,
            .y1 = static_cast<lv_coord_t>(surfaceArea.y1 + VALUE_LABEL_TOP),
            .x2 = static_cast<lv_coord_t>(x + CELL_WIDTH - 1),
            .y2 = static_cast<lv_coord_t>(
                surfaceArea.y1 + VALUE_LABEL_TOP + VALUE_LABEL_HEIGHT - 1
            ),
        };
        drawLabel(
            layer,
            valueArea,
            valueText,
            theme::color::TEXT_PRIMARY,
            cell.focused
                ? LV_OPA_COVER
                : (cell.authored ? LV_OPA_70 : LV_OPA_40)
        );
    }
}

FLASHMEM void SequencerCcLaneGrid::onSurfaceDrawEvent(lv_event_t* event) {
    auto* self = static_cast<SequencerCcLaneGrid*>(
        lv_event_get_user_data(event)
    );
    if (!self) return;
    self->drawSurface(lv_event_get_layer(event));
}

FLASHMEM void SequencerCcLaneGrid::render(
    const SequencerCcLaneGridProps& props
) {
    if (!root_) return;
    if (!props.visible) {
        if (visible_) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
            visible_ = false;
        }
        return;
    }

    if (!visible_) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
        visible_ = true;
        rendered_ = false;
    }

    if (copyText(titleText_, props.title)) {
        lv_label_set_text_static(title_, titleText_.data());
    }
    if (copyText(metaText_, props.meta)) {
        lv_label_set_text_static(meta_, metaText_.data());
    }

    const SequencerCcLaneGridCell* focusedCell = nullptr;
    for (const auto& cell : props.cells) {
        if (cell.visible && cell.focused) {
            focusedCell = &cell;
            break;
        }
    }

    char contextualHint[64] = {};
    const char* hint = props.hint;
    if (props.contextualHint) {
        const uint8_t value = focusedCell ? focusedCell->value : 0U;
        if (props.hintSourceStep == props.hintTargetStep) {
            std::snprintf(
                contextualHint,
                sizeof(contextualHint),
                "S%u · %u · Add curve point",
                static_cast<unsigned>(props.hintSourceStep) + 1U,
                static_cast<unsigned>(value)
            );
        } else {
            std::snprintf(
                contextualHint,
                sizeof(contextualHint),
                "S%u · %u · %s > S%u",
                static_cast<unsigned>(props.hintSourceStep) + 1U,
                static_cast<unsigned>(value),
                transitionName(props.hintTransition),
                static_cast<unsigned>(props.hintTargetStep) + 1U
            );
        }
        hint = contextualHint;
    } else if ((!hint || hint[0] == '\0') && focusedCell) {
        if (focusedCell->authored) {
            std::snprintf(
                contextualHint,
                sizeof(contextualHint),
                "S%u · %u · %s",
                static_cast<unsigned>(focusedCell->step) + 1U,
                static_cast<unsigned>(focusedCell->value),
                transitionName(focusedCell->transition)
            );
        } else {
            std::snprintf(
                contextualHint,
                sizeof(contextualHint),
                "S%u · Empty",
                static_cast<unsigned>(focusedCell->step) + 1U
            );
        }
        hint = contextualHint;
    }
    if (copyText(hintText_, hint)) {
        lv_label_set_text_static(hint_, hintText_.data());
    }
    if (statusColor_ != props.statusColor) {
        lv_obj_set_style_text_color(meta_, lv_color_hex(props.statusColor), 0);
        statusColor_ = props.statusColor;
    }

    const bool staticChanged = staticVisualChanged(props);
    std::array<bool, CELL_COUNT> playheadChanged{};
    if (rendered_ && !staticChanged) {
        for (size_t cell = 0; cell < props.cells.size(); ++cell) {
            playheadChanged[cell] =
                rendered_props_.cells[cell].playhead != props.cells[cell].playhead;
        }
    }

    accentColor_ = props.accentColor == 0
        ? theme::color::MACRO_CC_COLOR
        : props.accentColor;
    rendered_props_ = props;
    rendered_ = true;

    if (staticChanged) {
        if (surface_) lv_obj_invalidate(surface_);
        return;
    }
    for (size_t cell = 0; cell < playheadChanged.size(); ++cell) {
        if (playheadChanged[cell]) invalidatePlayheadCell(cell);
    }
}

}  // namespace core::ui

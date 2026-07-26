#include "ui/sequencer/SequencerPatternEditorOverlay.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/ui/lvgl/StaticSurfaceInvalidation.hpp>

#include "ui/font/StandaloneFonts.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {
namespace {

namespace theme = standalone::theme;
namespace timeline = core::ui::sequencer;

constexpr lv_coord_t TIMELINE_X = 8;
constexpr lv_coord_t TIMELINE_Y = 28;
constexpr lv_coord_t TIMELINE_WIDTH = 304;
constexpr lv_coord_t TIMELINE_HEIGHT = 132;
constexpr lv_coord_t FIELD_Y = 180;
constexpr lv_coord_t FIELD_WIDTH = 304;
constexpr lv_coord_t FIELD_HEIGHT = 20;
constexpr lv_opa_t OPACITY_15 = static_cast<lv_opa_t>(38);
constexpr lv_opa_t OPACITY_25 = static_cast<lv_opa_t>(64);
constexpr lv_opa_t OPACITY_35 = static_cast<lv_opa_t>(89);
constexpr lv_opa_t OPACITY_55 = static_cast<lv_opa_t>(140);

constexpr std::array<uint32_t, 4> CC_COLORS = {
    theme::color::MACRO_5,
    theme::color::MACRO_6,
    theme::color::MACRO_7,
    theme::color::MACRO_8,
};

template <std::size_t N>
bool copyText(std::array<char, N>& destination, const char* source) {
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

void drawRect(
    lv_layer_t* layer,
    const lv_area_t& area,
    uint32_t color,
    lv_opa_t fill,
    lv_coord_t borderWidth = 0,
    lv_opa_t borderOpacity = LV_OPA_TRANSP,
    lv_coord_t radius = 0
) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = fill;
    dsc.border_color = lv_color_hex(color);
    dsc.border_width = borderWidth;
    dsc.border_opa = borderOpacity;
    dsc.radius = radius;
    lv_draw_rect(layer, &dsc, &area);
}

void drawLine(
    lv_layer_t* layer,
    lv_coord_t x1,
    lv_coord_t y1,
    lv_coord_t x2,
    lv_coord_t y2,
    uint32_t color,
    lv_opa_t opacity,
    lv_coord_t width = 1
) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.base.layer = layer;
    dsc.p1 = {
        static_cast<lv_value_precise_t>(x1),
        static_cast<lv_value_precise_t>(y1),
    };
    dsc.p2 = {
        static_cast<lv_value_precise_t>(x2),
        static_cast<lv_value_precise_t>(y2),
    };
    dsc.color = lv_color_hex(color);
    dsc.opa = opacity;
    dsc.width = width;
    lv_draw_line(layer, &dsc);
}

FLASHMEM void flushCurveRun(
    lv_layer_t* layer,
    std::array<
        lv_point_precise_t,
        timeline::SEQUENCER_PATTERN_TIMELINE_MAX_WIDTH>& points,
    uint16_t& runCount,
    uint32_t color,
    lv_opa_t opacity,
    bool focused
) {
    if (runCount < 2U) {
        runCount = 0U;
        return;
    }
    if (runCount <= 8U) {
        for (uint16_t index = 1U; index < runCount; ++index) {
            const auto& p1 = points[index - 1U];
            const auto& p2 = points[index];
            drawLine(
                layer,
                static_cast<lv_coord_t>(p1.x),
                static_cast<lv_coord_t>(p1.y),
                static_cast<lv_coord_t>(p2.x),
                static_cast<lv_coord_t>(p2.y),
                color,
                opacity,
                focused ? 2 : 1
            );
        }
    } else {
        lv_draw_line_dsc_t dsc;
        lv_draw_line_dsc_init(&dsc);
        dsc.base.layer = layer;
        dsc.points = points.data();
        dsc.point_cnt = runCount;
        dsc.color = lv_color_hex(color);
        dsc.opa = opacity;
        dsc.width = focused ? 2 : 1;
        lv_draw_line(layer, &dsc);
    }
    runCount = 0U;
}

void drawLabel(
    lv_layer_t* layer,
    const lv_area_t& area,
    const char* text,
    const lv_font_t* font,
    uint32_t color,
    lv_opa_t opacity,
    lv_text_align_t align = LV_TEXT_ALIGN_CENTER
) {
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text ? text : "";
    dsc.font = font ? font : LV_FONT_DEFAULT;
    dsc.color = lv_color_hex(color);
    dsc.opa = opacity;
    dsc.align = align;
    lv_draw_label(layer, &dsc, &area);
}

bool ccLayerFocused(
    core::state::sequencer::SequencerPatternEditorLayer layer,
    uint8_t sourceLane
) {
    using Layer = core::state::sequencer::SequencerPatternEditorLayer;
    const auto expected = static_cast<uint8_t>(Layer::CC1) + sourceLane;
    return static_cast<uint8_t>(layer) == expected;
}

}  // namespace

FLASHMEM SequencerPatternEditorOverlay::SequencerPatternEditorOverlay(lv_obj_t* parent) {
    createUi(parent);
}

FLASHMEM SequencerPatternEditorOverlay::~SequencerPatternEditorOverlay() {
    if (root_) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

FLASHMEM void SequencerPatternEditorOverlay::createUi(lv_obj_t* parent) {
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
        root_, fonts.inter_14_semibold, theme::color::TEXT_PRIMARY, LV_TEXT_ALIGN_LEFT
    );
    lv_obj_set_pos(title_, 8, 5);
    lv_obj_set_size(title_, 168, 18);

    meta_ = createLabel(
        root_, fonts.inter_12_medium, theme::color::TEXT_SECONDARY, LV_TEXT_ALIGN_RIGHT
    );
    lv_obj_set_pos(meta_, 168, 6);
    lv_obj_set_size(meta_, 144, 16);

    timeline_ = lv_obj_create(root_);
    lv_obj_remove_style_all(timeline_);
    lv_obj_add_flag(timeline_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(timeline_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(timeline_, TIMELINE_X, TIMELINE_Y);
    lv_obj_set_size(timeline_, TIMELINE_WIDTH, TIMELINE_HEIGHT);
    lv_obj_clear_flag(timeline_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(timeline_, onTimelineDraw, LV_EVENT_DRAW_MAIN, this);

    playhead_surface_ = lv_obj_create(root_);
    lv_obj_remove_style_all(playhead_surface_);
    lv_obj_add_flag(playhead_surface_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(playhead_surface_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(playhead_surface_, TIMELINE_X, TIMELINE_Y);
    lv_obj_set_size(playhead_surface_, TIMELINE_WIDTH, TIMELINE_HEIGHT);
    lv_obj_clear_flag(playhead_surface_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(
        playhead_surface_, onPlayheadDraw, LV_EVENT_DRAW_MAIN, this
    );

    layer_ = createLabel(
        root_, fonts.inter_12_medium, theme::color::TEXT_PRIMARY, LV_TEXT_ALIGN_LEFT
    );
    lv_obj_set_pos(layer_, 12, 31);
    lv_obj_set_size(layer_, 92, 15);
    lv_obj_move_foreground(layer_);

    transient_hint_ = createLabel(
        root_, fonts.inter_12_medium, theme::color::TEXT_SECONDARY, LV_TEXT_ALIGN_CENTER
    );
    lv_obj_set_style_text_opa(transient_hint_, LV_OPA_80, 0);
    lv_obj_set_pos(transient_hint_, 8, 162);
    lv_obj_set_size(transient_hint_, 304, 15);

    fields_ = lv_obj_create(root_);
    lv_obj_remove_style_all(fields_);
    lv_obj_add_flag(fields_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(fields_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(fields_, 8, FIELD_Y);
    lv_obj_set_size(fields_, FIELD_WIDTH, FIELD_HEIGHT);
    lv_obj_clear_flag(fields_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(fields_, onFieldsDraw, LV_EVENT_DRAW_MAIN, this);
}

FLASHMEM void SequencerPatternEditorOverlay::invalidateTimeline() {
    if (timeline_) lv_obj_invalidate(timeline_);
}

FLASHMEM void SequencerPatternEditorOverlay::invalidateFields() {
    if (fields_) lv_obj_invalidate(fields_);
}

FLASHMEM void SequencerPatternEditorOverlay::render(
    const SequencerPatternEditorOverlayProps& props
) {
    if (!root_) return;
    OC_PERF_SCOPE(perfMutation, "ui.sequencer.pattern-editor.mutation");
    OC_PERF_UNITS(perfMutation, props.geometryRevision, props.fieldCount);
    if (!props.visible) {
        if (visible_) {
            lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
            visible_ = false;
        }
        return;
    }

    bool headerChanged = false;
    headerChanged = copyText(title_text_, props.title) || headerChanged;
    headerChanged = copyText(meta_text_, props.meta) || headerChanged;
    headerChanged = copyText(layer_text_, props.layer) || headerChanged;
    headerChanged = copyText(hint_text_, props.transientHint) || headerChanged;
    if (headerChanged || !rendered_) {
        lv_label_set_text_static(title_, title_text_.data());
        lv_label_set_text_static(meta_, meta_text_.data());
        lv_label_set_text_static(layer_, layer_text_.data());
        lv_label_set_text_static(transient_hint_, hint_text_.data());
    }

    const uint8_t requestedFieldCount = std::clamp<uint8_t>(
        props.fieldCount,
        1U,
        static_cast<uint8_t>(field_cache_.size())
    );
    bool fieldsChanged = !rendered_ || field_count_ != requestedFieldCount;
    field_count_ = requestedFieldCount;
    for (std::size_t index = 0; index < field_cache_.size(); ++index) {
        auto& cached = field_cache_[index];
        const auto& incoming = props.fields[index];
        fieldsChanged = copyText(cached.icon, incoming.icon) || fieldsChanged;
        fieldsChanged = copyText(cached.value, incoming.value) || fieldsChanged;
        if (cached.color != incoming.color || cached.selected != incoming.selected) {
            cached.color = incoming.color;
            cached.selected = incoming.selected;
            fieldsChanged = true;
        }
    }
    if (fieldsChanged) invalidateFields();

    const bool staticTimelineChanged = !rendered_ || geometry_ != props.geometry ||
        geometry_revision_ != props.geometryRevision ||
        focused_layer_ != props.focusedLayer ||
        !(randomize_changed_steps_ == props.randomizeChangedSteps) ||
        randomize_property_ != props.randomizeProperty ||
        randomize_preview_ != props.randomizePreview;
    geometry_ = props.geometry;
    geometry_revision_ = props.geometryRevision;
    focused_layer_ = props.focusedLayer;
    randomize_changed_steps_ = props.randomizeChangedSteps;
    randomize_property_ = props.randomizeProperty;
    randomize_preview_ = props.randomizePreview;
    if (staticTimelineChanged) {
        playhead_ = props.playhead;
        invalidateTimeline();
        if (playhead_surface_) lv_obj_invalidate(playhead_surface_);
    } else {
        renderPlayhead(props.playhead);
    }

    if (!visible_) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
        visible_ = true;
    }
    rendered_ = true;
}

void SequencerPatternEditorOverlay::renderPlayhead(
    timeline::SequencerPatternTimelinePlayhead next
) {
    OC_PERF_SCOPE(perfMutation, "ui.sequencer.pattern-editor.playhead");
    if (!playhead_surface_ || !geometry_ || !rendered_) {
        playhead_ = next;
        return;
    }
    const auto damage = timeline::sequencerPatternTimelinePlayheadDamage(
        *geometry_, playhead_, next, 5U
    );
    playhead_ = next;
    OC_PERF_UNITS(
        perfMutation,
        damage.count,
        static_cast<uint32_t>(
            damage.count == 0U
                ? 0U
                : damage.bands[0].width * damage.bands[0].height
        )
    );
    if (damage.count == 0U) return;

    lv_area_t surface{};
    lv_obj_get_coords(playhead_surface_, &surface);
    for (uint8_t index = 0U; index < damage.count; ++index) {
        const auto& band = damage.bands[index];
        if (band.width == 0U || band.height == 0U) continue;
        const lv_area_t area{
            .x1 = static_cast<lv_coord_t>(surface.x1 + band.x),
            .y1 = static_cast<lv_coord_t>(surface.y1 + band.y),
            .x2 = static_cast<lv_coord_t>(surface.x1 + band.x + band.width - 1U),
            .y2 = static_cast<lv_coord_t>(surface.y1 + band.y + band.height - 1U),
        };
        oc::ui::lvgl::invalidateStaticSurfaceArea(playhead_surface_, area);
    }
}

FLASHMEM void SequencerPatternEditorOverlay::drawTimeline(lv_layer_t* layer) {
    if (!layer || !timeline_ || !geometry_) return;
    const auto& geometry = *geometry_;
    const auto& key = geometry.key;
    if (key.width == 0U || key.height == 0U) return;

    lv_area_t area{};
    lv_obj_get_coords(timeline_, &area);
    const lv_area_t clip = layer->_clip_area;
    const uint16_t clipX1 = static_cast<uint16_t>(std::clamp<lv_coord_t>(
        clip.x1 - area.x1,
        0,
        static_cast<lv_coord_t>(key.width - 1U)
    ));
    const uint16_t clipX2 = static_cast<uint16_t>(std::clamp<lv_coord_t>(
        clip.x2 - area.x1,
        0,
        static_cast<lv_coord_t>(key.width - 1U)
    ));
    const auto gx = [&area](uint16_t x) {
        return static_cast<lv_coord_t>(area.x1 + x);
    };
    const auto gy = [&area](uint8_t y) {
        return static_cast<lv_coord_t>(area.y1 + y);
    };

    drawRect(layer, area, theme::color::BACKGROUND, LV_OPA_COVER);
    if (geometry.windowEndX > geometry.windowStartX) {
        const lv_area_t window{
            .x1 = gx(geometry.windowStartX),
            .y1 = area.y1,
            .x2 = static_cast<lv_coord_t>(gx(geometry.windowEndX) - 1),
            .y2 = area.y2,
        };
        drawRect(
            layer, window, theme::color::STEP_LENGTH, LV_OPA_10,
            1, LV_OPA_40, 2
        );
    }

    for (uint8_t quarter = 1U; quarter < 4U; ++quarter) {
        const auto y = static_cast<lv_coord_t>(
            area.y1 + (static_cast<uint16_t>(key.height) * quarter) / 4U
        );
        drawLine(
            layer, area.x1, y, area.x2, y,
            theme::color::INACTIVE, LV_OPA_20
        );
    }
    const uint8_t stepStride = key.contentLength <= 32U ? 1U : 4U;
    for (uint16_t step = stepStride; step < key.contentLength; step += stepStride) {
        const auto x = gx(timeline::sequencerPatternTimelineBoundaryX(
            geometry, static_cast<uint8_t>(step)
        ));
        if (x < clip.x1 || x > clip.x2) continue;
        drawLine(
            layer, x, area.y1, x, area.y2,
            theme::color::INACTIVE,
            (step % 4U) == 0U ? LV_OPA_30 : OPACITY_15
        );
    }

    using Layer = core::state::sequencer::SequencerPatternEditorLayer;
    const bool noteFocus = focused_layer_ == Layer::NOTES;
    const bool regionFocus = focused_layer_ == Layer::REGION;
    const lv_opa_t noteOpacity = noteFocus ? LV_OPA_COVER : OPACITY_35;
    for (uint16_t step = 0U; step < key.contentLength; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        if (!geometry.activeSteps.test(stepIndex)) continue;
        const auto& retained = geometry.steps[step];
        const uint16_t onset = timeline::sequencerPatternTimelineStepOnsetX(
            geometry, stepIndex
        );
        const uint16_t gateEnd = std::max<uint16_t>(
            static_cast<uint16_t>(onset + 1U),
            timeline::sequencerPatternTimelineStepGateEndX(geometry, stepIndex)
        );
        const lv_coord_t x1 = gx(std::min<uint16_t>(onset, key.width - 1U));
        const lv_coord_t x2 = gx(std::min<uint16_t>(gateEnd, key.width - 1U));
        if (x2 < clip.x1 || x1 > clip.x2) continue;
        const lv_coord_t noteY = gy(retained.noteY);
        drawLine(
            layer, x1, noteY, x2, noteY,
            theme::color::STEP_PITCH, noteOpacity, noteFocus ? 2 : 1
        );
        const lv_coord_t velocityY = gy(retained.velocityY);
        const lv_area_t velocityDot{
            .x1 = static_cast<lv_coord_t>(x1 - 1),
            .y1 = static_cast<lv_coord_t>(velocityY - 1),
            .x2 = x1,
            .y2 = velocityY,
        };
        drawRect(
            layer, velocityDot, theme::color::STEP_VELOCITY,
            noteFocus ? LV_OPA_COVER : LV_OPA_30
        );
        const lv_coord_t probabilityX = std::min<lv_coord_t>(
            static_cast<lv_coord_t>(x1 + 3), area.x2
        );
        const lv_coord_t probabilityY = gy(retained.probabilityY);
        const lv_area_t probabilityDot{
            .x1 = static_cast<lv_coord_t>(probabilityX - 1),
            .y1 = static_cast<lv_coord_t>(probabilityY - 1),
            .x2 = probabilityX,
            .y2 = probabilityY,
        };
        drawRect(
            layer, probabilityDot, theme::color::STEP_CHANCE,
            noteFocus ? LV_OPA_70 : LV_OPA_30
        );
    }

    if (randomize_preview_) {
        using Property =
            core::state::sequencer::SequencerPatternRandomizeProperty;
        for (uint16_t step = 0U; step < key.contentLength; ++step) {
            const auto stepIndex = static_cast<uint8_t>(step);
            if (!randomize_changed_steps_.test(stepIndex)) continue;
            const auto& retained = geometry.steps[step];
            const auto x = gx(std::min<uint16_t>(
                timeline::sequencerPatternTimelineStepOnsetX(geometry, stepIndex),
                static_cast<uint16_t>(key.width - 1U)
            ));
            if (x < clip.x1 - 2 || x > clip.x2 + 2) continue;
            lv_coord_t y = gy(retained.noteY);
            switch (randomize_property_) {
                case Property::VELOCITY:
                    y = gy(retained.velocityY);
                    break;
                case Property::PROBABILITY:
                    y = gy(retained.probabilityY);
                    break;
                case Property::GATE:
                case Property::NUDGE:
                case Property::NOTE:
                default:
                    break;
            }
            const bool active = geometry.activeSteps.test(step);
            const lv_area_t marker{
                .x1 = static_cast<lv_coord_t>(x - 2),
                .y1 = static_cast<lv_coord_t>(y - 2),
                .x2 = static_cast<lv_coord_t>(x + 2),
                .y2 = static_cast<lv_coord_t>(y + 2),
            };
            drawRect(
                layer,
                marker,
                theme::color::STEP_CHANCE,
                active ? LV_OPA_COVER : LV_OPA_TRANSP,
                active ? 0 : 1,
                active ? LV_OPA_TRANSP : LV_OPA_COVER,
                2
            );
            if (!active) {
                drawLine(
                    layer, x, static_cast<lv_coord_t>(y - 4),
                    x, static_cast<lv_coord_t>(y + 4),
                    theme::color::STEP_CHANCE, LV_OPA_70, 1
                );
            }
        }
    }

    for (uint8_t laneSlot = 0U; laneSlot < geometry.ccLaneCount; ++laneSlot) {
        const uint8_t sourceLane = geometry.sourceLaneIndex[laneSlot];
        if (sourceLane >= CC_COLORS.size()) continue;
        const bool focused = ccLayerFocused(focused_layer_, sourceLane);
        const lv_opa_t opacity = focused
            ? LV_OPA_COVER
            : (noteFocus ? OPACITY_25 : OPACITY_15);
        uint16_t runCount = 0U;
        const uint16_t curveStart = clipX1 > 0U
            ? static_cast<uint16_t>(clipX1 - 1U)
            : 0U;
        const uint16_t curveEnd = std::min<uint16_t>(
            static_cast<uint16_t>(clipX2 + 1U),
            static_cast<uint16_t>(key.width - 1U)
        );
        for (uint16_t x = curveStart; x <= curveEnd; ++x) {
            if (!timeline::sequencerPatternTimelineCcSampleValid(
                    geometry, laneSlot, x
                )) {
                flushCurveRun(
                    layer,
                    curve_points_,
                    runCount,
                    CC_COLORS[sourceLane],
                    opacity,
                    focused
                );
                continue;
            }
            curve_points_[runCount++] = {
                static_cast<lv_value_precise_t>(gx(x)),
                static_cast<lv_value_precise_t>(gy(geometry.ccY[laneSlot][x])),
            };
        }
        flushCurveRun(
            layer,
            curve_points_,
            runCount,
            CC_COLORS[sourceLane],
            opacity,
            focused
        );
    }

    const lv_opa_t regionOpacity = regionFocus ? LV_OPA_COVER : LV_OPA_60;
    const auto playStartX = gx(geometry.playStartX);
    const auto loopStartX = gx(geometry.loopStartX);
    const auto loopEndX = gx(std::min<uint16_t>(
        geometry.loopEndX,
        static_cast<uint16_t>(key.width - 1U)
    ));
    if (playStartX >= clip.x1 && playStartX <= clip.x2) {
        drawLine(
            layer, playStartX, area.y1, playStartX, area.y2,
            theme::color::STEP_NUDGE, regionOpacity, regionFocus ? 2 : 1
        );
    }
    if (loopStartX >= clip.x1 && loopStartX <= clip.x2) {
        drawLine(
            layer, loopStartX, area.y1, loopStartX, area.y2,
            theme::color::PLAY_ACTIVE, regionOpacity, regionFocus ? 2 : 1
        );
    }
    if (loopEndX >= clip.x1 && loopEndX <= clip.x2) {
        drawLine(
            layer, loopEndX, area.y1, loopEndX, area.y2,
            theme::color::PLAY_ACTIVE, regionOpacity, regionFocus ? 2 : 1
        );
    }
}

void SequencerPatternEditorOverlay::drawPlayhead(lv_layer_t* layer) {
    if (!layer || !playhead_surface_ || !geometry_ || !playhead_.visible ||
        playhead_.column >= geometry_->key.width) {
        return;
    }
    lv_area_t area{};
    lv_obj_get_coords(playhead_surface_, &area);
    const auto x = static_cast<lv_coord_t>(area.x1 + playhead_.column);
    drawLine(
        layer, x, area.y1, x, area.y2,
        theme::color::PLAY_ACTIVE, LV_OPA_COVER, 2
    );
}

FLASHMEM void SequencerPatternEditorOverlay::drawFields(lv_layer_t* layer) {
    if (!layer || !fields_) return;
    lv_area_t area{};
    lv_obj_get_coords(fields_, &area);
    const lv_coord_t cellWidth = static_cast<lv_coord_t>(
        FIELD_WIDTH / std::max<uint8_t>(field_count_, 1U)
    );
    for (std::size_t index = 0; index < field_count_; ++index) {
        const auto& field = field_cache_[index];
        const lv_coord_t x = static_cast<lv_coord_t>(
            area.x1 + static_cast<lv_coord_t>(index) * cellWidth
        );
        const lv_area_t cell{
            .x1 = x,
            .y1 = area.y1,
            .x2 = static_cast<lv_coord_t>(x + cellWidth - 2),
            .y2 = area.y2,
        };
        if (field.selected) {
            drawRect(layer, cell, field.color, LV_OPA_10, 1, LV_OPA_COVER, 2);
        }
        const lv_area_t iconArea{
            .x1 = static_cast<lv_coord_t>(x + 2),
            .y1 = static_cast<lv_coord_t>(area.y1 + 2),
            .x2 = static_cast<lv_coord_t>(x + 15),
            .y2 = static_cast<lv_coord_t>(area.y2 - 1),
        };
        drawLabel(
            layer, iconArea, field.icon.data(), standalone_fonts.icons_12,
            field.color, field.selected ? LV_OPA_COVER : LV_OPA_60
        );
        const lv_area_t valueArea{
            .x1 = static_cast<lv_coord_t>(x + 15),
            .y1 = static_cast<lv_coord_t>(area.y1 + 2),
            .x2 = static_cast<lv_coord_t>(x + cellWidth - 3),
            .y2 = static_cast<lv_coord_t>(area.y2 - 1),
        };
        drawLabel(
            layer, valueArea, field.value.data(), fonts.inter_12_medium,
            theme::color::TEXT_PRIMARY,
            field.selected ? LV_OPA_COVER : OPACITY_55
        );
    }
}

FLASHMEM void SequencerPatternEditorOverlay::onTimelineDraw(lv_event_t* event) {
    auto* self = static_cast<SequencerPatternEditorOverlay*>(
        lv_event_get_user_data(event)
    );
    if (self) self->drawTimeline(lv_event_get_layer(event));
}

void SequencerPatternEditorOverlay::onPlayheadDraw(lv_event_t* event) {
    auto* self = static_cast<SequencerPatternEditorOverlay*>(
        lv_event_get_user_data(event)
    );
    if (self) self->drawPlayhead(lv_event_get_layer(event));
}

FLASHMEM void SequencerPatternEditorOverlay::onFieldsDraw(lv_event_t* event) {
    auto* self = static_cast<SequencerPatternEditorOverlay*>(
        lv_event_get_user_data(event)
    );
    if (self) self->drawFields(lv_event_get_layer(event));
}

}  // namespace core::ui

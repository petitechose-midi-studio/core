#include "ui/macro/MacroEditorOverlay.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/time/Time.hpp>

#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "ui/font/StandaloneFonts.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/interaction/InteractiveSurfaceVisual.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {
namespace {

namespace theme = standalone::theme;
namespace state_mod = core::state::modulation;

constexpr lv_coord_t TAB_X = 8;
constexpr lv_coord_t TAB_Y = 30;
constexpr lv_coord_t TAB_WIDTH = 99;
constexpr lv_coord_t TAB_HEIGHT = 34;
constexpr lv_coord_t TAB_GAP = 4;
constexpr lv_coord_t GRAPH_X = 8;
constexpr lv_coord_t GRAPH_Y = 72;
constexpr lv_coord_t GRAPH_WIDTH = 304;
constexpr lv_coord_t GRAPH_HEIGHT = 92;

template <size_t N>
bool copyText(std::array<char, N>& destination, const char* source) {
    static_assert(N > 1U);
    const char* text = source ? source : "";
    // Equality is based on the retained, displayable prefix. A source longer
    // than the fixed label buffer must not retrigger the same LVGL update on
    // every render.
    if (std::strncmp(destination.data(), text, N - 1U) == 0) return false;
    std::strncpy(destination.data(), text, N - 1U);
    destination[N - 1U] = '\0';
    return true;
}

FLASHMEM lv_obj_t* createLabel(lv_obj_t* parent,
                               const lv_font_t* font,
                               uint32_t color,
                               lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    auto* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font ? font : LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

}  // namespace

FLASHMEM MacroEditorOverlay::MacroEditorOverlay(lv_obj_t* parent) {
    createUi(parent);
}

FLASHMEM MacroEditorOverlay::~MacroEditorOverlay() {
    curve_preview_.reset();
    if (root_) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

FLASHMEM void MacroEditorOverlay::createUi(lv_obj_t* parent) {
    if (!parent) return;
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, lv_color_hex(theme::color::BACKGROUND), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    title_ = createLabel(root_, fonts.context_title(), theme::color::TEXT_PRIMARY);
    lv_obj_set_pos(title_, 10, 7);
    lv_obj_set_size(title_, 150, 18);
    meta_ = createLabel(
        root_, fonts.meta_label(), theme::color::TEXT_SECONDARY,
        LV_TEXT_ALIGN_RIGHT
    );
    lv_obj_set_pos(meta_, 160, 8);
    lv_obj_set_size(meta_, 150, 16);

    createTab(0, standalone::icons::MIDI_CC, "Destination", theme::color::MACRO_CC_COLOR);
    createTab(1, standalone::icons::MACRO_AUTOMATION, "Automation", theme::color::MACRO_AUTOMATION);
    createTab(2, standalone::icons::MACRO_MODULATION, "Modulation", theme::color::MACRO_MODULATION);

    curve_preview_ = core::app::makeExtmemUnique<ms::ui::CurvePreviewWidget>(
        root_
    );
    if (curve_preview_) {
        auto* graph = curve_preview_->getElement();
        lv_obj_set_pos(graph, GRAPH_X, GRAPH_Y);
        lv_obj_set_size(graph, GRAPH_WIDTH, GRAPH_HEIGHT);
        lv_obj_set_style_bg_color(
            graph,
            lv_color_hex(theme::color::SURFACE_IDLE),
            0
        );
        lv_obj_set_style_bg_opa(graph, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(
            graph, theme::layout::INTERACTIVE_SURFACE_BORDER_WIDTH, 0
        );
        lv_obj_set_style_border_color(
            graph,
            lv_color_hex(theme::color::BORDER_SUBTLE),
            0
        );
        lv_obj_set_style_border_opa(graph, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(
            graph, theme::layout::INTERACTIVE_SURFACE_RADIUS, 0
        );
    }
    clipping_ = createLabel(
        root_, fonts.meta_label(), theme::color::MACRO_CONFLICT,
        LV_TEXT_ALIGN_RIGHT
    );
    lv_label_set_text(clipping_, "Clip");
    lv_obj_set_pos(clipping_, 252, 168);
    lv_obj_set_size(clipping_, 58, 15);
    lv_obj_add_flag(clipping_, LV_OBJ_FLAG_HIDDEN);

    hint_ = createLabel(
        root_, fonts.meta_label(), theme::color::TEXT_SECONDARY,
        LV_TEXT_ALIGN_CENTER
    );
    lv_obj_set_pos(hint_, 8, 185);
    lv_obj_set_size(hint_, 304, 15);

    interaction_overlay_ = lv_obj_create(root_);
    lv_obj_remove_style_all(interaction_overlay_);
    lv_obj_set_pos(interaction_overlay_, 44, 88);
    lv_obj_set_size(interaction_overlay_, 232, 62);
    lv_obj_set_style_border_width(
        interaction_overlay_,
        theme::layout::INTERACTIVE_SURFACE_BORDER_WIDTH,
        0
    );
    lv_obj_set_style_radius(
        interaction_overlay_, theme::layout::INTERACTIVE_SURFACE_RADIUS, 0
    );
    core::ui::interaction::applyInteractiveSurfaceChrome(
        interaction_overlay_,
        core::ui::interaction::InteractiveSurfaceState::FOCUSED
    );
    lv_obj_clear_flag(interaction_overlay_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(interaction_overlay_, LV_OBJ_FLAG_HIDDEN);

    interaction_icon_ = createLabel(
        interaction_overlay_, standalone_fonts.icons_16,
        theme::color::TEXT_PRIMARY, LV_TEXT_ALIGN_CENTER
    );
    lv_obj_set_pos(interaction_icon_, 10, 21);
    lv_obj_set_size(interaction_icon_, 26, 20);
    interaction_label_ = createLabel(
        interaction_overlay_, fonts.meta_label(),
        theme::color::TEXT_SECONDARY
    );
    lv_obj_set_pos(interaction_label_, 45, 9);
    lv_obj_set_size(interaction_label_, 176, 17);
    interaction_value_ = createLabel(
        interaction_overlay_, fonts.primary_value(),
        theme::color::TEXT_PRIMARY
    );
    lv_obj_set_pos(interaction_value_, 45, 29);
    lv_obj_set_size(interaction_value_, 176, 20);
}

FLASHMEM void MacroEditorOverlay::createTab(
    size_t index,
    const char* icon,
    const char* label,
    uint32_t color
) {
    if (index >= tabs_.size() || !root_) return;
    auto& tab = tabs_[index];
    tab.root = lv_obj_create(root_);
    lv_obj_remove_style_all(tab.root);
    lv_obj_set_pos(
        tab.root,
        TAB_X + static_cast<lv_coord_t>(index) * (TAB_WIDTH + TAB_GAP),
        TAB_Y
    );
    lv_obj_set_size(tab.root, TAB_WIDTH, TAB_HEIGHT);
    lv_obj_set_style_radius(
        tab.root, theme::layout::INTERACTIVE_SURFACE_RADIUS, 0
    );
    lv_obj_set_style_border_width(
        tab.root, theme::layout::INTERACTIVE_SURFACE_BORDER_WIDTH, 0
    );
    core::ui::interaction::applyInteractiveSurfaceChrome(
        tab.root,
        core::ui::interaction::InteractiveSurfaceState::IDLE
    );
    lv_obj_clear_flag(tab.root, LV_OBJ_FLAG_SCROLLABLE);
    tab.icon = createLabel(tab.root, standalone_fonts.icons_12, color);
    lv_label_set_text(tab.icon, icon);
    lv_obj_set_pos(tab.icon, 5, 5);
    lv_obj_set_size(tab.icon, 15, 14);
    tab.label = createLabel(tab.root, fonts.meta_label(), theme::color::TEXT_PRIMARY);
    lv_label_set_text(tab.label, label);
    lv_obj_set_pos(tab.label, 22, 3);
    lv_obj_set_size(tab.label, 72, 14);
    tab.value = createLabel(tab.root, fonts.meta_label(), theme::color::TEXT_SECONDARY);
    lv_obj_set_pos(tab.value, 22, 17);
    lv_obj_set_size(tab.value, 70, 14);
    tab.state = lv_obj_create(tab.root);
    lv_obj_remove_style_all(tab.state);
    lv_obj_set_pos(tab.state, 5, 24);
    lv_obj_set_size(tab.state, 10, 3);
    lv_obj_set_style_radius(tab.state, 2, 0);
    lv_obj_set_style_bg_color(tab.state, lv_color_hex(color), 0);
    tab.color = color;
}

FLASHMEM void MacroEditorOverlay::renderTab(
    size_t index,
    const char* value,
    bool selected,
    bool stored,
    bool playback,
    uint32_t color
) {
    if (index >= tabs_.size()) return;
    auto& tab = tabs_[index];
    if (copyText(tab.valueText, value)) {
        lv_label_set_text_static(tab.value, tab.valueText.data());
    }
    if (!tab.rendered || tab.color != color) {
        lv_obj_set_style_text_color(tab.icon, lv_color_hex(color), 0);
        lv_obj_set_style_bg_color(tab.state, lv_color_hex(color), 0);
        tab.color = color;
    }
    if (!tab.rendered || tab.selected != selected) {
        const auto surfaceState = selected
            ? core::ui::interaction::InteractiveSurfaceState::FOCUSED
            : core::ui::interaction::InteractiveSurfaceState::IDLE;
        const auto visual = core::ui::interaction::interactiveSurfaceVisual(
            surfaceState
        );
        core::ui::interaction::applyInteractiveSurfaceChrome(
            tab.root, visual
        );
        lv_obj_set_style_text_color(
            tab.label, lv_color_hex(visual.textColor), 0
        );
        lv_obj_set_style_text_opa(
            tab.label, visual.textOpacity, 0
        );
        lv_obj_set_style_text_color(
            tab.value, lv_color_hex(theme::color::TEXT_SECONDARY), 0
        );
        lv_obj_set_style_text_opa(
            tab.value,
            selected ? LV_OPA_80 : LV_OPA_60,
            0
        );
        tab.selected = selected;
    }
    if (!tab.rendered || tab.stored != stored ||
        tab.playback != playback) {
        lv_obj_set_style_bg_opa(
            tab.state,
            playback
                ? LV_OPA_COVER
                : (stored ? LV_OPA_30 : LV_OPA_TRANSP),
            0
        );
        tab.stored = stored;
        tab.playback = playback;
    }
    tab.rendered = true;
}

FLASHMEM bool MacroEditorOverlay::sampleCurve(
    void* rawContext,
    uint16_t positionQ16,
    ms::ui::CurvePreviewSample& out
) {
    auto* context = static_cast<CurveSampleContext*>(rawContext);
    if (context == nullptr || context->preview == nullptr) return false;
    MacroEditorPreviewSample sample{};
    const bool sampled = sampleMacroEditorPreview(
            *context->preview,
            context->focus,
            positionQ16,
            context->previousPositionQ16,
            context->hasPrevious,
            sample
        );
    if (!sampled) {
        return false;
    }
    uint16_t curve = sample.outQ16;
    if (context->focus == MacroEditorPreviewFocus::AUTOMATION) {
        curve = sample.automationQ16;
    } else if (
        context->focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR ||
        context->focus == MacroEditorPreviewFocus::ALL_MODULATION
    ) {
        curve = static_cast<uint16_t>(std::clamp<int32_t>(
            32768 + static_cast<int32_t>(sample.modulationQ15),
            0,
            65535
        ));
    }
    out = {
        .curve = curve,
        .base = sample.baseQ16,
        .impact = sample.outQ16,
        .discontinuityBefore = sample.discontinuityBefore,
    };
    context->previousPositionQ16 = positionQ16;
    context->hasPrevious = true;
    context->clippedLow = context->clippedLow || sample.clippedLow;
    context->clippedHigh = context->clippedHigh || sample.clippedHigh;
    return true;
}

FLASHMEM bool MacroEditorOverlay::sampleMarker(
    void* rawContext,
    ms::ui::CurvePreviewMarker& out
) {
    auto* context = static_cast<CurveSampleContext*>(rawContext);
    out = {};
    if (context == nullptr || context->preview == nullptr ||
        context->preview->control == nullptr) {
        return true;
    }
    const auto& model = *context->preview;
    const auto& control = *model.control;
    const auto time = state_mod::extrapolateProjectControlTime(
        control.timeTelemetry,
        oc::time::millis()
    );
    uint16_t positionQ16 = 0U;
    bool positionKnown = false;
    if (control.runtime.initialized) {
        positionQ16 = state_mod::projectControlTimelinePositionQ16(
            control.runtime,
            time,
            model.timelineDurationTicks
        );
        positionKnown = true;
    }
    if (!positionKnown && model.activeTake != nullptr) {
        const auto& take = *model.activeTake;
        if (take.activeFor(model.activeTakeMacro)) {
            const uint32_t duration = std::max<uint16_t>(
                model.timelineDurationTicks,
                1U
            );
            const uint32_t phaseTick = static_cast<uint32_t>(
                (static_cast<uint64_t>(take.startProjectPhaseTick % duration) +
                 static_cast<uint64_t>(take.latestElapsedTick % duration)) %
                duration
            );
            positionQ16 = static_cast<uint16_t>(
                (static_cast<uint64_t>(phaseTick) * 65535U + duration / 2U) /
                duration
            );
            positionKnown = true;
        }
    }
    if (!positionKnown) return true;
    MacroEditorPreviewSample sample{};
    if (!sampleMacroEditorPreview(
            model,
            context->focus,
            positionQ16,
            positionQ16,
            false,
            sample
        )) {
        return false;
    }
    uint16_t valueQ16 = sample.outQ16;
    if (context->focus == MacroEditorPreviewFocus::AUTOMATION) {
        valueQ16 = sample.automationQ16;
    } else if (context->focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR ||
               context->focus == MacroEditorPreviewFocus::ALL_MODULATION) {
        valueQ16 = static_cast<uint16_t>(std::clamp<int32_t>(
            32768 + static_cast<int32_t>(sample.modulationQ15),
            0,
            65535
        ));
    }
    const auto quantizeLive = [](float value) {
        return static_cast<uint16_t>(
            std::clamp(value, 0.0f, 1.0f) * 65535.0f + 0.5f
        );
    };
    if (context->live != nullptr && context->live->valid) {
        if (context->focus == MacroEditorPreviewFocus::DESTINATION) {
            valueQ16 = quantizeLive(context->live->out);
        } else if (
            context->focus == MacroEditorPreviewFocus::ALL_MODULATION ||
            context->focus == MacroEditorPreviewFocus::FOCUSED_MODULATOR
        ) {
            valueQ16 = quantizeLive(
                (std::clamp(context->live->modulation, -1.0f, 1.0f) + 1.0f) *
                0.5f
            );
        } else if (
            model.activeTake != nullptr || model.automationPlayback
        ) {
            valueQ16 = quantizeLive(context->live->base);
        }
    }
    out = {
        .visible = true,
        .positionQ16 = positionQ16,
        .valueQ16 = valueQ16,
    };
    return true;
}

FLASHMEM void MacroEditorOverlay::renderGraph(
    const MacroEditorPreviewModel& model,
    int selected,
    uint32_t previewRevision
) {
    if (!curve_preview_) return;
    selected = std::clamp(selected, 0, 2);
    const uint32_t baseColor = model.manualOverride
        ? theme::color::MACRO_AUTOMATION_MANUAL
        : (model.automationDrivingBase
            ? theme::color::MACRO_AUTOMATION
            : theme::color::TEXT_PRIMARY);
    uint32_t curveColor = theme::color::MACRO_CC_COLOR;
    lv_opa_t curveOpacity = LV_OPA_COVER;
    if (selected == 1) {
        curveColor = model.automationStored
            ? theme::color::MACRO_AUTOMATION
            : baseColor;
        curveOpacity = model.automationStored ? LV_OPA_COVER : LV_OPA_30;
    } else if (selected == 2) {
        curveColor = theme::color::MACRO_MODULATION;
        curveOpacity = model.modulationStored ? LV_OPA_COVER : LV_OPA_TRANSP;
    }
    const lv_opa_t baseOpacity = static_cast<lv_opa_t>(
        selected == 0 ? LV_OPA_40 : LV_OPA_30
    );
    const lv_opa_t impactOpacity = static_cast<lv_opa_t>(
        selected == 0 ? LV_OPA_TRANSP : LV_OPA_30
    );
    const lv_opa_t bandOpacity = static_cast<lv_opa_t>(
        model.modulationStored ? LV_OPA_20 : LV_OPA_TRANSP
    );
    curve_sample_context_ = {
        .preview = &model,
        .live = &latest_live_,
        .focus = selected == 0
            ? MacroEditorPreviewFocus::DESTINATION
            : (selected == 1
                ? MacroEditorPreviewFocus::AUTOMATION
                : MacroEditorPreviewFocus::ALL_MODULATION),
        .previousPositionQ16 = 0U,
        .hasPrevious = false,
        .clippedLow = false,
        .clippedHigh = false,
    };
    curve_props_ = {
        .visible = true,
        .sampleProvider = &MacroEditorOverlay::sampleCurve,
        .sampleContext = &curve_sample_context_,
        .geometryRevision = previewRevision * 3U + static_cast<uint32_t>(selected),
        .geometryUpdate = ms::ui::CurvePreviewGeometryUpdate::REBUILD_DAMAGE,
        .geometryAdvance = 0U,
        .markerProvider = &MacroEditorOverlay::sampleMarker,
        .markerContext = &curve_sample_context_,
        .showImpactBand = model.modulationStored,
        .showCenterGuide = selected == 2,
        .showRestGuide = false,
        .restValueQ16 = 0U,
        .paddingX = 5,
        .paddingY = 5,
        .curveColor = curveColor,
        .baseColor = baseColor,
        .impactColor = theme::color::MACRO_CC_COLOR,
        .guideColor = theme::color::TEXT_SECONDARY,
        .markerColor = theme::color::PLAY_ACTIVE,
        .curveOpacity = curveOpacity,
        .baseOpacity = baseOpacity,
        .impactOpacity = impactOpacity,
        .bandOpacity = bandOpacity,
        .guideOpacity = LV_OPA_30,
        .curveWidth = 2,
        .baseWidth = 1,
        .impactWidth = 1,
        .markerRadius = 2,
        .marker = {},
    };
    curve_preview_->render(curve_props_);
    const bool clipped = curve_sample_context_.clippedLow ||
        curve_sample_context_.clippedHigh;
    setClippingVisible(clipped);
}

void MacroEditorOverlay::setClippingVisible(bool visible) {
    if (visible == clipping_visible_) return;
    if (visible) {
        lv_obj_clear_flag(clipping_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(clipping_, LV_OBJ_FLAG_HIDDEN);
    }
    clipping_visible_ = visible;
}

void MacroEditorOverlay::renderLive(const MacroEditorLiveValue& live) {
    // The retained marker samples ProjectControl runtime directly. A live
    // value update therefore needs no layout, tab refresh or curve rebuild.
    latest_live_ = live;
}

FLASHMEM void MacroEditorOverlay::render(
    const MacroEditorOverlayProps& props
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
        renderedRevision_ = UINT32_MAX;
        renderedPreviewRevision_ = UINT32_MAX;
    }
    if (renderedRevision_ == props.dataRevision &&
        renderedPreviewRevision_ == props.previewRevision) {
        return;
    }
    if (props.preview == nullptr) return;
    latest_live_ = props.live;
    renderedRevision_ = props.dataRevision;
    if (copyText(titleText_, props.title)) {
        lv_label_set_text_static(title_, titleText_.data());
    }
    if (copyText(metaText_, props.meta)) {
        lv_label_set_text_static(meta_, metaText_.data());
    }
    const int selected = std::clamp(props.selectedDomain, 0, 2);
    renderTab(0, props.destination, selected == 0, true, true, theme::color::MACRO_CC_COLOR);
    renderTab(
        1, props.automation, selected == 1,
        props.preview->automationStored, props.preview->automationPlayback,
        theme::color::MACRO_AUTOMATION
    );
    renderTab(
        2, props.modulation, selected == 2,
        props.preview->modulationStored, props.preview->modulationPlayback,
        theme::color::MACRO_MODULATION
    );
    renderGraph(
        *props.preview,
        selected,
        props.previewRevision
    );
    renderedPreviewRevision_ = props.previewRevision;
    static constexpr std::array<const char*, 3> HINTS = {
        "Base + modulation = output",
        "Absolute gesture · Press to edit",
        "Relative loop · Press to edit",
    };
    if (renderedSelectedDomain_ != selected) {
        lv_label_set_text_static(
            hint_,
            HINTS[static_cast<size_t>(selected)]
        );
        renderedSelectedDomain_ = selected;
    }

    if (props.interactionOverlayVisible) {
        const char* interactionIcon = props.interactionIcon
            ? props.interactionIcon
            : standalone::icons::KNOB;
        if (copyText(interactionIconText_, interactionIcon)) {
            lv_label_set_text_static(
                interaction_icon_,
                interactionIconText_.data()
            );
        }
        if (copyText(interactionLabelText_, props.interactionLabel)) {
            lv_label_set_text_static(
                interaction_label_, interactionLabelText_.data()
            );
        }
        if (copyText(interactionValueText_, props.interactionValue)) {
            lv_label_set_text_static(
                interaction_value_, interactionValueText_.data()
            );
        }
        const uint32_t color = props.interactionColor != 0U
            ? props.interactionColor
            : theme::color::TEXT_PRIMARY;
        if (interactionColor_ != color) {
            lv_obj_set_style_text_color(
                interaction_icon_, lv_color_hex(color), 0
            );
            interactionColor_ = color;
        }
        if (!interaction_overlay_visible_) {
            lv_obj_clear_flag(interaction_overlay_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(interaction_overlay_);
            interaction_overlay_visible_ = true;
        }
    } else if (interaction_overlay_visible_) {
        lv_obj_add_flag(interaction_overlay_, LV_OBJ_FLAG_HIDDEN);
        interaction_overlay_visible_ = false;
    }
}

}  // namespace core::ui

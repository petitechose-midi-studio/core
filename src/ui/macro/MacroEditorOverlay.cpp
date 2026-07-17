#include "ui/macro/MacroEditorOverlay.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>

#include "ui/font/StandaloneFonts.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {
namespace {

namespace theme = standalone::theme;

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
    const char* text = source ? source : "";
    if (std::strncmp(destination.data(), text, N) == 0) return false;
    std::strncpy(destination.data(), text, N - 1U);
    destination[N - 1U] = '\0';
    return true;
}

lv_obj_t* createLabel(lv_obj_t* parent,
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

template <typename Value>
FLASHMEM int32_t interpolatePreviewValue(
    const std::array<Value, MACRO_EDITOR_PREVIEW_SAMPLE_COUNT>& values,
    uint16_t positionQ16
) {
    constexpr uint32_t LAST = MACRO_EDITOR_PREVIEW_SAMPLE_COUNT - 1U;
    const uint32_t scaled = static_cast<uint32_t>(positionQ16) * LAST;
    const uint32_t left = scaled / 65535U;
    const uint32_t remainder = scaled % 65535U;
    const uint32_t right = std::min<uint32_t>(left + 1U, LAST);
    const int32_t from = static_cast<int32_t>(values[left]);
    const int32_t to = static_cast<int32_t>(values[right]);
    return from + static_cast<int32_t>(
        (static_cast<int64_t>(to - from) * remainder + 32767) / 65535
    );
}

FLASHMEM uint16_t byteToQ16(int32_t value) {
    return static_cast<uint16_t>(
        std::clamp<int32_t>(value, 0, 255) * 257
    );
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

    title_ = createLabel(root_, fonts.inter_14_semibold, theme::color::TEXT_PRIMARY);
    lv_obj_set_pos(title_, 10, 7);
    lv_obj_set_size(title_, 150, 18);
    meta_ = createLabel(
        root_, fonts.inter_12_medium, theme::color::TEXT_SECONDARY,
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
            lv_color_hex(theme::color::TEXT_PRIMARY),
            0
        );
        lv_obj_set_style_bg_opa(graph, LV_OPA_10, 0);
        lv_obj_set_style_border_width(graph, 1, 0);
        lv_obj_set_style_border_color(
            graph,
            lv_color_hex(theme::color::TEXT_SECONDARY),
            0
        );
        lv_obj_set_style_border_opa(graph, LV_OPA_20, 0);
        lv_obj_set_style_radius(graph, 3, 0);
    }
    clipping_ = createLabel(
        root_, fonts.inter_12_medium, theme::color::MACRO_CONFLICT,
        LV_TEXT_ALIGN_RIGHT
    );
    lv_label_set_text(clipping_, "CLIP");
    lv_obj_set_pos(clipping_, 252, 168);
    lv_obj_set_size(clipping_, 58, 15);
    lv_obj_add_flag(clipping_, LV_OBJ_FLAG_HIDDEN);

    hint_ = createLabel(
        root_, fonts.inter_12_medium, theme::color::TEXT_SECONDARY,
        LV_TEXT_ALIGN_CENTER
    );
    lv_obj_set_pos(hint_, 8, 185);
    lv_obj_set_size(hint_, 304, 15);
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
    lv_obj_set_style_radius(tab.root, 3, 0);
    lv_obj_set_style_border_width(tab.root, 1, 0);
    lv_obj_set_style_border_color(tab.root, lv_color_hex(color), 0);
    lv_obj_clear_flag(tab.root, LV_OBJ_FLAG_SCROLLABLE);
    tab.icon = createLabel(tab.root, standalone_fonts.icons_12, color);
    lv_label_set_text(tab.icon, icon);
    lv_obj_set_pos(tab.icon, 5, 5);
    lv_obj_set_size(tab.icon, 15, 14);
    tab.label = createLabel(tab.root, fonts.inter_12_medium, theme::color::TEXT_PRIMARY);
    lv_label_set_text(tab.label, label);
    lv_obj_set_pos(tab.label, 22, 3);
    lv_obj_set_size(tab.label, 72, 14);
    tab.value = createLabel(tab.root, fonts.inter_12_medium, theme::color::TEXT_SECONDARY);
    lv_obj_set_pos(tab.value, 22, 17);
    lv_obj_set_size(tab.value, 70, 14);
    tab.state = lv_obj_create(tab.root);
    lv_obj_remove_style_all(tab.state);
    lv_obj_set_pos(tab.state, 5, 24);
    lv_obj_set_size(tab.state, 10, 3);
    lv_obj_set_style_radius(tab.state, 2, 0);
    lv_obj_set_style_bg_color(tab.state, lv_color_hex(color), 0);
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
    lv_obj_set_style_bg_color(tab.root, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(tab.root, selected ? LV_OPA_10 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(tab.root, selected ? LV_OPA_80 : LV_OPA_20, 0);
    lv_obj_set_style_text_opa(tab.label, selected ? LV_OPA_COVER : LV_OPA_70, 0);
    lv_obj_set_style_text_opa(tab.value, selected ? LV_OPA_COVER : LV_OPA_60, 0);
    lv_obj_set_style_bg_opa(
        tab.state,
        playback ? LV_OPA_COVER : (stored ? LV_OPA_30 : LV_OPA_TRANSP),
        0
    );
}

FLASHMEM bool MacroEditorOverlay::sampleCurve(
    void* rawContext,
    uint16_t positionQ16,
    ms::ui::CurvePreviewSample& out
) {
    auto* context = static_cast<CurveSampleContext*>(rawContext);
    if (context == nullptr || context->preview == nullptr) return false;
    const auto& model = *context->preview;
    const int selected = std::clamp(context->selectedDomain, 0, 2);
    const int32_t base = interpolatePreviewValue(model.base, positionQ16);
    const int32_t audible = interpolatePreviewValue(model.out, positionQ16);
    const int32_t automation = interpolatePreviewValue(
        model.automation,
        positionQ16
    );
    const int32_t modulation = interpolatePreviewValue(
        model.modulation,
        positionQ16
    );
    uint16_t curve = byteToQ16(audible);
    if (selected == 1) {
        curve = byteToQ16(model.automationStored ? automation : base);
    } else if (selected == 2) {
        curve = static_cast<uint16_t>(std::clamp<int32_t>(
            32768 + modulation * 128,
            0,
            65535
        ));
    }
    out = {
        .curve = curve,
        .base = byteToQ16(base),
        .impact = byteToQ16(audible),
        .discontinuityBefore = context->hasPrevious &&
            std::abs(
                static_cast<int32_t>(curve) -
                static_cast<int32_t>(context->previousValue)
            ) > 16384,
    };
    context->previousValue = curve;
    context->hasPrevious = true;
    return true;
}

FLASHMEM void MacroEditorOverlay::renderGraph(
    const MacroEditorOverlayProps& props
) {
    if (props.preview == nullptr || !curve_preview_) return;
    const auto& model = *props.preview;
    const int selected = std::clamp(props.selectedDomain, 0, 2);
    const uint32_t baseColor = model.manualOverride
        ? theme::color::MACRO_AUTOMATION_MANUAL
        : (model.automationDrivingBase
            ? theme::color::MACRO_AUTOMATION
            : theme::color::TEXT_PRIMARY);
    uint32_t curveColor = theme::color::TEXT_PRIMARY;
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
        selected == 2 ? LV_OPA_60 : LV_OPA_COVER
    );
    const lv_opa_t impactOpacity = static_cast<lv_opa_t>(
        model.modulationStored
            ? (selected == 2
                ? LV_OPA_COVER
                : (model.modulationPlayback ? LV_OPA_70 : LV_OPA_30))
            : LV_OPA_TRANSP
    );
    const lv_opa_t bandOpacity = static_cast<lv_opa_t>(
        model.modulationStored ? LV_OPA_20 : LV_OPA_TRANSP
    );
    curve_sample_context_ = {
        .preview = &model,
        .selectedDomain = selected,
        .previousValue = 0U,
        .hasPrevious = false,
    };
    curve_preview_->render({
        .visible = true,
        .sampleProvider = &MacroEditorOverlay::sampleCurve,
        .sampleContext = &curve_sample_context_,
        .geometryRevision = props.previewRevision * 3U +
            static_cast<uint32_t>(selected),
        .showImpactBand = model.modulationStored,
        .showCenterGuide = selected == 2,
        .showRestGuide = false,
        .restValueQ16 = 0U,
        .paddingX = 5,
        .paddingY = 5,
        .curveColor = curveColor,
        .baseColor = baseColor,
        .impactColor = theme::color::MACRO_MODULATION,
        .guideColor = theme::color::TEXT_SECONDARY,
        .markerColor = theme::color::PLAY_ACTIVE,
        .curveOpacity = curveOpacity,
        .baseOpacity = baseOpacity,
        .impactOpacity = impactOpacity,
        .bandOpacity = bandOpacity,
        .guideOpacity = LV_OPA_30,
        .curveWidth = 2,
        .baseWidth = 1,
        .impactWidth = 2,
        .markerRadius = 2,
        .marker = {},
    });
    if (model.clippedLow || model.clippedHigh) {
        lv_obj_clear_flag(clipping_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(clipping_, LV_OBJ_FLAG_HIDDEN);
    }
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
    renderGraph(props);
    renderedPreviewRevision_ = props.previewRevision;
    static constexpr std::array<const char*, 3> HINTS = {
        "Destination MIDI · Press to edit",
        "Absolute gesture · Press to edit",
        "Relative loop · Press to edit",
    };
    lv_label_set_text_static(hint_, HINTS[static_cast<size_t>(selected)]);
}

}  // namespace core::ui

#include "ui/sequencer/SequencerStepEditOverlay.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "ui/font/StandaloneFonts.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace style = oc::ui::lvgl::style;
namespace theme = standalone::theme;

namespace core::ui {

namespace {

constexpr lv_coord_t PANEL_PAD = theme::layout::PAD_MD;
constexpr lv_coord_t PANEL_GAP = theme::layout::GAP_MD;
constexpr lv_coord_t CHIP_PAD = theme::layout::PAD_SM;
constexpr lv_coord_t CHIP_GAP = theme::layout::GAP_SM;
constexpr lv_coord_t ACTION_TEXT_GAP = 1;
constexpr lv_coord_t TRIGGER_PAD_V = theme::layout::PAD_MD;
constexpr lv_coord_t STEP_BADGE_PAD_V = theme::layout::PAD_SM / 2;
constexpr lv_coord_t CHIP_RADIUS = 3;
constexpr lv_coord_t CHORD_MAP_WIDTH = 196;
constexpr lv_coord_t CHORD_MAP_HEIGHT = 44;
constexpr lv_coord_t CHORD_MAP_RAIL_X = 5;
constexpr lv_coord_t CHORD_MAP_RAIL_Y = 37;
constexpr lv_coord_t CHORD_MAP_RAIL_WIDTH = CHORD_MAP_WIDTH - (CHORD_MAP_RAIL_X * 2);
constexpr lv_coord_t CHORD_MAP_RAIL_HEIGHT = 2;
constexpr lv_coord_t CHORD_MAP_NOTE_TOP = 3;
constexpr lv_coord_t CHORD_MAP_NOTE_HEIGHT = 30;

constexpr uint32_t BG_COLOR = theme::color::BACKGROUND;
constexpr uint32_t TEXT_PRIMARY = theme::color::TEXT_PRIMARY;
constexpr uint32_t TEXT_SECONDARY = theme::color::TEXT_SECONDARY;
constexpr uint32_t STEP_BADGE_COLOR = theme::color::STEP_STATE;

constexpr size_t PITCH_INDEX = 0;
constexpr size_t VELOCITY_INDEX = 1;
constexpr size_t GATE_INDEX = 2;
constexpr size_t NUDGE_INDEX = 3;
constexpr size_t CHANCE_INDEX = 4;
constexpr size_t TRIGGER_STATE_INDEX = 0;
constexpr size_t TRIGGER_CHANCE_INDEX = 1;

FLASHMEM lv_obj_t* createLabel(lv_obj_t* parent,
                               const lv_font_t* font,
                               uint32_t color,
                               lv_opa_t opa = LV_OPA_COVER) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(label, opa, 0);
    if (font) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

template <size_t N>
bool setCachedText(lv_obj_t* label, std::array<char, N>& cache, const char* text) {
    if (!label) return false;
    const char* next = text ? text : "";
    if (std::strncmp(cache.data(), next, N) == 0) {
        return false;
    }
    std::strncpy(cache.data(), next, N - 1);
    cache[N - 1] = '\0';
    lv_label_set_text_static(label, cache.data());
    return true;
}

template <size_t N>
bool setCachedIcon(lv_obj_t* label,
                   std::array<char, N>& cache,
                   standalone::icons::Size& sizeCache,
                   const char* icon,
                   standalone::icons::Size size,
                   bool valid) {
    if (!label) return false;
    const char* next = icon ? icon : "";
    const bool textChanged = std::strncmp(cache.data(), next, N) != 0;
    if (valid && !textChanged && sizeCache == size) return false;

    if (textChanged) {
        std::strncpy(cache.data(), next, N - 1);
        cache[N - 1] = '\0';
    }
    standalone::icons::set(label, cache.data(), size);
    sizeCache = size;
    return true;
}

bool setCachedColor(lv_obj_t* label, uint32_t& cache, uint32_t color) {
    if (!label || cache == color) return false;
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    cache = color;
    return true;
}

bool setCachedOpa(lv_obj_t* label, int16_t& cache, lv_opa_t opa) {
    if (!label || cache == static_cast<int16_t>(opa)) return false;
    lv_obj_set_style_text_opa(label, opa, 0);
    cache = static_cast<int16_t>(opa);
    return true;
}

void setChipState(lv_obj_t* box,
                  bool selected,
                  bool active,
                  lv_opa_t idleBorderOpa = LV_OPA_30) {
    lv_obj_set_style_bg_opa(
        box,
        selected ? LV_OPA_20 : (active ? LV_OPA_10 : LV_OPA_TRANSP),
        0
    );
    lv_obj_set_style_border_opa(
        box,
        selected ? LV_OPA_80 : (active ? idleBorderOpa : LV_OPA_20),
        0
    );
}

lv_coord_t markerCoord(uint8_t normalized, lv_coord_t span, lv_coord_t size) {
    if (span <= size) return 0;
    return static_cast<lv_coord_t>(
        (static_cast<int32_t>(normalized) * static_cast<int32_t>(span - size)) / 100
    );
}

lv_coord_t railCoord(uint8_t normalized) {
    return static_cast<lv_coord_t>(
        (static_cast<int32_t>(normalized) * static_cast<int32_t>(CHORD_MAP_RAIL_WIDTH)) / 100
    );
}

uint32_t selectedRowToPropertyIndex(int selectedIndex) {
    if (!core::state::sequencer::step_edit_rows::isProperty(
            static_cast<uint8_t>(std::max(0, selectedIndex))
        )) {
        return 255U;
    }
    return static_cast<uint32_t>(
        selectedIndex - core::state::sequencer::step_edit_rows::PROPERTY_OFFSET
    );
}

uint32_t selectedRowToActionIndex(int selectedIndex) {
    using namespace core::state::sequencer::step_edit_rows;
    if (selectedIndex == CHORD) return 0;
    if (selectedIndex == MICRO_SEQUENCE) return 1;
    if (selectedIndex == CYCLE_STATES) return 2;
    return 255U;
}

bool explicitVisualSlot(const SequencerStepEditOverlayProps& props) {
    return props.selectedVisualSlot != SequencerStepEditVisualSlot::AUTO;
}

bool selectedVisualSlot(const SequencerStepEditOverlayProps& props,
                        SequencerStepEditVisualSlot slot,
                        bool fallback) {
    return explicitVisualSlot(props) ? props.selectedVisualSlot == slot : fallback;
}

uint32_t focusColorForSelection(const SequencerStepEditOverlayProps& props) {
    if (props.focusColor != 0) {
        return props.focusColor;
    }

    using namespace core::state::sequencer::step_edit_rows;
    if (props.selectedIndex == ACTIVATED) {
        return sequencer::semantic::color(sequencer::semantic::Tone::STATE);
    }
    if (props.selectedIndex == MICRO_SEQUENCE) {
        return sequencer::semantic::color(sequencer::semantic::Tone::MICRO_SEQUENCE);
    }
    if (props.selectedIndex == CYCLE_STATES) {
        return sequencer::semantic::color(sequencer::semantic::Tone::CYCLE_STATE);
    }
    if (props.selectedIndex == CHORD) {
        return sequencer::semantic::color(sequencer::semantic::Tone::CHORD);
    }
    const uint32_t property = selectedRowToPropertyIndex(props.selectedIndex);
    if (property < props.properties.size()) {
        return props.properties[property].color;
    }
    return TEXT_PRIMARY;
}

}  // namespace

FLASHMEM SequencerStepEditOverlay::SequencerStepEditOverlay(lv_obj_t* parent) {
    createUI(parent);
}

FLASHMEM SequencerStepEditOverlay::~SequencerStepEditOverlay() {
    if (overlay_) {
        lv_obj_delete(overlay_);
        overlay_ = nullptr;
    }
}

FLASHMEM void SequencerStepEditOverlay::createUI(lv_obj_t* parent) {
    if (!parent) return;

    overlay_ = lv_obj_create(parent);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_FLOATING);
    style::apply(overlay_).fullSize().noScroll().noBorder().pad(0);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_90, 0);
    lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);

    panel_ = lv_obj_create(overlay_);
    lv_obj_remove_style_all(panel_);
    lv_obj_set_size(panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(panel_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        panel_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_left(panel_, PANEL_PAD, 0);
    lv_obj_set_style_pad_right(panel_, PANEL_PAD, 0);
    lv_obj_set_style_pad_top(panel_, PANEL_PAD, 0);
    lv_obj_set_style_pad_bottom(
        panel_,
        static_cast<lv_coord_t>(
            theme::layout::TRANSPORT_BAR_HEIGHT +
            theme::layout::CONTEXT_ACTION_STRIP_HEIGHT +
            PANEL_PAD
        ),
        0
    );
    lv_obj_set_style_pad_row(panel_, PANEL_GAP, 0);
    lv_obj_set_style_bg_color(panel_, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(panel_, 0, 0);
    lv_obj_set_style_radius(panel_, 0, 0);
    lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

    header_row_ = lv_obj_create(panel_);
    lv_obj_remove_style_all(header_row_);
    lv_obj_set_width(header_row_, LV_PCT(100));
    lv_obj_set_height(header_row_, LV_SIZE_CONTENT);
    lv_obj_set_layout(header_row_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(header_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        header_row_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(header_row_, CHIP_GAP, 0);
    lv_obj_clear_flag(header_row_, LV_OBJ_FLAG_SCROLLABLE);

    step_badge_ = lv_obj_create(header_row_);
    lv_obj_remove_style_all(step_badge_);
    lv_obj_set_size(step_badge_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(step_badge_, CHIP_PAD, 0);
    lv_obj_set_style_pad_right(step_badge_, CHIP_PAD, 0);
    lv_obj_set_style_pad_top(step_badge_, STEP_BADGE_PAD_V, 0);
    lv_obj_set_style_pad_bottom(step_badge_, STEP_BADGE_PAD_V, 0);
    lv_obj_set_style_bg_color(step_badge_, lv_color_hex(STEP_BADGE_COLOR), 0);
    lv_obj_set_style_bg_opa(step_badge_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(step_badge_, 3, 0);
    lv_obj_clear_flag(step_badge_, LV_OBJ_FLAG_SCROLLABLE);
    auto* stepLabel = createLabel(step_badge_, fonts.inter_12_medium, BG_COLOR);
    lv_obj_center(stepLabel);

    summary_column_ = lv_obj_create(header_row_);
    lv_obj_remove_style_all(summary_column_);
    lv_obj_set_width(summary_column_, 0);
    lv_obj_set_height(summary_column_, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(summary_column_, 1);
    lv_obj_set_layout(summary_column_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(summary_column_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        summary_column_,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_START
    );
    lv_obj_set_style_pad_row(summary_column_, ACTION_TEXT_GAP, 0);
    lv_obj_clear_flag(summary_column_, LV_OBJ_FLAG_SCROLLABLE);

    title_ = createLabel(summary_column_, fonts.inter_14_semibold, TEXT_PRIMARY);
    lv_obj_set_width(title_, LV_PCT(100));
    lv_obj_set_height(title_, LV_SIZE_CONTENT);

    meta_ = createLabel(header_row_, fonts.inter_12_medium, TEXT_SECONDARY, LV_OPA_80);
    lv_obj_set_size(meta_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(meta_, LV_TEXT_ALIGN_RIGHT, 0);

    focus_label_ = createLabel(panel_, fonts.inter_13_bold, TEXT_PRIMARY, LV_OPA_COVER);
    lv_obj_set_width(focus_label_, LV_PCT(100));
    lv_obj_set_height(focus_label_, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(focus_label_, LV_TEXT_ALIGN_CENTER, 0);

    chord_preview_ = lv_obj_create(panel_);
    lv_obj_remove_style_all(chord_preview_);
    lv_obj_set_width(chord_preview_, LV_PCT(100));
    lv_obj_set_height(chord_preview_, LV_SIZE_CONTENT);
    lv_obj_set_layout(chord_preview_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(chord_preview_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        chord_preview_,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_row(chord_preview_, ACTION_TEXT_GAP, 0);
    lv_obj_add_flag(chord_preview_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(chord_preview_, LV_OBJ_FLAG_SCROLLABLE);

    chord_preview_map_ = lv_obj_create(chord_preview_);
    lv_obj_remove_style_all(chord_preview_map_);
    lv_obj_set_size(chord_preview_map_, CHORD_MAP_WIDTH, CHORD_MAP_HEIGHT);
    lv_obj_set_style_bg_color(chord_preview_map_, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_bg_opa(chord_preview_map_, LV_OPA_10, 0);
    lv_obj_set_style_border_width(chord_preview_map_, 1, 0);
    lv_obj_set_style_border_color(chord_preview_map_, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_border_opa(chord_preview_map_, LV_OPA_20, 0);
    lv_obj_set_style_radius(chord_preview_map_, 4, 0);
    lv_obj_clear_flag(chord_preview_map_, LV_OBJ_FLAG_SCROLLABLE);

    chord_preview_timing_rail_ = lv_obj_create(chord_preview_map_);
    lv_obj_remove_style_all(chord_preview_timing_rail_);
    lv_obj_set_pos(chord_preview_timing_rail_, CHORD_MAP_RAIL_X, CHORD_MAP_RAIL_Y);
    lv_obj_set_size(chord_preview_timing_rail_, CHORD_MAP_RAIL_WIDTH, CHORD_MAP_RAIL_HEIGHT);
    lv_obj_set_style_radius(chord_preview_timing_rail_, CHORD_MAP_RAIL_HEIGHT, 0);
    lv_obj_set_style_bg_color(chord_preview_timing_rail_, lv_color_hex(TEXT_SECONDARY), 0);
    lv_obj_set_style_bg_opa(chord_preview_timing_rail_, LV_OPA_30, 0);
    lv_obj_add_flag(chord_preview_timing_rail_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(chord_preview_timing_rail_, LV_OBJ_FLAG_SCROLLABLE);

    chord_preview_timing_span_ = lv_obj_create(chord_preview_map_);
    lv_obj_remove_style_all(chord_preview_timing_span_);
    lv_obj_set_size(chord_preview_timing_span_, 2, CHORD_MAP_RAIL_HEIGHT);
    lv_obj_set_style_radius(chord_preview_timing_span_, CHORD_MAP_RAIL_HEIGHT, 0);
    lv_obj_set_style_bg_opa(chord_preview_timing_span_, LV_OPA_80, 0);
    lv_obj_add_flag(chord_preview_timing_span_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(chord_preview_timing_span_, LV_OBJ_FLAG_SCROLLABLE);

    for (auto*& dot : chord_preview_voice_dots_) {
        dot = lv_obj_create(chord_preview_map_);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 5, 5);
        lv_obj_set_style_radius(dot, 3, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    }

    chord_preview_name_ =
        createLabel(chord_preview_, fonts.inter_13_bold, TEXT_PRIMARY, LV_OPA_COVER);
    lv_obj_set_width(chord_preview_name_, LV_PCT(100));
    lv_obj_set_height(chord_preview_name_, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(chord_preview_name_, LV_TEXT_ALIGN_CENTER, 0);

    chord_preview_detail_ =
        createLabel(chord_preview_, fonts.inter_12_medium, TEXT_SECONDARY, LV_OPA_80);
    lv_obj_set_width(chord_preview_detail_, LV_PCT(100));
    lv_obj_set_height(chord_preview_detail_, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(chord_preview_detail_, LV_TEXT_ALIGN_CENTER, 0);

    trigger_row_ = lv_obj_create(panel_);
    lv_obj_remove_style_all(trigger_row_);
    lv_obj_set_width(trigger_row_, LV_PCT(100));
    lv_obj_set_height(trigger_row_, LV_SIZE_CONTENT);
    lv_obj_set_layout(trigger_row_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(trigger_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        trigger_row_,
        LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(trigger_row_, CHIP_GAP, 0);
    lv_obj_clear_flag(trigger_row_, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < TRIGGER_COUNT; ++i) {
        auto& widgets = trigger_widgets_[i];
        widgets.box = lv_obj_create(trigger_row_);
        lv_obj_remove_style_all(widgets.box);
        lv_obj_set_width(widgets.box, 0);
        lv_obj_set_height(widgets.box, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(widgets.box, 1);
        lv_obj_set_layout(widgets.box, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(widgets.box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            widgets.box,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER
        );
        lv_obj_set_style_radius(widgets.box, CHIP_RADIUS, 0);
        lv_obj_set_style_border_width(widgets.box, 1, 0);
        lv_obj_set_style_pad_left(widgets.box, CHIP_PAD, 0);
        lv_obj_set_style_pad_right(widgets.box, CHIP_PAD, 0);
        lv_obj_set_style_pad_top(widgets.box, TRIGGER_PAD_V, 0);
        lv_obj_set_style_pad_bottom(widgets.box, TRIGGER_PAD_V, 0);
        lv_obj_set_style_pad_row(widgets.box, ACTION_TEXT_GAP, 0);
        lv_obj_clear_flag(widgets.box, LV_OBJ_FLAG_SCROLLABLE);

        widgets.icon = createLabel(widgets.box, standalone_fonts.icons_16, TEXT_SECONDARY);
        lv_obj_set_size(widgets.icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

        widgets.value = createLabel(widgets.box, fonts.inter_14_semibold, TEXT_PRIMARY);
        lv_obj_set_width(widgets.value, LV_PCT(100));
        lv_obj_set_height(widgets.value, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(widgets.value, LV_TEXT_ALIGN_CENTER, 0);
    }

    property_row_ = lv_obj_create(panel_);
    lv_obj_remove_style_all(property_row_);
    lv_obj_set_width(property_row_, LV_PCT(100));
    lv_obj_set_height(property_row_, LV_SIZE_CONTENT);
    lv_obj_set_layout(property_row_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(property_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        property_row_,
        LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(property_row_, CHIP_GAP, 0);
    lv_obj_clear_flag(property_row_, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < MUSICAL_PROPERTY_COUNT; ++i) {
        auto& widgets = property_widgets_[i];
        widgets.box = lv_obj_create(property_row_);
        lv_obj_remove_style_all(widgets.box);
        lv_obj_set_width(widgets.box, 0);
        lv_obj_set_height(widgets.box, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(widgets.box, 1);
        lv_obj_set_layout(widgets.box, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(widgets.box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            widgets.box,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER
        );
        lv_obj_set_style_radius(widgets.box, CHIP_RADIUS, 0);
        lv_obj_set_style_border_width(widgets.box, 1, 0);
        lv_obj_set_style_pad_all(widgets.box, CHIP_PAD, 0);
        lv_obj_set_style_pad_row(widgets.box, ACTION_TEXT_GAP, 0);
        lv_obj_clear_flag(widgets.box, LV_OBJ_FLAG_SCROLLABLE);

        widgets.icon = createLabel(widgets.box, standalone_fonts.icons_14, TEXT_SECONDARY);
        lv_obj_set_size(widgets.icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

        widgets.value = createLabel(widgets.box, fonts.inter_13_medium, TEXT_PRIMARY);
        lv_obj_set_width(widgets.value, LV_PCT(100));
        lv_obj_set_height(widgets.value, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(widgets.value, LV_TEXT_ALIGN_CENTER, 0);
    }

    spacer_ = lv_obj_create(panel_);
    lv_obj_remove_style_all(spacer_);
    lv_obj_set_width(spacer_, LV_PCT(100));
    lv_obj_set_height(spacer_, 0);
    lv_obj_set_flex_grow(spacer_, 1);
    lv_obj_clear_flag(spacer_, LV_OBJ_FLAG_SCROLLABLE);

    action_row_ = lv_obj_create(panel_);
    lv_obj_remove_style_all(action_row_);
    lv_obj_set_width(action_row_, LV_PCT(100));
    lv_obj_set_height(action_row_, LV_SIZE_CONTENT);
    lv_obj_set_layout(action_row_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(action_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        action_row_,
        LV_FLEX_ALIGN_SPACE_BETWEEN,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_column(action_row_, CHIP_GAP, 0);
    lv_obj_clear_flag(action_row_, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < ACTION_COUNT; ++i) {
        auto& widgets = action_widgets_[i];
        widgets.box = lv_obj_create(action_row_);
        lv_obj_remove_style_all(widgets.box);
        lv_obj_set_width(widgets.box, 0);
        lv_obj_set_height(widgets.box, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(widgets.box, 1);
        lv_obj_set_layout(widgets.box, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(widgets.box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            widgets.box,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER
        );
        lv_obj_set_style_radius(widgets.box, CHIP_RADIUS, 0);
        lv_obj_set_style_border_width(widgets.box, 1, 0);
        lv_obj_set_style_pad_all(widgets.box, CHIP_PAD, 0);
        lv_obj_set_style_pad_row(widgets.box, ACTION_TEXT_GAP, 0);
        lv_obj_clear_flag(widgets.box, LV_OBJ_FLAG_SCROLLABLE);

        widgets.icon = createLabel(widgets.box, standalone_fonts.icons_14, TEXT_PRIMARY);
        lv_obj_set_size(widgets.icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

        widgets.value = createLabel(widgets.box, fonts.inter_13_medium, TEXT_PRIMARY);
        lv_obj_set_width(widgets.value, LV_PCT(100));
        lv_obj_set_height(widgets.value, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(widgets.value, LV_TEXT_ALIGN_CENTER, 0);
    }
}

FLASHMEM void SequencerStepEditOverlay::renderChip(
    ChipWidgets& widgets,
    ChipRenderCache& cache,
    const SequencerStepEditPropertyChip& chip,
    bool selected,
    bool active,
    standalone::icons::Size iconSize
) {
    if (!widgets.box || !widgets.icon || !widgets.value) return;

    const char* value = chip.value ? chip.value : "";
    const char* icon = chip.icon ? chip.icon : "";
    const uint32_t color = chip.color == 0 ? TEXT_SECONDARY : chip.color;
    const lv_opa_t iconOpa = selected ? LV_OPA_COVER : (active ? LV_OPA_70 : LV_OPA_30);
    const uint32_t valueColor = selected ? TEXT_PRIMARY : TEXT_SECONDARY;
    const lv_opa_t valueOpa = selected ? LV_OPA_COVER : (active ? LV_OPA_80 : LV_OPA_40);
    const bool colorChanged = !cache.valid || cache.color != color;
    const bool stateChanged =
        !cache.valid || cache.selected != selected || cache.active != active;

    if (colorChanged) {
        lv_obj_set_style_bg_color(widgets.box, lv_color_hex(color), 0);
        lv_obj_set_style_border_color(widgets.box, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(widgets.icon, lv_color_hex(color), 0);
    }
    if (stateChanged) {
        setChipState(widgets.box, selected, active);
    }
    setCachedIcon(widgets.icon, cache.icon, cache.iconSize, icon, iconSize, cache.valid);
    if (cache.iconOpa != static_cast<int16_t>(iconOpa)) {
        lv_obj_set_style_text_opa(widgets.icon, iconOpa, 0);
    }
    setCachedText(widgets.value, cache.value, value);
    if (cache.valueColor != valueColor) {
        lv_obj_set_style_text_color(widgets.value, lv_color_hex(valueColor), 0);
        cache.valueColor = valueColor;
    }
    if (cache.valueOpa != static_cast<int16_t>(valueOpa)) {
        lv_obj_set_style_text_opa(widgets.value, valueOpa, 0);
        cache.valueOpa = static_cast<int16_t>(valueOpa);
    }

    cache.color = color;
    cache.iconOpa = static_cast<int16_t>(iconOpa);
    cache.iconSize = iconSize;
    cache.selected = selected;
    cache.active = active;
    cache.valid = true;
}

FLASHMEM void SequencerStepEditOverlay::renderAction(
    size_t index,
    const SequencerStepEditActionChip& chip,
    bool selected
) {
    if (index >= ACTION_COUNT) return;
    auto& widgets = action_widgets_[index];
    auto& cache = action_cache_[index];
    if (!widgets.box || !widgets.icon || !widgets.value) return;

    const char* value = chip.value ? chip.value : "";
    const char* icon = chip.icon ? chip.icon : "";
    const bool active = (chip.key && chip.key[0] != '\0') ||
                        value[0] != '\0' ||
                        icon[0] != '\0';
    const uint32_t color = chip.color == 0 ? TEXT_SECONDARY : chip.color;
    const lv_opa_t iconOpa = active ? (selected ? LV_OPA_COVER : LV_OPA_70) : LV_OPA_TRANSP;
    const uint32_t valueColor = selected ? TEXT_PRIMARY : TEXT_SECONDARY;
    const lv_opa_t valueOpa = active ? (selected ? LV_OPA_COVER : LV_OPA_70) : LV_OPA_TRANSP;
    const bool colorChanged = !cache.valid || cache.color != color;
    const bool stateChanged =
        !cache.valid || cache.selected != selected || cache.active != active;

    if (colorChanged) {
        lv_obj_set_style_bg_color(widgets.box, lv_color_hex(color), 0);
        lv_obj_set_style_border_color(widgets.box, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(widgets.icon, lv_color_hex(color), 0);
    }
    if (stateChanged) {
        setChipState(widgets.box, selected && active, active, LV_OPA_30);
    }
    setCachedIcon(
        widgets.icon,
        cache.icon,
        cache.iconSize,
        icon,
        standalone::icons::Size::M,
        cache.valid
    );
    if (cache.iconOpa != static_cast<int16_t>(iconOpa)) {
        lv_obj_set_style_text_opa(widgets.icon, iconOpa, 0);
    }
    setCachedText(widgets.value, cache.value, value);
    if (cache.valueColor != valueColor) {
        lv_obj_set_style_text_color(widgets.value, lv_color_hex(valueColor), 0);
        cache.valueColor = valueColor;
    }
    if (cache.valueOpa != static_cast<int16_t>(valueOpa)) {
        lv_obj_set_style_text_opa(widgets.value, valueOpa, 0);
        cache.valueOpa = static_cast<int16_t>(valueOpa);
    }

    cache.color = color;
    cache.iconOpa = static_cast<int16_t>(iconOpa);
    cache.iconSize = standalone::icons::Size::M;
    cache.selected = selected;
    cache.active = active;
    cache.valid = true;
}

FLASHMEM void SequencerStepEditOverlay::render(
    const SequencerStepEditOverlayProps& props
) {
    if (!overlay_) return;
    OC_PERF_SCOPE(perfMutation, "ui.sequencer.step-editor.mutation");
    OC_PERF_UNITS(
        perfMutation,
        props.dataRevision,
        static_cast<uint32_t>(std::count_if(
            props.chordPreview.voices.begin(),
            props.chordPreview.voices.end(),
            [](const auto& voice) { return voice.active; }
        ))
    );

    if (!props.visible) {
        if (visible_cache_) {
            lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
            visible_cache_ = false;
        }
        return;
    }

    if (!visible_cache_) {
        lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(overlay_);
        visible_cache_ = true;
    }

    const bool actionRowVisible = props.actionsVisible || props.chordDetailLayout;

    if (has_rendered_props_cache_ &&
        data_revision_cache_ == props.dataRevision &&
        selected_index_cache_ == props.selectedIndex &&
        actions_visible_cache_ == actionRowVisible &&
        title_centered_cache_ == props.titleCentered &&
        focus_label_visible_cache_ == props.focusLabelVisible &&
        chord_detail_layout_cache_ == props.chordDetailLayout &&
        selected_visual_slot_cache_ == props.selectedVisualSlot &&
        focus_color_cache_ == props.focusColor &&
        title_cache_.color == (props.titleColor == 0 ? TEXT_PRIMARY : props.titleColor)) {
        return;
    }

    if (chord_detail_layout_cache_ != props.chordDetailLayout) {
        if (trigger_row_) {
            if (props.chordDetailLayout) {
                lv_obj_add_flag(trigger_row_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(trigger_row_, LV_OBJ_FLAG_HIDDEN);
            }
        }
        chord_detail_layout_cache_ = props.chordDetailLayout;
    }

    if (action_row_ && actions_visible_cache_ != actionRowVisible) {
        if (actionRowVisible) {
            lv_obj_clear_flag(action_row_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(action_row_, LV_OBJ_FLAG_HIDDEN);
        }
        actions_visible_cache_ = actionRowVisible;
    }

    lv_obj_t* stepLabel = step_badge_ ? lv_obj_get_child(step_badge_, 0) : nullptr;
    if (stepLabel) {
        setCachedText(stepLabel, step_badge_cache_.text, props.stepBadge);
    }
    if (title_) {
        setCachedColor(
            title_,
            title_cache_.color,
            props.titleColor == 0 ? TEXT_PRIMARY : props.titleColor
        );
        setCachedText(title_, title_cache_.text, props.title);
        setCachedOpa(title_, title_cache_.opa, props.enabled ? LV_OPA_COVER : LV_OPA_60);
        if (title_centered_cache_ != props.titleCentered) {
            lv_obj_set_style_text_align(
                title_,
                props.titleCentered ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT,
                0
            );
            title_centered_cache_ = props.titleCentered;
        }
    }
    if (meta_) {
        setCachedText(meta_, meta_cache_.text, props.meta);
    }
    if (focus_label_) {
        if (focus_label_visible_cache_ != props.focusLabelVisible) {
            if (props.focusLabelVisible) {
                lv_obj_clear_flag(focus_label_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(focus_label_, LV_OBJ_FLAG_HIDDEN);
            }
            focus_label_visible_cache_ = props.focusLabelVisible;
        }
        setCachedText(focus_label_, focus_label_cache_.text, props.focusLabel);
        setCachedColor(focus_label_, focus_label_cache_.color, focusColorForSelection(props));
    }
    if (chord_preview_) {
        if (chord_preview_visible_cache_ != props.chordPreview.visible) {
            if (props.chordPreview.visible) {
                lv_obj_clear_flag(chord_preview_, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(chord_preview_, LV_OBJ_FLAG_HIDDEN);
            }
            chord_preview_visible_cache_ = props.chordPreview.visible;
        }
        if (props.chordPreview.visible) {
            const uint32_t previewColor =
                props.chordPreview.color == 0 ? TEXT_PRIMARY : props.chordPreview.color;
            const bool previewColorChanged =
                setCachedColor(chord_preview_name_, chord_preview_color_cache_, previewColor);
            setCachedText(
                chord_preview_name_,
                chord_preview_name_cache_.text,
                props.chordPreview.name
            );
            setCachedText(
                chord_preview_detail_,
                chord_preview_detail_cache_.text,
                props.chordPreview.detail
            );
            if (chord_preview_detail_) {
                const bool hasDetail =
                    props.chordPreview.detail && props.chordPreview.detail[0] != '\0';
                if (chord_preview_detail_visible_cache_ != hasDetail) {
                    if (hasDetail) {
                        lv_obj_clear_flag(chord_preview_detail_, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(chord_preview_detail_, LV_OBJ_FLAG_HIDDEN);
                    }
                    chord_preview_detail_visible_cache_ = hasDetail;
                }
            }
            if (chord_preview_map_) {
                if (previewColorChanged) {
                    lv_obj_set_style_bg_color(chord_preview_map_, lv_color_hex(previewColor), 0);
                    lv_obj_set_style_border_color(chord_preview_map_, lv_color_hex(previewColor), 0);
                }
                if (chord_preview_map_visible_cache_ != props.chordPreview.mapVisible) {
                    if (props.chordPreview.mapVisible) {
                        lv_obj_clear_flag(chord_preview_map_, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(chord_preview_map_, LV_OBJ_FLAG_HIDDEN);
                    }
                    chord_preview_map_visible_cache_ = props.chordPreview.mapVisible;
                }
            }
            const bool timingVisible =
                props.chordPreview.mapVisible && props.chordPreview.timingVisible;
            if (chord_preview_timing_visible_cache_ != timingVisible) {
                if (chord_preview_timing_rail_) {
                    if (timingVisible) {
                        lv_obj_clear_flag(chord_preview_timing_rail_, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(chord_preview_timing_rail_, LV_OBJ_FLAG_HIDDEN);
                    }
                }
                if (chord_preview_timing_span_) {
                    if (timingVisible) {
                        lv_obj_clear_flag(chord_preview_timing_span_, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(chord_preview_timing_span_, LV_OBJ_FLAG_HIDDEN);
                    }
                }
                chord_preview_timing_visible_cache_ = timingVisible;
            }
            if (timingVisible && chord_preview_timing_span_) {
                const uint8_t timingStart =
                    std::min(props.chordPreview.timingStart, props.chordPreview.timingEnd);
                const uint8_t timingEnd =
                    std::max(props.chordPreview.timingStart, props.chordPreview.timingEnd);
                const lv_coord_t startX = static_cast<lv_coord_t>(
                    CHORD_MAP_RAIL_X + railCoord(timingStart)
                );
                const lv_coord_t endX = static_cast<lv_coord_t>(
                    CHORD_MAP_RAIL_X + railCoord(timingEnd)
                );
                const lv_coord_t spanWidth = std::max<lv_coord_t>(2, endX - startX);
                const uint32_t timingColor = props.chordPreview.timingColor == 0
                    ? previewColor
                    : props.chordPreview.timingColor;
                if (chord_preview_timing_x_cache_ != startX ||
                    chord_preview_timing_width_cache_ != spanWidth) {
                    lv_obj_set_pos(chord_preview_timing_span_, startX, CHORD_MAP_RAIL_Y);
                    lv_obj_set_size(
                        chord_preview_timing_span_,
                        spanWidth,
                        CHORD_MAP_RAIL_HEIGHT
                    );
                    chord_preview_timing_x_cache_ = startX;
                    chord_preview_timing_width_cache_ = spanWidth;
                }
                if (chord_preview_timing_color_cache_ != timingColor) {
                    lv_obj_set_style_bg_color(
                        chord_preview_timing_span_,
                        lv_color_hex(timingColor),
                        0
                    );
                    chord_preview_timing_color_cache_ = timingColor;
                }
            }
            for (size_t i = 0; i < chord_preview_voice_dots_.size(); ++i) {
                auto* dot = chord_preview_voice_dots_[i];
                if (!dot) continue;
                const auto& voice = props.chordPreview.voices[i];
                auto& cache = chord_preview_voice_cache_[i];
                if (!props.chordPreview.mapVisible || !voice.active) {
                    if (cache.active) {
                        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
                        cache = {};
                    }
                    continue;
                }
                const lv_coord_t width =
                    std::max<lv_coord_t>(3, voice.width == 0 ? voice.size : voice.width);
                const lv_coord_t height =
                    std::max<lv_coord_t>(3, voice.height == 0 ? voice.size : voice.height);
                const lv_coord_t x = static_cast<lv_coord_t>(
                    CHORD_MAP_RAIL_X + markerCoord(voice.x, CHORD_MAP_RAIL_WIDTH, width)
                );
                const lv_coord_t y = static_cast<lv_coord_t>(
                    CHORD_MAP_NOTE_TOP + markerCoord(voice.y, CHORD_MAP_NOTE_HEIGHT, height)
                );
                const uint32_t markerColor = voice.color == 0 ? previewColor : voice.color;
                if (!cache.active) {
                    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
                    cache.active = true;
                }
                if (cache.width != width || cache.height != height) {
                    lv_obj_set_size(dot, width, height);
                    lv_obj_set_style_radius(
                        dot,
                        static_cast<lv_coord_t>(std::max(width, height) / 2 + 1),
                        0
                    );
                    cache.width = width;
                    cache.height = height;
                }
                if (cache.x != x || cache.y != y) {
                    lv_obj_set_pos(dot, x, y);
                    cache.x = x;
                    cache.y = y;
                }
                if (cache.color != markerColor) {
                    lv_obj_set_style_bg_color(dot, lv_color_hex(markerColor), 0);
                    cache.color = markerColor;
                }
                if (cache.opa != static_cast<int16_t>(voice.opa)) {
                    lv_obj_set_style_bg_opa(dot, voice.opa, 0);
                    cache.opa = static_cast<int16_t>(voice.opa);
                }
            }
        }
    }

    const uint32_t selectedProperty = selectedRowToPropertyIndex(props.selectedIndex);
    if (!props.chordDetailLayout) {
        renderChip(
            trigger_widgets_[TRIGGER_STATE_INDEX],
            trigger_cache_[TRIGGER_STATE_INDEX],
            props.state,
            selectedVisualSlot(
                props,
                SequencerStepEditVisualSlot::STATE,
                props.selectedIndex == core::state::sequencer::step_edit_rows::ACTIVATED
            ),
            true,
            standalone::icons::Size::L
        );
        renderChip(
            trigger_widgets_[TRIGGER_CHANCE_INDEX],
            trigger_cache_[TRIGGER_CHANCE_INDEX],
            props.properties[CHANCE_INDEX],
            selectedVisualSlot(
                props,
                SequencerStepEditVisualSlot::CHANCE,
                selectedProperty == CHANCE_INDEX
            ),
            props.enabled,
            standalone::icons::Size::L
        );
    }

    constexpr size_t musicalIndices[] = {
        PITCH_INDEX,
        VELOCITY_INDEX,
        GATE_INDEX,
        NUDGE_INDEX,
    };
    constexpr SequencerStepEditVisualSlot chordCompositionSlots[] = {
        SequencerStepEditVisualSlot::CHORD_MODE,
        SequencerStepEditVisualSlot::CHORD_HARMONY,
        SequencerStepEditVisualSlot::CHORD_VOICES,
        SequencerStepEditVisualSlot::CHORD_INVERSION,
    };
    for (size_t i = 0; i < MUSICAL_PROPERTY_COUNT; ++i) {
        const size_t propertyIndex = musicalIndices[i];
        const auto slot = props.chordDetailLayout
            ? chordCompositionSlots[i]
            : static_cast<SequencerStepEditVisualSlot>(
                  static_cast<uint8_t>(SequencerStepEditVisualSlot::PITCH) + i
              );
        renderChip(
            property_widgets_[i],
            property_cache_[i],
            props.properties[propertyIndex],
            selectedVisualSlot(props, slot, selectedProperty == propertyIndex),
            props.enabled,
            standalone::icons::Size::M
        );
    }

    if (props.chordDetailLayout) {
        constexpr SequencerStepEditVisualSlot chordPerformanceSlots[] = {
            SequencerStepEditVisualSlot::CHORD_VOICING,
            SequencerStepEditVisualSlot::CHORD_STRUM,
            SequencerStepEditVisualSlot::CHORD_VELOCITY,
        };
        for (size_t i = 0; i < CHORD_PERFORMANCE_COUNT; ++i) {
            const auto& chip = props.chordPerformance[i];
            renderAction(
                i,
                SequencerStepEditActionChip{
                    .key = chip.key,
                    .value = chip.value,
                    .icon = chip.icon,
                    .color = chip.color,
                },
                selectedVisualSlot(props, chordPerformanceSlots[i], false)
            );
        }
    } else if (props.actionsVisible) {
        const uint32_t selectedAction = selectedRowToActionIndex(props.selectedIndex);
        for (size_t i = 0; i < ACTION_COUNT; ++i) {
            const auto slot = static_cast<SequencerStepEditVisualSlot>(
                static_cast<uint8_t>(SequencerStepEditVisualSlot::ACTION_0) + i
            );
            renderAction(i, props.actions[i], selectedVisualSlot(props, slot, selectedAction == i));
        }
    }

    // LVGL batches any required layout with the next refresh. Do not force an
    // immediate whole-screen layout from the state notification path.
    has_rendered_props_cache_ = true;
    data_revision_cache_ = props.dataRevision;
    selected_index_cache_ = props.selectedIndex;
    actions_visible_cache_ = actionRowVisible;
    title_centered_cache_ = props.titleCentered;
    focus_label_visible_cache_ = props.focusLabelVisible;
    chord_detail_layout_cache_ = props.chordDetailLayout;
    selected_visual_slot_cache_ = props.selectedVisualSlot;
    focus_color_cache_ = props.focusColor;
}

}  // namespace core::ui

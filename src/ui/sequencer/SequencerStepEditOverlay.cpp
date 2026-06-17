#include "ui/sequencer/SequencerStepEditOverlay.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <ms/ui/font/CoreFonts.hpp>
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

lv_obj_t* createLabel(lv_obj_t* parent,
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

bool sameText(const char* lhs, const char* rhs) {
    if (lhs == rhs) return true;
    if (!lhs || !rhs) return false;
    return std::strcmp(lhs, rhs) == 0;
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
    lv_label_set_text(label, cache.data());
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

void styleChip(lv_obj_t* box,
               uint32_t color,
               bool selected,
               bool active,
               lv_opa_t idleBorderOpa = LV_OPA_30) {
    if (!box) return;
    lv_obj_set_style_bg_color(box, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(
        box,
        selected ? LV_OPA_20 : (active ? LV_OPA_10 : LV_OPA_TRANSP),
        0
    );
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(
        box,
        selected ? LV_OPA_80 : (active ? idleBorderOpa : LV_OPA_20),
        0
    );
}

void setIcon(lv_obj_t* label,
             const char* icon,
             standalone::icons::Size size,
             uint32_t color,
             lv_opa_t opa) {
    if (!label) return;
    standalone::icons::set(label, icon ? icon : "", size);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(label, opa, 0);
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
    if (selectedIndex == MICRO_SEQUENCE) return 0;
    if (selectedIndex == CYCLE_STATES) return 1;
    return 255U;
}

uint32_t focusColorForSelection(const SequencerStepEditOverlayProps& props) {
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
        static_cast<lv_coord_t>(theme::layout::TRANSPORT_BAR_HEIGHT + PANEL_PAD),
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
    const bool shellChanged =
        !cache.valid ||
        cache.color != color ||
        cache.selected != selected ||
        cache.active != active;

    if (shellChanged) {
        styleChip(widgets.box, color, selected, active);
    }
    if (shellChanged ||
        !sameText(cache.icon, icon) ||
        cache.iconSize != iconSize ||
        cache.iconOpa != static_cast<int16_t>(iconOpa)) {
        setIcon(widgets.icon, icon, iconSize, color, iconOpa);
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

    cache.icon = icon;
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
    const uint32_t color = chip.color == 0 ? TEXT_SECONDARY : chip.color;
    const lv_opa_t iconOpa = selected ? LV_OPA_COVER : LV_OPA_70;
    const uint32_t valueColor = selected ? TEXT_PRIMARY : TEXT_SECONDARY;
    const lv_opa_t valueOpa = selected ? LV_OPA_COVER : LV_OPA_70;
    const bool shellChanged =
        !cache.valid ||
        cache.color != color ||
        cache.selected != selected ||
        !cache.active;

    if (shellChanged) {
        styleChip(widgets.box, color, selected, true, LV_OPA_30);
    }
    if (shellChanged ||
        !sameText(cache.icon, icon) ||
        cache.iconSize != standalone::icons::Size::M ||
        cache.iconOpa != static_cast<int16_t>(iconOpa)) {
        setIcon(widgets.icon, icon, standalone::icons::Size::M, color, iconOpa);
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

    cache.icon = icon;
    cache.color = color;
    cache.iconOpa = static_cast<int16_t>(iconOpa);
    cache.iconSize = standalone::icons::Size::M;
    cache.selected = selected;
    cache.active = true;
    cache.valid = true;
}

FLASHMEM void SequencerStepEditOverlay::resetRenderCaches() {
    has_rendered_props_cache_ = false;
    data_revision_cache_ = 0;
    selected_index_cache_ = -1;
    step_badge_cache_ = {};
    title_cache_ = {};
    meta_cache_ = {};
    focus_label_cache_ = {};
    for (auto& cache : trigger_cache_) {
        cache = {};
    }
    for (auto& cache : property_cache_) {
        cache = {};
    }
    for (auto& cache : action_cache_) {
        cache = {};
    }
}

FLASHMEM void SequencerStepEditOverlay::render(
    const SequencerStepEditOverlayProps& props
) {
    if (!overlay_) return;

    if (!props.visible) {
        if (visible_cache_) {
            lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
            visible_cache_ = false;
            resetRenderCaches();
        }
        return;
    }

    const bool wasHidden = !visible_cache_;
    if (!visible_cache_) {
        lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(overlay_);
        visible_cache_ = true;
    }

    if (has_rendered_props_cache_ &&
        data_revision_cache_ == props.dataRevision &&
        selected_index_cache_ == props.selectedIndex) {
        return;
    }

    lv_obj_t* stepLabel = step_badge_ ? lv_obj_get_child(step_badge_, 0) : nullptr;
    if (stepLabel) {
        setCachedText(stepLabel, step_badge_cache_.text, props.stepBadge);
    }
    if (title_) {
        setCachedText(title_, title_cache_.text, props.title);
        setCachedOpa(title_, title_cache_.opa, props.enabled ? LV_OPA_COVER : LV_OPA_60);
    }
    if (meta_) {
        setCachedText(meta_, meta_cache_.text, props.meta);
    }
    if (focus_label_) {
        setCachedText(focus_label_, focus_label_cache_.text, props.focusLabel);
        setCachedColor(focus_label_, focus_label_cache_.color, focusColorForSelection(props));
    }

    const uint32_t selectedProperty = selectedRowToPropertyIndex(props.selectedIndex);
    renderChip(
        trigger_widgets_[TRIGGER_STATE_INDEX],
        trigger_cache_[TRIGGER_STATE_INDEX],
        props.state,
        props.selectedIndex == core::state::sequencer::step_edit_rows::ACTIVATED,
        true,
        standalone::icons::Size::L
    );
    renderChip(
        trigger_widgets_[TRIGGER_CHANCE_INDEX],
        trigger_cache_[TRIGGER_CHANCE_INDEX],
        props.properties[CHANCE_INDEX],
        selectedProperty == CHANCE_INDEX,
        props.enabled,
        standalone::icons::Size::L
    );

    constexpr size_t musicalIndices[] = {
        PITCH_INDEX,
        VELOCITY_INDEX,
        GATE_INDEX,
        NUDGE_INDEX,
    };
    for (size_t i = 0; i < MUSICAL_PROPERTY_COUNT; ++i) {
        const size_t propertyIndex = musicalIndices[i];
        renderChip(
            property_widgets_[i],
            property_cache_[i],
            props.properties[propertyIndex],
            selectedProperty == propertyIndex,
            props.enabled,
            standalone::icons::Size::M
        );
    }

    const uint32_t selectedAction = selectedRowToActionIndex(props.selectedIndex);
    for (size_t i = 0; i < ACTION_COUNT; ++i) {
        renderAction(i, props.actions[i], selectedAction == i);
    }

    if (wasHidden) {
        lv_obj_update_layout(panel_);
    }
    has_rendered_props_cache_ = true;
    data_revision_cache_ = props.dataRevision;
    selected_index_cache_ = props.selectedIndex;
}

}  // namespace core::ui

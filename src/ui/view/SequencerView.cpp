#include "SequencerView.hpp"

#include <cstdio>

#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <ms/ui/font/CoreFonts.hpp>

#include "ui/sequencer/StepVisualUtils.hpp"

namespace theme = oc::ui::lvgl::base_theme;
namespace style = oc::ui::lvgl::style;

namespace core::ui {

namespace {

constexpr uint32_t COLOR_STEP_PLAY_HEX = 0x5CA8EE;
constexpr uint32_t COLOR_STEP_SELECTOR_HEX = theme::color::TEXT_PRIMARY;

constexpr uint8_t CHROMATIC_NOTE_COUNT = 12;

constexpr lv_coord_t STEP_BUTTON_SIZE = 56;
constexpr lv_coord_t STEP_MIN_SHAPE_SIZE = 14;
constexpr lv_coord_t STEP_SHAPE_PADDING = 4;
constexpr lv_coord_t STEP_SHAPE_RADIUS = 3;

constexpr lv_coord_t STEP_BAR_HEIGHT = 4;
constexpr lv_coord_t STEP_BAR_SELECTOR_TOP_GAP = 1;
constexpr lv_coord_t STEP_BAR_PLAYHEAD_BOTTOM_GAP = 2;
constexpr lv_opa_t STEP_BAR_ACTIVE_OPA = LV_OPA_COVER;
constexpr lv_opa_t STEP_BAR_SELECTOR_OPA = LV_OPA_80;

constexpr uint8_t SHAPE_DISABLED_FILL_BRIGHTNESS = 20;
constexpr uint8_t NOTE_TEXT_DISABLED_BRIGHTNESS = 38;

constexpr lv_opa_t STEP_SHAPE_OPA_ENABLED = LV_OPA_COVER;
constexpr lv_opa_t STEP_SHAPE_OPA_DISABLED = LV_OPA_70;

constexpr uint16_t GATE_MAX = 100;
constexpr uint8_t VELOCITY_MAX = 127;

// Pitch visuals:
// - fill: chromatic class (12 semitones)
// - note text: same chromatic color
constexpr uint32_t CHROMATIC_NOTE_PALETTE_HEX[] = {
    0xF4F1DE,  // C
    0xEAB69E,  // C#
    0xE07A5F,  // D
    0xAA675E,  // D#
    0x73535C,  // E
    0x3D405B,  // F
    0x5F797A,  // F#
    0x81B29A,  // G
    0xBABF94,  // G#
    0xF2CC8F,  // A
    0xF3D8A9,  // A#
    0xF3E5C4,  // B
};

constexpr uint8_t STEP_SHAPE_MAX_SIZE = static_cast<uint8_t>(STEP_BUTTON_SIZE - (2 * STEP_SHAPE_PADDING));

struct StepVisualStyle {
    lv_coord_t width = STEP_MIN_SHAPE_SIZE;
    lv_coord_t height = STEP_MIN_SHAPE_SIZE;
    lv_color_t noteTextColor = lv_color_hex(theme::color::TEXT_SECONDARY);
    lv_color_t fillColor = lv_color_hex(theme::color::INACTIVE);
    lv_opa_t fillOpa = STEP_SHAPE_OPA_DISABLED;
};

StepVisualStyle buildStepVisualStyle(uint8_t note, uint8_t velocity, uint16_t gate, bool enabled) {
    using namespace core::ui::sequencer::visual;

    StepVisualStyle style;
    style.width = static_cast<lv_coord_t>(mapToRangeU8(gate, GATE_MAX, STEP_MIN_SHAPE_SIZE, STEP_SHAPE_MAX_SIZE));
    style.height = static_cast<lv_coord_t>(
        mapToRangeU8(velocity, VELOCITY_MAX, STEP_MIN_SHAPE_SIZE, STEP_SHAPE_MAX_SIZE)
    );

    if (enabled) {
        const uint8_t chromaIndex = static_cast<uint8_t>(note % CHROMATIC_NOTE_COUNT);

        style.fillColor = lv_color_hex(CHROMATIC_NOTE_PALETTE_HEX[chromaIndex]);
        style.noteTextColor = style.fillColor;
        style.fillOpa = STEP_SHAPE_OPA_ENABLED;
        return style;
    }

    style.fillColor = grayscaleColor(SHAPE_DISABLED_FILL_BRIGHTNESS);
    style.noteTextColor = grayscaleColor(NOTE_TEXT_DISABLED_BRIGHTNESS);
    style.fillOpa = STEP_SHAPE_OPA_DISABLED;

    return style;
}

void formatNoteName(char* buf, size_t bufSize, uint8_t midiNote) {
    static const char* NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int idx = static_cast<int>(midiNote) % 12;
    const int octave = static_cast<int>(midiNote) / 12 - 1;
    snprintf(buf, bufSize, "%s%d", NAMES[idx], octave);
}

}  // namespace

SequencerView::SequencerView(lv_obj_t* parent, core::state::CoreState& coreState)
    : core_state_(coreState) {
    createLayout(parent);
    createHeaderBar();
    createSteps();
    bindToState();
}

SequencerView::~SequencerView() {
    if (render_timer_) {
        lv_timer_delete(render_timer_);
        render_timer_ = nullptr;
    }

    header_bar_.reset();
    layout_.reset();
    container_ = nullptr;
    body_container_ = nullptr;
}

void SequencerView::onActivate() {
    if (container_) {
        lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
        render();
        dirty_ = false;
    }
}

void SequencerView::onDeactivate() {
    if (container_) {
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
    }

    if (render_timer_) {
        lv_timer_delete(render_timer_);
        render_timer_ = nullptr;
    }
}

void SequencerView::createLayout(lv_obj_t* parent) {
    layout_ = std::make_unique<ms::ui::LayoutView>(parent);
    container_ = layout_->getElement();
    body_container_ = layout_->content();

    // Body styling
    style::apply(body_container_).transparent().pad(theme::layout::MARGIN_MD);
    lv_obj_set_layout(body_container_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(body_container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body_container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(body_container_, theme::layout::ROW_GAP_MD, 0);
}

void SequencerView::createHeaderBar() {
    if (!layout_) return;
    header_bar_ = std::make_unique<SequencerHeaderBar>(layout_->header());
}

void SequencerView::createSteps() {
    grid_ = lv_obj_create(body_container_);
    style::apply(grid_).size(LV_PCT(100), LV_PCT(100)).transparent().noBorder();
    lv_obj_set_flex_grow(grid_, 1);

    static lv_coord_t col_dsc[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid_, col_dsc, row_dsc);
    lv_obj_set_layout(grid_, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_column(grid_, theme::layout::MARGIN_SM, 0);
    lv_obj_set_style_pad_row(grid_, theme::layout::MARGIN_SM, 0);
    lv_obj_add_flag(grid_, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        uint8_t col = i % 4;
        uint8_t row = i / 4;

        lv_obj_t* tile = lv_obj_create(grid_);
        tiles_[i] = tile;
        style::apply(tile).transparent().noBorder().pad(0).noScroll();
        lv_obj_add_flag(tile, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_grid_cell(tile,
            LV_GRID_ALIGN_STRETCH, col, 1,
            LV_GRID_ALIGN_STRETCH, row, 1);

        lv_obj_set_layout(tile, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(tile, theme::layout::MARGIN_XS, 0);

        // Note label (top-left)
        lv_obj_t* note = lv_label_create(tile);
        note_labels_[i] = note;
        lv_label_set_text(note, "");
        lv_obj_set_width(note, LV_PCT(100));
        lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_font(note, fonts.inter_14_semibold, 0);
        lv_obj_set_style_text_color(note, lv_color_hex(theme::color::TEXT_SECONDARY), 0);

        // Button wrapper to center the step button
        lv_obj_t* btnWrap = lv_obj_create(tile);
        lv_obj_remove_style_all(btnWrap);
        lv_obj_clear_flag(btnWrap, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(btnWrap, LV_PCT(100));
        lv_obj_set_flex_grow(btnWrap, 1);
        lv_obj_set_layout(btnWrap, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(btnWrap, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btnWrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Step button (transparent container + overlays)
        lv_obj_t* btn = lv_obj_create(btnWrap);
        step_buttons_[i] = btn;
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_size(btn, STEP_BUTTON_SIZE, STEP_BUTTON_SIZE);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(theme::color::TEXT_PRIMARY), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(theme::color::INACTIVE), 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t* shape = lv_obj_create(btn);
        step_shapes_[i] = shape;
        lv_obj_clear_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(shape, STEP_MIN_SHAPE_SIZE, STEP_MIN_SHAPE_SIZE);
        lv_obj_set_style_radius(shape, STEP_SHAPE_RADIUS, 0);
        lv_obj_set_style_border_width(shape, 0, 0);
        lv_obj_set_style_bg_opa(shape, STEP_SHAPE_OPA_ENABLED, 0);
        lv_obj_set_style_bg_color(shape, lv_color_hex(theme::color::INACTIVE_LIGHTER), 0);
        lv_obj_align(shape, LV_ALIGN_BOTTOM_MID, 0, -STEP_SHAPE_PADDING);

        // Selector bar: full width, above step container
        lv_obj_t* sel = lv_obj_create(btn);
        step_selectors_[i] = sel;
        lv_obj_clear_flag(sel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(sel, STEP_BUTTON_SIZE, STEP_BAR_HEIGHT);
        lv_obj_set_style_radius(sel, STEP_BAR_HEIGHT / 2, 0);
        lv_obj_set_style_border_width(sel, 0, 0);
        lv_obj_set_style_bg_opa(sel, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(sel, lv_color_hex(COLOR_STEP_SELECTOR_HEX), 0);
        lv_obj_align(sel, LV_ALIGN_OUT_TOP_MID, 0, -STEP_BAR_SELECTOR_TOP_GAP);

        // Playhead bar: full width, outside below container
        lv_obj_t* ph = lv_obj_create(btn);
        step_indicators_[i] = ph;
        lv_obj_clear_flag(ph, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(ph, STEP_BUTTON_SIZE, STEP_BAR_HEIGHT);
        lv_obj_set_style_radius(ph, STEP_BAR_HEIGHT / 2, 0);
        lv_obj_set_style_border_width(ph, 0, 0);
        lv_obj_set_style_bg_opa(ph, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(ph, lv_color_hex(COLOR_STEP_PLAY_HEX), 0);
        lv_obj_align(ph, LV_ALIGN_OUT_BOTTOM_MID, 0, STEP_BAR_PLAYHEAD_BOTTOM_GAP);
    }
}

void SequencerView::bindToState() {
    watcher_.watchAll(
        [this]() { requestRender(); },
        core_state_.sequencer.length,
        core_state_.sequencer.stepsPerBeat,
        core_state_.sequencer.page,
        core_state_.sequencer.enabledMask,
        core_state_.sequencer.focusedStep,
        core_state_.sequencer.playheadStep,
        core_state_.sequencer.stepDataRevision
    );

    render();
    dirty_ = false;
}

void SequencerView::requestRender() {
    dirty_ = true;

    if (!container_) return;
    if (lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;

    // Schedule a single render at ~60Hz max (coalesces bursts of updates).
    if (!render_timer_) {
        render_timer_ = lv_timer_create(onRenderTimer, 16, this);
        lv_timer_set_repeat_count(render_timer_, 1);
    }
}

void SequencerView::onRenderTimer(lv_timer_t* timer) {
    auto* self = static_cast<SequencerView*>(lv_timer_get_user_data(timer));
    if (!self) return;

    // One-shot timer: LVGL will delete it after the callback.
    self->render_timer_ = nullptr;

    if (!self->dirty_) return;
    if (!self->container_) return;
    if (lv_obj_has_flag(self->container_, LV_OBJ_FLAG_HIDDEN)) return;

    self->render();
    self->dirty_ = false;
}

void SequencerView::render() {
    if (!container_) return;
    if (lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN)) return;

    const uint8_t len = core_state_.sequencer.length.get();
    constexpr uint8_t stepsPerPage = core::state::sequencer::SequencerState::STEPS_PER_PAGE;

    const uint8_t activePages = (len == 0)
        ? 0
        : static_cast<uint8_t>((len + stepsPerPage - 1) / stepsPerPage);
    const uint8_t page = (activePages == 0)
        ? 0
        : static_cast<uint8_t>(core_state_.sequencer.page.get() % activePages);

    const uint8_t startStep = static_cast<uint8_t>(page * stepsPerPage);
    const uint64_t mask = core_state_.sequencer.enabledMask.get();
    const uint8_t focused = core_state_.sequencer.focusedStep.get();
    const int16_t playhead = core_state_.sequencer.playheadStep.get();

    if (header_bar_) {
        header_bar_->render({
            .length = len,
            .viewedPage = page,
            .playheadStep = playhead,
            .stepsPerBeat = core_state_.sequencer.stepsPerBeat.get(),
        });
    }

    // Step tiles (8 visible)
    for (uint8_t i = 0; i < tiles_.size(); ++i) {
        const uint8_t abs = static_cast<uint8_t>(startStep + i);
        const bool inPattern = (abs < len);
        const bool enabled = inPattern ? ((mask & (1ULL << abs)) != 0) : false;
        const bool isFocused = inPattern && (abs == focused);
        const bool isPlaying = inPattern && (playhead >= 0) && (abs == static_cast<uint8_t>(playhead));

        uint8_t note = 0;
        uint8_t velocity = 0;
        uint16_t gate = 0;
        if (inPattern) {
            note = core_state_.sequencer.note[abs];
            velocity = core_state_.sequencer.velocity[abs];
            gate = core_state_.sequencer.gate[abs];
            if (gate > GATE_MAX) gate = GATE_MAX;
        }

        const StepVisualStyle visual = buildStepVisualStyle(note, velocity, gate, enabled);

        // Note label
        if (note_labels_[i]) {
            if (inPattern) {
                char buf[8];
                formatNoteName(buf, sizeof(buf), note);
                lv_label_set_text(note_labels_[i], buf);
                lv_obj_set_style_text_color(note_labels_[i], visual.noteTextColor, 0);
            } else {
                lv_label_set_text(note_labels_[i], " ");
                lv_obj_set_style_text_color(note_labels_[i], lv_color_hex(theme::color::TEXT_SECONDARY), 0);
            }
        }

        // Step container (invisible background, no border)
        if (step_buttons_[i]) {
            lv_obj_set_style_bg_opa(step_buttons_[i], LV_OPA_TRANSP, 0);
        }

        // Internal shape (gate -> width, velocity -> height)
        if (step_shapes_[i]) {
            if (!inPattern) {
                lv_obj_add_flag(step_shapes_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(step_shapes_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_size(step_shapes_[i], visual.width, visual.height);
                lv_obj_align(step_shapes_[i], LV_ALIGN_BOTTOM_MID, 0, -STEP_SHAPE_PADDING);
                lv_obj_set_style_bg_color(step_shapes_[i], visual.fillColor, 0);
                lv_obj_set_style_bg_opa(step_shapes_[i], visual.fillOpa, 0);
                lv_obj_set_style_border_width(step_shapes_[i], 0, 0);
            }
        }

        // Selector bar (focus only)
        if (step_selectors_[i]) {
            if (!inPattern) {
                lv_obj_add_flag(step_selectors_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(step_selectors_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_color(step_selectors_[i], lv_color_hex(COLOR_STEP_SELECTOR_HEX), 0);
                lv_obj_set_style_bg_opa(step_selectors_[i], isFocused ? STEP_BAR_SELECTOR_OPA : LV_OPA_TRANSP, 0);
            }
        }

        // Playhead bar (active step only)
        if (step_indicators_[i]) {
            if (!inPattern) {
                lv_obj_add_flag(step_indicators_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(step_indicators_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_color(step_indicators_[i], lv_color_hex(COLOR_STEP_PLAY_HEX), 0);
                lv_obj_set_style_bg_opa(step_indicators_[i], isPlaying ? STEP_BAR_ACTIVE_OPA : LV_OPA_TRANSP, 0);
            }
        }
    }
}

}  // namespace core::ui

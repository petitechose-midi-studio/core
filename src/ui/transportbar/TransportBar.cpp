#include "TransportBar.hpp"

#include <cstdio>
#include <oc/state/Bind.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <ms/ui/font/CoreFonts.hpp>
#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace theme = standalone::theme;
namespace icons = standalone::icons;
namespace style = oc::ui::lvgl::style;

namespace {
const lv_color_t COLOR_INACTIVE = lv_color_hex(theme::color::MIDI_INACTIVE);
const lv_color_t COLOR_IN_ACTIVE = lv_color_hex(theme::color::MIDI_IN_ACTIVE);
const lv_color_t COLOR_OUT_ACTIVE = lv_color_hex(theme::color::MIDI_OUT_ACTIVE);
const lv_color_t COLOR_PLAY_INACTIVE = lv_color_hex(theme::color::PLAY_INACTIVE);
const lv_color_t COLOR_PLAY_ACTIVE = lv_color_hex(theme::color::PLAY_ACTIVE);
const lv_color_t COLOR_TEXT = lv_color_hex(theme::color::TEXT_SECONDARY);
const lv_color_t COLOR_BEAT = lv_color_hex(theme::color::BEAT_PULSE);
}  // namespace

TransportBar::TransportBar(lv_obj_t* parent, core::state::StatusBarState& state)
    : state_(state) {
    createLayout(parent);
    setupBindings();
    render();
}

TransportBar::~TransportBar() {
    // Cleanup timers first (they reference indicators)
    if (note_in_timer_) { lv_timer_delete(note_in_timer_); note_in_timer_ = nullptr; }
    if (note_out_timer_) { lv_timer_delete(note_out_timer_); note_out_timer_ = nullptr; }
    if (cc_in_timer_) { lv_timer_delete(cc_in_timer_); cc_in_timer_ = nullptr; }
    if (cc_out_timer_) { lv_timer_delete(cc_out_timer_); cc_out_timer_ = nullptr; }
    if (beat_timer_) { lv_timer_delete(beat_timer_); beat_timer_ = nullptr; }

    // Clear subscriptions before destroying UI
    subs_.clear();

    // unique_ptr members auto-cleanup, then delete container
    if (container_) {
        lv_obj_delete(container_);
    }
}

void TransportBar::createLayout(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, LV_PCT(100), theme::layout::TRANSPORT_BAR_HEIGHT);
    style::apply(container_).bgColor(theme::color::BACKGROUND);

    // 3 columns: MIDI indicators | Play (center) | Beat + Tempo (right)
    static const int32_t col_dsc[] = {
        LV_GRID_FR(1),  // MIDI indicators
        LV_GRID_FR(1),  // Play (centered)
        LV_GRID_FR(1),  // Beat + Tempo
        LV_GRID_TEMPLATE_LAST
    };
    static const int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(container_, col_dsc, row_dsc);
    lv_obj_set_layout(container_, LV_LAYOUT_GRID);

    createMidiIndicators(container_);
    createTransportCenter(container_);
    createTempoWithBeat(container_);
}

void TransportBar::createMidiIndicators(lv_obj_t* parent) {
    // Cell 0: MIDI indicators (Note IN/OUT + CC IN/OUT)
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    style::apply(cell).flexRow(LV_FLEX_ALIGN_START, theme::layout::GAP_SM).padLeft(theme::layout::PAD_MD);

    // Note IN icon
    note_in_icon_ = lv_label_create(cell);
    icons::set(note_in_icon_, icons::NOTE, icons::Size::M);
    lv_obj_set_style_text_color(note_in_icon_, COLOR_INACTIVE, 0);

    // Note OUT icon
    note_out_icon_ = lv_label_create(cell);
    icons::set(note_out_icon_, icons::NOTE, icons::Size::M);
    lv_obj_set_style_text_color(note_out_icon_, COLOR_INACTIVE, 0);

    // CC IN icon
    cc_in_icon_ = lv_label_create(cell);
    icons::set(cc_in_icon_, icons::KNOB, icons::Size::M);
    lv_obj_set_style_text_color(cc_in_icon_, COLOR_INACTIVE, 0);

    // CC OUT icon
    cc_out_icon_ = lv_label_create(cell);
    icons::set(cc_out_icon_, icons::KNOB, icons::Size::M);
    lv_obj_set_style_text_color(cc_out_icon_, COLOR_INACTIVE, 0);
}

void TransportBar::createTransportCenter(lv_obj_t* parent) {
    // Cell 1: Play icon (fully centered in cell)
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, 1, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);

    play_icon_ = lv_label_create(cell);
    icons::set(play_icon_, icons::TRANSPORT_PLAY, icons::Size::L);
    lv_obj_set_style_text_color(play_icon_, COLOR_PLAY_INACTIVE, 0);
    lv_obj_center(play_icon_);
}

void TransportBar::createTempoWithBeat(lv_obj_t* parent) {
    // Cell 2: Beat indicator + Tempo (right-aligned, beat left of tempo)
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_END, 2, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    style::apply(cell).flexRow(LV_FLEX_ALIGN_END, theme::layout::GAP_SM).padRight(theme::layout::PAD_MD);

    // Beat indicator (left of tempo)
    beat_indicator_ = std::make_unique<StateIndicator>(cell, theme::layout::INDICATOR_SIZE);
    beat_indicator_->color(StateIndicator::State::OFF, theme::color::INACTIVE)
                  .color(StateIndicator::State::ACTIVE, theme::color::BEAT_PULSE)
                  .opacity(StateIndicator::State::OFF, LV_OPA_COVER)
                  .opacity(StateIndicator::State::ACTIVE, LV_OPA_COVER);
    beat_indicator_->setState(StateIndicator::State::OFF);

    // Tempo label (no BPM unit)
    tempo_label_ = lv_label_create(cell);
    lv_label_set_text(tempo_label_, "120.00");
    lv_obj_set_style_text_font(tempo_label_, fonts.tempo_label, 0);
    lv_obj_set_style_text_color(tempo_label_, COLOR_TEXT, 0);
}

void TransportBar::setupBindings() {
    using oc::state::bind;
    bind(subs_)
        .on(state_.noteInActive, [this](bool active) { setNoteIn(active); })
        .on(state_.noteOutActive, [this](bool active) { setNoteOut(active); })
        .on(state_.ccInActive, [this](bool active) { setCcIn(active); })
        .on(state_.ccOutActive, [this](bool active) { setCcOut(active); })
        .on(state_.playing, [this](bool playing) { setPlaying(playing); })
        .on(state_.tempo, [this](float bpm) { setTempo(bpm); })
        .on(state_.beatPulse, [this](bool pulse) { setBeatPulse(pulse); });
}

void TransportBar::pulseIcon(lv_obj_t* icon, lv_timer_t*& timer, lv_color_t activeColor,
                              uint32_t duration, lv_timer_cb_t callback) {
    lv_obj_set_style_text_color(icon, activeColor, 0);
    if (timer) lv_timer_delete(timer);
    timer = lv_timer_create(callback, duration, this);
    lv_timer_set_repeat_count(timer, 1);
}

void TransportBar::setNoteIn(bool active) {
    if (!active) return;
    pulseIcon(note_in_icon_, note_in_timer_, COLOR_IN_ACTIVE,
              theme::timing::MIDI_BLINK_MS, onNoteInTimeout);
}

void TransportBar::setNoteOut(bool active) {
    if (!active) return;
    pulseIcon(note_out_icon_, note_out_timer_, COLOR_OUT_ACTIVE,
              theme::timing::MIDI_BLINK_MS, onNoteOutTimeout);
}

void TransportBar::setCcIn(bool active) {
    if (!active) return;
    pulseIcon(cc_in_icon_, cc_in_timer_, COLOR_IN_ACTIVE,
              theme::timing::MIDI_BLINK_MS, onCcInTimeout);
}

void TransportBar::setCcOut(bool active) {
    if (!active) return;
    pulseIcon(cc_out_icon_, cc_out_timer_, COLOR_OUT_ACTIVE,
              theme::timing::MIDI_BLINK_MS, onCcOutTimeout);
}

void TransportBar::setPlaying(bool playing) {
    lv_obj_set_style_text_color(play_icon_,
        playing ? COLOR_PLAY_ACTIVE : COLOR_PLAY_INACTIVE, 0);
}

void TransportBar::setTempo(float bpm) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", bpm);
    lv_label_set_text(tempo_label_, buf);
}

void TransportBar::setBeatPulse(bool pulse) {
    if (!pulse) return;
    beat_indicator_->setState(StateIndicator::State::ACTIVE);
    if (beat_timer_) lv_timer_delete(beat_timer_);
    beat_timer_ = lv_timer_create(onBeatTimeout, theme::timing::BEAT_PULSE_MS, this);
    lv_timer_set_repeat_count(beat_timer_, 1);
}

void TransportBar::onNoteInTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    if (!self || !self->note_in_icon_) return;
    lv_obj_set_style_text_color(self->note_in_icon_, COLOR_INACTIVE, 0);
    self->state_.noteInActive.set(false);  // Reset for next pulse
    self->note_in_timer_ = nullptr;
}

void TransportBar::onNoteOutTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    if (!self || !self->note_out_icon_) return;
    lv_obj_set_style_text_color(self->note_out_icon_, COLOR_INACTIVE, 0);
    self->state_.noteOutActive.set(false);
    self->note_out_timer_ = nullptr;
}

void TransportBar::onCcInTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    if (!self || !self->cc_in_icon_) return;
    lv_obj_set_style_text_color(self->cc_in_icon_, COLOR_INACTIVE, 0);
    self->state_.ccInActive.set(false);
    self->cc_in_timer_ = nullptr;
}

void TransportBar::onCcOutTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    if (!self || !self->cc_out_icon_) return;
    lv_obj_set_style_text_color(self->cc_out_icon_, COLOR_INACTIVE, 0);
    self->state_.ccOutActive.set(false);
    self->cc_out_timer_ = nullptr;
}

void TransportBar::onBeatTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    if (!self || !self->beat_indicator_) return;
    self->beat_indicator_->setState(StateIndicator::State::OFF);
    self->state_.beatPulse.set(false);
    self->beat_timer_ = nullptr;
}

void TransportBar::render() {
    setPlaying(state_.playing.get());
    setTempo(state_.tempo.get());
}

void TransportBar::show() {
    if (container_) { lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN); }
}

void TransportBar::hide() {
    if (container_) { lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN); }
}

bool TransportBar::isVisible() const {
    return container_ && !lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace core::ui

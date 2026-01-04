#include "TransportBar.hpp"

#include <cstdio>
#include <oc/state/Bind.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "ui/font/CoreFonts.hpp"
#include "ui/font/Icon.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace ui {

namespace Theme = standalone::theme;
namespace style = oc::ui::lvgl::style;

namespace {
const lv_color_t COLOR_INACTIVE = lv_color_hex(Theme::Color::MIDI_INACTIVE);
const lv_color_t COLOR_IN_ACTIVE = lv_color_hex(Theme::Color::MIDI_IN_ACTIVE);
const lv_color_t COLOR_OUT_ACTIVE = lv_color_hex(Theme::Color::MIDI_OUT_ACTIVE);
const lv_color_t COLOR_PLAY_INACTIVE = lv_color_hex(Theme::Color::PLAY_INACTIVE);
const lv_color_t COLOR_PLAY_ACTIVE = lv_color_hex(Theme::Color::PLAY_ACTIVE);
const lv_color_t COLOR_TEXT = lv_color_hex(Theme::Color::TEXT_SECONDARY);
const lv_color_t COLOR_BEAT = lv_color_hex(Theme::Color::BEAT_PULSE);
}  // namespace

TransportBar::TransportBar(lv_obj_t* parent, state::StatusBarState& state)
    : state_(state) {
    createLayout(parent);
    setupBindings();
    render();
}

TransportBar::~TransportBar() {
    // Cleanup timers first (they reference indicators)
    if (noteInTimer_) lv_timer_delete(noteInTimer_);
    if (noteOutTimer_) lv_timer_delete(noteOutTimer_);
    if (ccInTimer_) lv_timer_delete(ccInTimer_);
    if (ccOutTimer_) lv_timer_delete(ccOutTimer_);
    if (beatTimer_) lv_timer_delete(beatTimer_);

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
    lv_obj_set_size(container_, LV_PCT(100), Theme::Layout::TRANSPORT_BAR_HEIGHT);
    style::apply(container_).bgColor(Theme::Color::BACKGROUND);

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
    style::apply(cell).flexRow(LV_FLEX_ALIGN_START, Theme::Layout::GAP_SM).padLeft(Theme::Layout::PAD_MD);

    // Note IN icon
    noteInIcon_ = lv_label_create(cell);
    Icon::set(noteInIcon_, Icon::NOTE, Icon::Size::M);
    lv_obj_set_style_text_color(noteInIcon_, COLOR_INACTIVE, 0);

    // Note OUT icon
    noteOutIcon_ = lv_label_create(cell);
    Icon::set(noteOutIcon_, Icon::NOTE, Icon::Size::M);
    lv_obj_set_style_text_color(noteOutIcon_, COLOR_INACTIVE, 0);

    // CC IN icon
    ccInIcon_ = lv_label_create(cell);
    Icon::set(ccInIcon_, Icon::KNOB, Icon::Size::M);
    lv_obj_set_style_text_color(ccInIcon_, COLOR_INACTIVE, 0);

    // CC OUT icon
    ccOutIcon_ = lv_label_create(cell);
    Icon::set(ccOutIcon_, Icon::KNOB, Icon::Size::M);
    lv_obj_set_style_text_color(ccOutIcon_, COLOR_INACTIVE, 0);
}

void TransportBar::createTransportCenter(lv_obj_t* parent) {
    // Cell 1: Play icon (fully centered in cell)
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, 1, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);

    playIcon_ = lv_label_create(cell);
    Icon::set(playIcon_, Icon::TRANSPORT_PLAY, Icon::Size::L);
    lv_obj_set_style_text_color(playIcon_, COLOR_PLAY_INACTIVE, 0);
    lv_obj_center(playIcon_);
}

void TransportBar::createTempoWithBeat(lv_obj_t* parent) {
    // Cell 2: Beat indicator + Tempo (right-aligned, beat left of tempo)
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_END, 2, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    style::apply(cell).flexRow(LV_FLEX_ALIGN_END, Theme::Layout::GAP_SM).padRight(Theme::Layout::PAD_MD);

    // Beat indicator (left of tempo)
    beatIndicator_ = std::make_unique<StateIndicator>(cell, Theme::Layout::INDICATOR_SIZE);
    beatIndicator_->color(StateIndicator::State::OFF, Theme::Color::INACTIVE)
                  .color(StateIndicator::State::ACTIVE, Theme::Color::BEAT_PULSE)
                  .opacity(StateIndicator::State::OFF, LV_OPA_COVER)
                  .opacity(StateIndicator::State::ACTIVE, LV_OPA_COVER);
    beatIndicator_->setState(StateIndicator::State::OFF);

    // Tempo label (no BPM unit)
    tempoLabel_ = lv_label_create(cell);
    lv_label_set_text(tempoLabel_, "120.00");
    lv_obj_set_style_text_font(tempoLabel_, fonts.tempo_label, 0);
    lv_obj_set_style_text_color(tempoLabel_, COLOR_TEXT, 0);
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
    pulseIcon(noteInIcon_, noteInTimer_, COLOR_IN_ACTIVE,
              Theme::Timing::MIDI_BLINK_MS, onNoteInTimeout);
}

void TransportBar::setNoteOut(bool active) {
    if (!active) return;
    pulseIcon(noteOutIcon_, noteOutTimer_, COLOR_OUT_ACTIVE,
              Theme::Timing::MIDI_BLINK_MS, onNoteOutTimeout);
}

void TransportBar::setCcIn(bool active) {
    if (!active) return;
    pulseIcon(ccInIcon_, ccInTimer_, COLOR_IN_ACTIVE,
              Theme::Timing::MIDI_BLINK_MS, onCcInTimeout);
}

void TransportBar::setCcOut(bool active) {
    if (!active) return;
    pulseIcon(ccOutIcon_, ccOutTimer_, COLOR_OUT_ACTIVE,
              Theme::Timing::MIDI_BLINK_MS, onCcOutTimeout);
}

void TransportBar::setPlaying(bool playing) {
    lv_obj_set_style_text_color(playIcon_,
        playing ? COLOR_PLAY_ACTIVE : COLOR_PLAY_INACTIVE, 0);
}

void TransportBar::setTempo(float bpm) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", bpm);
    lv_label_set_text(tempoLabel_, buf);
}

void TransportBar::setBeatPulse(bool pulse) {
    if (!pulse) return;
    beatIndicator_->setState(StateIndicator::State::ACTIVE);
    if (beatTimer_) lv_timer_delete(beatTimer_);
    beatTimer_ = lv_timer_create(onBeatTimeout, Theme::Timing::BEAT_PULSE_MS, this);
    lv_timer_set_repeat_count(beatTimer_, 1);
}

void TransportBar::onNoteInTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    lv_obj_set_style_text_color(self->noteInIcon_, COLOR_INACTIVE, 0);
    self->state_.noteInActive.set(false);  // Reset for next pulse
    self->noteInTimer_ = nullptr;
}

void TransportBar::onNoteOutTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    lv_obj_set_style_text_color(self->noteOutIcon_, COLOR_INACTIVE, 0);
    self->state_.noteOutActive.set(false);
    self->noteOutTimer_ = nullptr;
}

void TransportBar::onCcInTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    lv_obj_set_style_text_color(self->ccInIcon_, COLOR_INACTIVE, 0);
    self->state_.ccInActive.set(false);
    self->ccInTimer_ = nullptr;
}

void TransportBar::onCcOutTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    lv_obj_set_style_text_color(self->ccOutIcon_, COLOR_INACTIVE, 0);
    self->state_.ccOutActive.set(false);
    self->ccOutTimer_ = nullptr;
}

void TransportBar::onBeatTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    self->beatIndicator_->setState(StateIndicator::State::OFF);
    self->state_.beatPulse.set(false);
    self->beatTimer_ = nullptr;
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

}  // namespace ui

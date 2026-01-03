#include "TransportBar.hpp"

#include <cstdio>
#include <oc/state/Bind.hpp>

#include "ui/font/CoreFonts.hpp"
#include "ui/font/Icon.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace ui {

namespace Theme = standalone::theme;

namespace {
const lv_color_t COLOR_MIDI_INACTIVE = lv_color_hex(Theme::Color::MIDI_INACTIVE);
const lv_color_t COLOR_MIDI_ACTIVE = lv_color_hex(Theme::Color::MIDI_ACTIVE);
const lv_color_t COLOR_PLAY_INACTIVE = lv_color_hex(Theme::Color::PLAY_INACTIVE);
const lv_color_t COLOR_PLAY_ACTIVE = lv_color_hex(Theme::Color::PLAY_ACTIVE);
const lv_color_t COLOR_TEXT = lv_color_hex(Theme::Color::TEXT_SECONDARY);
}  // namespace

TransportBar::TransportBar(lv_obj_t* parent, state::StatusBarState& state)
    : state_(state) {
    createLayout(parent);
    setupBindings();
    setPlaying(state_.playing.get());
    setTempo(state_.tempo.get());
}

TransportBar::~TransportBar() {
    // Cleanup timers first (they reference indicators)
    if (midiInTimer_) lv_timer_delete(midiInTimer_);
    if (midiOutTimer_) lv_timer_delete(midiOutTimer_);
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
    lv_obj_set_style_bg_color(container_, lv_color_hex(Theme::Color::BACKGROUND), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);

    static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                       LV_GRID_TEMPLATE_LAST};
    static const int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(container_, col_dsc, row_dsc);
    lv_obj_set_layout(container_, LV_LAYOUT_GRID);

    createMidiIndicators(container_);
    createTransportCenter(container_);
    createBeatIndicator(container_);
}

void TransportBar::createMidiIndicators(lv_obj_t* parent) {
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(cell, Theme::Layout::PAD_MD, 0);
    lv_obj_set_style_pad_gap(cell, Theme::Layout::GAP_SM, 0);

    midiInIndicator_ = std::make_unique<StateIndicator>(cell, Theme::Layout::INDICATOR_SIZE);
    midiInIndicator_->color(StateIndicator::State::OFF, Theme::Color::MIDI_INACTIVE)
                    .color(StateIndicator::State::ACTIVE, Theme::Color::MIDI_ACTIVE)
                    .opacity(StateIndicator::State::OFF, LV_OPA_COVER)
                    .opacity(StateIndicator::State::ACTIVE, LV_OPA_COVER);

    midiOutIndicator_ = std::make_unique<StateIndicator>(cell, Theme::Layout::INDICATOR_SIZE);
    midiOutIndicator_->color(StateIndicator::State::OFF, Theme::Color::MIDI_INACTIVE)
                     .color(StateIndicator::State::ACTIVE, Theme::Color::MIDI_ACTIVE)
                     .opacity(StateIndicator::State::OFF, LV_OPA_COVER)
                     .opacity(StateIndicator::State::ACTIVE, LV_OPA_COVER);
}

void TransportBar::createTransportCenter(lv_obj_t* parent) {
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_CENTER, 1, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(cell, Theme::Layout::GAP_MD, 0);

    playIcon_ = lv_label_create(cell);
    Icon::set(playIcon_, Icon::TRANSPORT_PLAY, Icon::Size::L);
    lv_obj_set_style_text_color(playIcon_, COLOR_PLAY_INACTIVE, 0);

    tempoLabel_ = lv_label_create(cell);
    lv_label_set_text(tempoLabel_, "120.00");
    lv_obj_set_style_text_font(tempoLabel_, fonts.inter_14_regular, 0);
    lv_obj_set_style_text_color(tempoLabel_, COLOR_TEXT, 0);
}

void TransportBar::createBeatIndicator(lv_obj_t* parent) {
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_END, 2, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_right(cell, Theme::Layout::PAD_MD, 0);

    beatIndicator_ = std::make_unique<StateIndicator>(cell, Theme::Layout::INDICATOR_SIZE);
    beatIndicator_->color(StateIndicator::State::OFF, Theme::Color::INACTIVE)
                  .color(StateIndicator::State::ACTIVE, Theme::Color::BEAT_PULSE)
                  .opacity(StateIndicator::State::OFF, LV_OPA_40)
                  .opacity(StateIndicator::State::ACTIVE, LV_OPA_COVER);
}

void TransportBar::setupBindings() {
    using oc::state::bind;
    bind(subs_)
        .on(state_.midiInActive, [this](bool active) { setMidiIn(active); })
        .on(state_.midiOutActive, [this](bool active) { setMidiOut(active); })
        .on(state_.playing, [this](bool playing) { setPlaying(playing); })
        .on(state_.tempo, [this](float bpm) { setTempo(bpm); })
        .on(state_.beatPulse, [this](bool pulse) { setBeatPulse(pulse); });
}

void TransportBar::pulseIndicator(StateIndicator* indicator, lv_timer_t*& timer,
                                   uint32_t duration, lv_timer_cb_t callback) {
    indicator->setState(StateIndicator::State::ACTIVE);
    if (timer) lv_timer_delete(timer);
    timer = lv_timer_create(callback, duration, this);
    lv_timer_set_repeat_count(timer, 1);
}

void TransportBar::setMidiIn(bool active) {
    if (!active) return;
    pulseIndicator(midiInIndicator_.get(), midiInTimer_,
                   Theme::Timing::MIDI_BLINK_MS, onMidiInTimeout);
}

void TransportBar::setMidiOut(bool active) {
    if (!active) return;
    pulseIndicator(midiOutIndicator_.get(), midiOutTimer_,
                   Theme::Timing::MIDI_BLINK_MS, onMidiOutTimeout);
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
    pulseIndicator(beatIndicator_.get(), beatTimer_,
                   Theme::Timing::BEAT_PULSE_MS, onBeatTimeout);
}

void TransportBar::onMidiInTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    self->midiInIndicator_->setState(StateIndicator::State::OFF);
    self->midiInTimer_ = nullptr;
}

void TransportBar::onMidiOutTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    self->midiOutIndicator_->setState(StateIndicator::State::OFF);
    self->midiOutTimer_ = nullptr;
}

void TransportBar::onBeatTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    self->beatIndicator_->setState(StateIndicator::State::OFF);
    self->beatTimer_ = nullptr;
}

}  // namespace ui

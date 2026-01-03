#include "TransportBar.hpp"

#include <cmath>
#include <cstdio>

#include "ui/font/CoreFonts.hpp"
#include "ui/font/Icon.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace ui {

namespace Theme = standalone::theme;

namespace {
const lv_color_t COLOR_INACTIVE = lv_color_hex(Theme::Color::MIDI_INACTIVE);
const lv_color_t COLOR_IN_ACTIVE = lv_color_hex(Theme::Color::MIDI_IN_ACTIVE);
const lv_color_t COLOR_OUT_ACTIVE = lv_color_hex(Theme::Color::MIDI_OUT_ACTIVE);
const lv_color_t COLOR_PLAY_INACTIVE = lv_color_hex(Theme::Color::PLAY_INACTIVE);
const lv_color_t COLOR_PLAY_ACTIVE = lv_color_hex(Theme::Color::PLAY_ACTIVE);
const lv_color_t COLOR_TEXT = lv_color_hex(Theme::Color::TEXT_SECONDARY);
}  // namespace

TransportBar::TransportBar(lv_obj_t* parent) {
    createLayout(parent);
}

TransportBar::~TransportBar() {
    // Cleanup timers first (they reference indicators)
    if (noteInTimer_) lv_timer_delete(noteInTimer_);
    if (noteOutTimer_) lv_timer_delete(noteOutTimer_);
    if (ccInTimer_) lv_timer_delete(ccInTimer_);
    if (ccOutTimer_) lv_timer_delete(ccOutTimer_);
    if (beatTimer_) lv_timer_delete(beatTimer_);

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
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(cell, Theme::Layout::PAD_MD, 0);
    lv_obj_set_style_pad_gap(cell, Theme::Layout::GAP_SM, 0);

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
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(cell, Theme::Layout::GAP_SM, 0);
    lv_obj_set_style_pad_right(cell, Theme::Layout::PAD_MD, 0);

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

void TransportBar::render(const TransportBarProps& props) {
    // Handle static state changes
    if (currentProps_.playing != props.playing) {
        setPlaying(props.playing);
    }

    if (std::fabs(currentProps_.tempo - props.tempo) > 0.001f) {
        setTempo(props.tempo);
    }

    // Handle pulse transitions (false → true triggers pulse)
    if (!currentProps_.noteInActive && props.noteInActive) {
        pulseIcon(noteInIcon_, noteInTimer_, COLOR_IN_ACTIVE,
                  Theme::Timing::MIDI_BLINK_MS, onNoteInTimeout);
    }

    if (!currentProps_.noteOutActive && props.noteOutActive) {
        pulseIcon(noteOutIcon_, noteOutTimer_, COLOR_OUT_ACTIVE,
                  Theme::Timing::MIDI_BLINK_MS, onNoteOutTimeout);
    }

    if (!currentProps_.ccInActive && props.ccInActive) {
        pulseIcon(ccInIcon_, ccInTimer_, COLOR_IN_ACTIVE,
                  Theme::Timing::MIDI_BLINK_MS, onCcInTimeout);
    }

    if (!currentProps_.ccOutActive && props.ccOutActive) {
        pulseIcon(ccOutIcon_, ccOutTimer_, COLOR_OUT_ACTIVE,
                  Theme::Timing::MIDI_BLINK_MS, onCcOutTimeout);
    }

    if (!currentProps_.beatPulse && props.beatPulse) {
        beatIndicator_->setState(StateIndicator::State::ACTIVE);
        if (beatTimer_) lv_timer_delete(beatTimer_);
        beatTimer_ = lv_timer_create(onBeatTimeout, Theme::Timing::BEAT_PULSE_MS, this);
        lv_timer_set_repeat_count(beatTimer_, 1);
    }

    // Store props (including callbacks for timer use)
    currentProps_ = props;
}

void TransportBar::pulseIcon(lv_obj_t* icon, lv_timer_t*& timer, lv_color_t activeColor,
                              uint32_t duration, lv_timer_cb_t callback) {
    lv_obj_set_style_text_color(icon, activeColor, 0);
    if (timer) lv_timer_delete(timer);
    timer = lv_timer_create(callback, duration, this);
    lv_timer_set_repeat_count(timer, 1);
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

void TransportBar::onNoteInTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    lv_obj_set_style_text_color(self->noteInIcon_, COLOR_INACTIVE, 0);
    self->noteInTimer_ = nullptr;
    if (self->currentProps_.onNoteInPulseComplete) {
        self->currentProps_.onNoteInPulseComplete();
    }
}

void TransportBar::onNoteOutTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    lv_obj_set_style_text_color(self->noteOutIcon_, COLOR_INACTIVE, 0);
    self->noteOutTimer_ = nullptr;
    if (self->currentProps_.onNoteOutPulseComplete) {
        self->currentProps_.onNoteOutPulseComplete();
    }
}

void TransportBar::onCcInTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    lv_obj_set_style_text_color(self->ccInIcon_, COLOR_INACTIVE, 0);
    self->ccInTimer_ = nullptr;
    if (self->currentProps_.onCcInPulseComplete) {
        self->currentProps_.onCcInPulseComplete();
    }
}

void TransportBar::onCcOutTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    lv_obj_set_style_text_color(self->ccOutIcon_, COLOR_INACTIVE, 0);
    self->ccOutTimer_ = nullptr;
    if (self->currentProps_.onCcOutPulseComplete) {
        self->currentProps_.onCcOutPulseComplete();
    }
}

void TransportBar::onBeatTimeout(lv_timer_t* timer) {
    auto* self = static_cast<TransportBar*>(lv_timer_get_user_data(timer));
    self->beatIndicator_->setState(StateIndicator::State::OFF);
    self->beatTimer_ = nullptr;
    if (self->currentProps_.onBeatPulseComplete) {
        self->currentProps_.onBeatPulseComplete();
    }
}

}  // namespace ui

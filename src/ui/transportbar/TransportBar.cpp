#include "TransportBar.hpp"

#include <algorithm>

#include <oc/state/Bind.hpp>
#include <oc/type/TextFormat.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include <config/PlatformCompat.hpp>
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
const lv_color_t COLOR_OUT_ACTIVE = lv_color_hex(theme::color::BEAT_PULSE);
const lv_color_t COLOR_IO_ACTIVE = lv_color_hex(theme::color::MACRO_4);
const lv_color_t COLOR_PLAY_INACTIVE = lv_color_hex(theme::color::PLAY_INACTIVE);
const lv_color_t COLOR_PLAY_ACTIVE = lv_color_hex(theme::color::PLAY_ACTIVE);
const lv_color_t COLOR_TEMPO_UNLOCKED = lv_color_hex(theme::color::TEXT_SECONDARY);
const lv_color_t COLOR_LOCK = lv_color_hex(theme::color::MIDI_IN_ACTIVE);
constexpr lv_coord_t TRACK_ACTIVITY_SIZE = 7;
constexpr lv_coord_t TRACK_ACTIVITY_GAP = 4;
constexpr lv_opa_t TRACK_ACTIVITY_BASE_OPA = LV_OPA_20;
constexpr lv_opa_t TRACK_ACTIVITY_ACTIVE_BONUS = LV_OPA_20;
constexpr lv_opa_t TRACK_ACTIVITY_PREVIEW_BONUS = LV_OPA_20;
constexpr lv_opa_t TRACK_ACTIVITY_RANGE = LV_OPA_80;

lv_opa_t trackSelectorOpa(uint8_t velocity, bool isActive, bool isPreview) {
    uint16_t opa = TRACK_ACTIVITY_BASE_OPA;
    if (isActive) opa += TRACK_ACTIVITY_ACTIVE_BONUS;
    if (isPreview) opa += TRACK_ACTIVITY_PREVIEW_BONUS;
    opa += static_cast<uint16_t>(velocity) * static_cast<uint16_t>(TRACK_ACTIVITY_RANGE) / 127U;
    return static_cast<lv_opa_t>(std::min<uint16_t>(opa, LV_OPA_COVER));
}
}  // namespace

TransportBar::TransportBar(lv_obj_t* parent,
                           core::state::StatusBarState& state,
                           core::state::sequencer::SequencerTrackBankState& tracks)
    : state_(state)
    , tracks_(tracks) {
    createLayout(parent);
    setupBindings();
    render();
}

TransportBar::~TransportBar() {
    // Clear subscriptions before destroying UI
    subs_.clear();

    // unique_ptr members auto-cleanup, then delete container
    if (container_) {
        lv_obj_delete(container_);
    }
}

FLASHMEM void TransportBar::createLayout(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, LV_PCT(100), theme::layout::TRANSPORT_BAR_HEIGHT);
    style::apply(container_).bgColor(theme::color::BACKGROUND);

    // 3 columns: Tempo | Play | Track note output
    static const int32_t col_dsc[] = {
        LV_GRID_FR(1),  // Tempo
        LV_GRID_FR(1),  // Play (centered)
        LV_GRID_FR(1),  // Track note output
        LV_GRID_TEMPLATE_LAST
    };
    static const int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(container_, col_dsc, row_dsc);
    lv_obj_set_layout(container_, LV_LAYOUT_GRID);

    createTempoWithBeat(container_);
    createTransportCenter(container_);
    createTrackNoteOutputs(container_);
}

FLASHMEM void TransportBar::createTempoWithBeat(lv_obj_t* parent) {
    // Cell 0: Tempo label + fixed indicator zone, flush left.
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    style::apply(cell).flexRow(LV_FLEX_ALIGN_START, theme::layout::GAP_SM).padLeft(theme::layout::PAD_MD);

    // Fixed indicator zone at the left of tempo.
    tempo_indicator_container_ = lv_obj_create(cell);
    lv_obj_remove_style_all(tempo_indicator_container_);
    lv_obj_clear_flag(tempo_indicator_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(tempo_indicator_container_, theme::layout::INDICATOR_SIZE, theme::layout::INDICATOR_SIZE);

    beat_indicator_ = std::make_unique<StateIndicator>(tempo_indicator_container_, theme::layout::INDICATOR_SIZE);
    beat_indicator_->color(StateIndicator::State::OFF, theme::color::INACTIVE)
                  .color(StateIndicator::State::ACTIVE, theme::color::BEAT_PULSE)
                  .opacity(StateIndicator::State::OFF, LV_OPA_COVER)
                  .opacity(StateIndicator::State::ACTIVE, LV_OPA_COVER);
    beat_indicator_->setState(StateIndicator::State::OFF);

    tempo_lock_icon_ = lv_label_create(tempo_indicator_container_);
    icons::set(tempo_lock_icon_, icons::LOCK, icons::Size::S);
    lv_obj_set_style_text_color(tempo_lock_icon_, COLOR_LOCK, 0);
    lv_obj_center(tempo_lock_icon_);
    lv_obj_add_flag(tempo_lock_icon_, LV_OBJ_FLAG_HIDDEN);

    // Tempo label in fixed-width slot to prevent layout jitter.
    tempo_label_ = lv_label_create(cell);
    lv_label_set_text(tempo_label_, "120.00");
    lv_obj_set_style_text_font(tempo_label_, fonts.tempo_label, 0);
    lv_obj_set_width(tempo_label_, 64);
    lv_obj_set_style_text_align(tempo_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(tempo_label_, COLOR_TEMPO_UNLOCKED, 0);

    cc_activity_icon_ = lv_label_create(cell);
    icons::set(cc_activity_icon_, icons::KNOB, icons::Size::M);
    lv_obj_set_style_text_color(cc_activity_icon_, COLOR_INACTIVE, 0);
}

FLASHMEM void TransportBar::createTransportCenter(lv_obj_t* parent) {
    // Cell 1: Play icon (fully centered in cell)
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, 1, 1,
                         LV_GRID_ALIGN_STRETCH, 0, 1);

    play_icon_ = lv_label_create(cell);
    icons::set(play_icon_, icons::TRANSPORT_PLAY, icons::Size::L);
    lv_obj_set_style_text_color(play_icon_, COLOR_PLAY_INACTIVE, 0);
    lv_obj_center(play_icon_);

    transport_lock_icon_ = lv_label_create(cell);
    icons::set(transport_lock_icon_, icons::LOCK, icons::Size::S);
    lv_obj_set_style_text_color(transport_lock_icon_, COLOR_LOCK, 0);
    lv_obj_add_flag(transport_lock_icon_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align_to(transport_lock_icon_, play_icon_, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
}

FLASHMEM void TransportBar::createTrackNoteOutputs(lv_obj_t* parent) {
    // Cell 2: Per-track note output activity, flush right.
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_END, 2, 1,
                         LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    style::apply(cell).flexRow(LV_FLEX_ALIGN_END, TRACK_ACTIVITY_GAP).padRight(theme::layout::PAD_MD);

    for (uint8_t i = 0; i < track_note_items_.size(); ++i) {
        track_note_items_[i] = lv_obj_create(cell);
        lv_obj_remove_style_all(track_note_items_[i]);
        lv_obj_set_size(track_note_items_[i], TRACK_ACTIVITY_SIZE, TRACK_ACTIVITY_SIZE);
        lv_obj_set_style_radius(track_note_items_[i], 1, 0);
        lv_obj_set_style_bg_color(
            track_note_items_[i],
            lv_color_hex(theme::color::trackColor(i)),
            0
        );
        lv_obj_set_style_bg_opa(track_note_items_[i], TRACK_ACTIVITY_BASE_OPA, 0);
    }
}

FLASHMEM void TransportBar::setupBindings() {
    using oc::state::bind;
    bind(subs_)
        .on(state_.ccInActive, [this](bool active) { setCcIn(active); })
        .on(state_.ccOutActive, [this](bool active) { setCcOut(active); })
        .on(state_.playing, [this](bool playing) { setPlaying(playing); })
        .on(state_.tempoDisplay, [this](float bpm) { setTempo(bpm); })
        .on(state_.tempoLocked, [this](bool locked) { setTempoLocked(locked); })
        .on(state_.transportLocked, [this](bool locked) { setTransportLocked(locked); })
        .on(state_.beatPulse, [this](bool pulse) { setBeatPulse(pulse); });

    for (uint8_t i = 0; i < track_note_items_.size(); ++i) {
        subs_.push_back(state_.trackNoteActivity[i].subscribe([this](uint8_t) {
            renderTrackSelectorStrip();
        }));
    }
    subs_.push_back(tracks_.activeTrack.subscribe([this](uint8_t) {
        renderTrackSelectorStrip();
    }));
    subs_.push_back(tracks_.enabledMask.subscribe([this](uint8_t) {
        renderTrackSelectorStrip();
    }));
    subs_.push_back(tracks_.selector.selecting.subscribe([this](bool) {
        renderTrackSelectorStrip();
    }));
    subs_.push_back(tracks_.selector.selectedTrack.subscribe([this](uint8_t) {
        renderTrackSelectorStrip();
    }));
}

FLASHMEM void TransportBar::setPlaying(bool playing) {
    lv_obj_set_style_text_color(play_icon_,
        playing ? COLOR_PLAY_ACTIVE : COLOR_PLAY_INACTIVE, 0);
}

FLASHMEM void TransportBar::setTempo(float bpm) {
    char buf[16];
    oc::type::text::formatFixed1(buf, sizeof(buf), bpm);
    lv_label_set_text(tempo_label_, buf);
}

FLASHMEM void TransportBar::setCcIn(bool active) {
    cc_in_active_ = active;
    updateCcActivityIcon();
}

FLASHMEM void TransportBar::setCcOut(bool active) {
    cc_out_active_ = active;
    updateCcActivityIcon();
}

FLASHMEM void TransportBar::updateCcActivityIcon() {
    if (!cc_activity_icon_) return;

    const lv_color_t color = cc_in_active_ && cc_out_active_
        ? COLOR_IO_ACTIVE
        : (cc_in_active_ ? COLOR_IN_ACTIVE
                         : (cc_out_active_ ? COLOR_OUT_ACTIVE : COLOR_INACTIVE));
    lv_obj_set_style_text_color(cc_activity_icon_, color, 0);
}

FLASHMEM void TransportBar::setTempoLocked(bool locked) {
    if (tempo_lock_icon_) {
        if (locked) {
            lv_obj_clear_flag(tempo_lock_icon_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(tempo_lock_icon_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (tempo_label_) {
        lv_obj_set_style_text_color(tempo_label_, COLOR_TEMPO_UNLOCKED, 0);
    }
}

FLASHMEM void TransportBar::setTransportLocked(bool locked) {
    if (!transport_lock_icon_) return;

    if (locked) {
        lv_obj_clear_flag(transport_lock_icon_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(transport_lock_icon_, LV_OBJ_FLAG_HIDDEN);
    }
}

FLASHMEM void TransportBar::setBeatPulse(bool pulse) {
    if (!beat_indicator_) return;
    beat_indicator_->setState(pulse ? StateIndicator::State::ACTIVE : StateIndicator::State::OFF);
}

FLASHMEM void TransportBar::renderTrackSelectorStrip() {
    const uint8_t activeTrack = tracks_.activeTrack.get();
    const uint8_t previewTrack = tracks_.selector.selecting.get()
        ? tracks_.selector.selectedTrack.get()
        : activeTrack;

    for (uint8_t i = 0; i < track_note_items_.size(); ++i) {
        if (!track_note_items_[i]) continue;

        const bool enabled = tracks_.isTrackEnabled(i);
        lv_obj_set_style_bg_color(
            track_note_items_[i],
            lv_color_hex(enabled ? theme::color::trackColor(i) : theme::color::INACTIVE),
            0
        );
        lv_obj_set_style_bg_opa(
            track_note_items_[i],
            trackSelectorOpa(state_.trackNoteActivity[i].get(), activeTrack == i, previewTrack == i),
            0
        );
    }
}

FLASHMEM void TransportBar::render() {
    setCcIn(state_.ccInActive.get());
    setCcOut(state_.ccOutActive.get());
    setPlaying(state_.playing.get());
    setTempo(state_.tempoDisplay.get());
    setTempoLocked(state_.tempoLocked.get());
    setTransportLocked(state_.transportLocked.get());
    setBeatPulse(state_.beatPulse.get());
    renderTrackSelectorStrip();
}

FLASHMEM void TransportBar::show() {
    if (container_) { lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN); }
}

FLASHMEM void TransportBar::hide() {
    if (container_) { lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN); }
}

FLASHMEM bool TransportBar::isVisible() const {
    return container_ && !lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace core::ui

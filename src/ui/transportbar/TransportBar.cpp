#include "TransportBar.hpp"

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
}  // namespace

FLASHMEM TransportBar::TransportBar(
    lv_obj_t* parent,
    const core::state::StatusBarState& state
)
    : state_(state) {
    createLayout(parent);
    setupBindings();
    render();
}

FLASHMEM TransportBar::~TransportBar() {
    subs_.clear();
    if (container_) {
        lv_obj_delete(container_);
    }
}

FLASHMEM void TransportBar::createLayout(lv_obj_t* parent) {
    container_ = lv_obj_create(parent);
    lv_obj_remove_style_all(container_);
    lv_obj_set_size(container_, LV_PCT(100), theme::layout::TRANSPORT_BAR_HEIGHT);
    style::apply(container_).bgColor(theme::color::BACKGROUND);

    static const int32_t col_dsc[] = {
        LV_GRID_FR(1),
        LV_GRID_FR(1),
        LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST
    };
    static const int32_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(container_, col_dsc, row_dsc);
    lv_obj_set_layout(container_, LV_LAYOUT_GRID);

    createTempoWithBeat(container_);
    createTransportCenter(container_);
}

FLASHMEM void TransportBar::createTempoWithBeat(lv_obj_t* parent) {
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    style::apply(cell).flexRow(LV_FLEX_ALIGN_START, theme::layout::GAP_SM).padLeft(theme::layout::PAD_MD);

    tempo_indicator_container_ = lv_obj_create(cell);
    lv_obj_remove_style_all(tempo_indicator_container_);
    lv_obj_clear_flag(tempo_indicator_container_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(
        tempo_indicator_container_,
        theme::layout::INDICATOR_SIZE,
        theme::layout::INDICATOR_SIZE
    );

    beat_indicator_ = std::make_unique<StateIndicator>(
        tempo_indicator_container_,
        theme::layout::INDICATOR_SIZE
    );
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
    lv_obj_t* cell = lv_obj_create(parent);
    lv_obj_remove_style_all(cell);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

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
}

FLASHMEM void TransportBar::setPlaying(bool playing) {
    lv_obj_set_style_text_color(play_icon_, playing ? COLOR_PLAY_ACTIVE : COLOR_PLAY_INACTIVE, 0);
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
        if (locked) lv_obj_clear_flag(tempo_lock_icon_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(tempo_lock_icon_, LV_OBJ_FLAG_HIDDEN);
    }

    if (tempo_label_) {
        lv_obj_set_style_text_color(tempo_label_, COLOR_TEMPO_UNLOCKED, 0);
    }
}

FLASHMEM void TransportBar::setTransportLocked(bool locked) {
    if (!transport_lock_icon_) return;
    if (locked) lv_obj_clear_flag(transport_lock_icon_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(transport_lock_icon_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM void TransportBar::setBeatPulse(bool pulse) {
    if (!beat_indicator_) return;
    beat_indicator_->setState(pulse ? StateIndicator::State::ACTIVE : StateIndicator::State::OFF);
}

FLASHMEM void TransportBar::render() {
    setCcIn(state_.ccInActive.get());
    setCcOut(state_.ccOutActive.get());
    setPlaying(state_.playing.get());
    setTempo(state_.tempoDisplay.get());
    setTempoLocked(state_.tempoLocked.get());
    setTransportLocked(state_.transportLocked.get());
    setBeatPulse(state_.beatPulse.get());
}

FLASHMEM void TransportBar::show() {
    if (container_) lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM void TransportBar::hide() {
    if (container_) lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

FLASHMEM bool TransportBar::isVisible() const {
    return container_ && !lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace core::ui

#pragma once

#include <memory>
#include <vector>

#include <lvgl.h>
#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/widget/StateIndicator.hpp>

#include "state/StatusBarState.hpp"

namespace ui {

/**
 * @brief Transport bar at bottom of screen
 *
 * Layout (3 columns):
 * - Cell 1 (Left): MIDI indicators (Note IN/OUT, CC IN/OUT)
 * - Cell 2 (Center): Play icon
 * - Cell 3 (Right): Beat indicator + Tempo
 */
class TransportBar {
public:
    TransportBar(lv_obj_t* parent, state::StatusBarState& state);
    ~TransportBar();

    TransportBar(const TransportBar&) = delete;
    TransportBar& operator=(const TransportBar&) = delete;

private:
    using StateIndicator = oc::ui::lvgl::StateIndicator;

    state::StatusBarState& state_;

    lv_obj_t* container_ = nullptr;

    // Cell 1: MIDI indicators (Note + CC)
    lv_obj_t* noteInIcon_ = nullptr;
    lv_obj_t* noteOutIcon_ = nullptr;
    lv_obj_t* ccInIcon_ = nullptr;
    lv_obj_t* ccOutIcon_ = nullptr;

    // Cell 2: Transport
    lv_obj_t* playIcon_ = nullptr;

    // Cell 3: Tempo
    lv_obj_t* tempoLabel_ = nullptr;

    // Cell 4: Beat indicator
    std::unique_ptr<StateIndicator> beatIndicator_;

    // Pulse timers (for auto-reset after blink)
    lv_timer_t* noteInTimer_ = nullptr;
    lv_timer_t* noteOutTimer_ = nullptr;
    lv_timer_t* ccInTimer_ = nullptr;
    lv_timer_t* ccOutTimer_ = nullptr;
    lv_timer_t* beatTimer_ = nullptr;

    std::vector<oc::state::Subscription> subs_;

    void createLayout(lv_obj_t* parent);
    void createMidiIndicators(lv_obj_t* parent);
    void createTransportCenter(lv_obj_t* parent);
    void createTempoWithBeat(lv_obj_t* parent);
    void setupBindings();

    void setNoteIn(bool active);
    void setNoteOut(bool active);
    void setCcIn(bool active);
    void setCcOut(bool active);
    void setPlaying(bool playing);
    void setTempo(float bpm);
    void setBeatPulse(bool pulse);

    // Pulse helper for icon-based indicators
    void pulseIcon(lv_obj_t* icon, lv_timer_t*& timer, lv_color_t activeColor,
                   uint32_t duration, lv_timer_cb_t callback);

    static void onNoteInTimeout(lv_timer_t* timer);
    static void onNoteOutTimeout(lv_timer_t* timer);
    static void onCcInTimeout(lv_timer_t* timer);
    static void onCcOutTimeout(lv_timer_t* timer);
    static void onBeatTimeout(lv_timer_t* timer);
};

}  // namespace ui

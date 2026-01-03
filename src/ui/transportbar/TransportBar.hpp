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
 * - Left: MIDI IN/OUT indicators
 * - Center: Play icon + Tempo
 * - Right: Beat pulse indicator
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

    // Left: MIDI indicators
    std::unique_ptr<StateIndicator> midiInIndicator_;
    std::unique_ptr<StateIndicator> midiOutIndicator_;

    // Center: Transport
    lv_obj_t* playIcon_ = nullptr;
    lv_obj_t* tempoLabel_ = nullptr;

    // Right: Beat indicator
    std::unique_ptr<StateIndicator> beatIndicator_;

    // Pulse timers (for auto-reset after blink)
    lv_timer_t* midiInTimer_ = nullptr;
    lv_timer_t* midiOutTimer_ = nullptr;
    lv_timer_t* beatTimer_ = nullptr;

    std::vector<oc::state::Subscription> subs_;

    void createLayout(lv_obj_t* parent);
    void createMidiIndicators(lv_obj_t* parent);
    void createTransportCenter(lv_obj_t* parent);
    void createBeatIndicator(lv_obj_t* parent);
    void setupBindings();

    void setMidiIn(bool active);
    void setMidiOut(bool active);
    void setPlaying(bool playing);
    void setTempo(float bpm);
    void setBeatPulse(bool pulse);

    // Pulse helper: activate indicator and schedule auto-reset
    void pulseIndicator(StateIndicator* indicator, lv_timer_t*& timer,
                        uint32_t duration, lv_timer_cb_t callback);

    static void onMidiInTimeout(lv_timer_t* timer);
    static void onMidiOutTimeout(lv_timer_t* timer);
    static void onBeatTimeout(lv_timer_t* timer);
};

}  // namespace ui

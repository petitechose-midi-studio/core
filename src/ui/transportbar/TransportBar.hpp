#pragma once

#include <memory>
#include <vector>

#include <lvgl.h>
#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/IComponent.hpp>
#include <oc/ui/lvgl/widget/StateIndicator.hpp>

#include "state/StatusBarState.hpp"

namespace core::ui {

/**
 * @brief Transport bar component at bottom of screen
 *
 * Layout (3 columns):
 * - Cell 1 (Left): MIDI indicators (Note IN/OUT, CC IN/OUT)
 * - Cell 2 (Center): Play icon
 * - Cell 3 (Right): Beat indicator + Tempo
 *
 * Subscribes to StatusBarState signals and auto-updates on changes.
 */
class TransportBar : public oc::ui::lvgl::IComponent {
public:
    TransportBar(lv_obj_t* parent, core::state::StatusBarState& state);
    ~TransportBar() override;

    TransportBar(const TransportBar&) = delete;
    TransportBar& operator=(const TransportBar&) = delete;

    // IComponent interface
    void show() override;
    void hide() override;
    bool isVisible() const override;
    lv_obj_t* getElement() const override { return container_; }

private:
    using StateIndicator = oc::ui::lvgl::StateIndicator;

    core::state::StatusBarState& state_;

    lv_obj_t* container_ = nullptr;

    // Cell 1: MIDI indicators (Note + CC)
    lv_obj_t* note_in_icon_ = nullptr;
    lv_obj_t* note_out_icon_ = nullptr;
    lv_obj_t* cc_in_icon_ = nullptr;
    lv_obj_t* cc_out_icon_ = nullptr;

    // Cell 2: Transport
    lv_obj_t* play_icon_ = nullptr;

    // Cell 3: Tempo
    lv_obj_t* tempo_label_ = nullptr;

    // Cell 4: Beat indicator
    std::unique_ptr<StateIndicator> beat_indicator_;

    // Pulse timers (for auto-reset after blink)
    lv_timer_t* note_in_timer_ = nullptr;
    lv_timer_t* note_out_timer_ = nullptr;
    lv_timer_t* cc_in_timer_ = nullptr;
    lv_timer_t* cc_out_timer_ = nullptr;
    lv_timer_t* beat_timer_ = nullptr;

    std::vector<oc::state::Subscription> subs_;

    void createLayout(lv_obj_t* parent);
    void createMidiIndicators(lv_obj_t* parent);
    void createTransportCenter(lv_obj_t* parent);
    void createTempoWithBeat(lv_obj_t* parent);
    void setupBindings();
    void render();

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

}  // namespace core::ui

#pragma once

/**
 * @file TransportBar.hpp
 * @brief Transport controls bar component
 */

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
 * - Cell 3 (Right): Tempo + lock/pulse indicator
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

    // Cell 1: MIDI indicators (Clock + Note + CC)
    lv_obj_t* clock_mode_icon_ = nullptr;
    lv_obj_t* note_in_icon_ = nullptr;
    lv_obj_t* note_out_icon_ = nullptr;
    lv_obj_t* cc_in_icon_ = nullptr;
    lv_obj_t* cc_out_icon_ = nullptr;

    // Cell 2: Transport
    lv_obj_t* play_icon_ = nullptr;
    lv_obj_t* transport_lock_icon_ = nullptr;

    // Cell 3: Tempo
    lv_obj_t* tempo_indicator_container_ = nullptr;
    lv_obj_t* tempo_lock_icon_ = nullptr;
    lv_obj_t* tempo_label_ = nullptr;

    // Pulse indicator behind tempo lock icon
    std::unique_ptr<StateIndicator> beat_indicator_;

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
    void setSyncSource(bool external);
    void setSyncInputPulse(bool pulse);
    void setTempoLocked(bool locked);
    void setTransportLocked(bool locked);
    void setBeatPulse(bool pulse);
};

}  // namespace core::ui

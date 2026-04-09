#pragma once

/**
 * @file TransportBar.hpp
 * @brief Transport controls bar component
 */

#include <memory>
#include <array>
#include <vector>

#include <lvgl.h>
#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/IComponent.hpp>
#include <oc/ui/lvgl/widget/StateIndicator.hpp>

#include "state/StatusBarState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::ui {

/**
 * @brief Transport bar component at bottom of screen
 *
 * Layout (3 columns):
 * - Cell 1 (Left): Tempo + beat/lock indicator
 * - Cell 2 (Center): Play icon
 * - Cell 3 (Right): Track note output activity
 *
 * Subscribes to StatusBarState signals and auto-updates on changes.
 */
class TransportBar : public oc::ui::lvgl::IComponent {
public:
    TransportBar(lv_obj_t* parent,
                 core::state::StatusBarState& state,
                 core::state::sequencer::SequencerTrackBankState& tracks);
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
    static constexpr uint8_t VISIBLE_TRACK_COUNT = 8;

    core::state::StatusBarState& state_;
    core::state::sequencer::SequencerTrackBankState& tracks_;

    lv_obj_t* container_ = nullptr;

    // Cell 1: Tempo
    lv_obj_t* tempo_indicator_container_ = nullptr;
    lv_obj_t* tempo_lock_icon_ = nullptr;
    lv_obj_t* tempo_label_ = nullptr;
    lv_obj_t* cc_activity_icon_ = nullptr;

    // Cell 2: Transport
    lv_obj_t* play_icon_ = nullptr;
    lv_obj_t* transport_lock_icon_ = nullptr;

    // Cell 3: Track note outputs
    std::array<lv_obj_t*, VISIBLE_TRACK_COUNT> track_note_items_{};

    // Pulse indicator behind tempo lock icon
    std::unique_ptr<StateIndicator> beat_indicator_;

    std::vector<oc::state::Subscription> subs_;
    bool cc_in_active_ = false;
    bool cc_out_active_ = false;

    void createLayout(lv_obj_t* parent);
    void createTempoWithBeat(lv_obj_t* parent);
    void createTransportCenter(lv_obj_t* parent);
    void createTrackNoteOutputs(lv_obj_t* parent);
    void setupBindings();
    void render();

    void setPlaying(bool playing);
    void setTempo(float bpm);
    void setCcIn(bool active);
    void setCcOut(bool active);
    void updateCcActivityIcon();
    void setTempoLocked(bool locked);
    void setTransportLocked(bool locked);
    void setBeatPulse(bool pulse);
    void renderTrackSelectorStrip();
};

}  // namespace core::ui

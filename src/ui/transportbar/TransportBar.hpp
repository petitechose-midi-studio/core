#pragma once

/**
 * @file TransportBar.hpp
 * @brief Transport controls bar component
 */

#include <memory>

#include <lvgl.h>
#include <oc/ui/lvgl/IComponent.hpp>
#include <oc/ui/lvgl/widget/StateIndicator.hpp>
#include <oc/state/FixedSubscriptionList.hpp>

#include "state/StatusBarState.hpp"

namespace core::ui {

class TransportBar : public oc::ui::lvgl::IComponent {
public:
    TransportBar(lv_obj_t* parent, core::state::StatusBarState& state);
    ~TransportBar() override;

    TransportBar(const TransportBar&) = delete;
    TransportBar& operator=(const TransportBar&) = delete;

    void show() override;
    void hide() override;
    bool isVisible() const override;
    lv_obj_t* getElement() const override { return container_; }

private:
    using StateIndicator = oc::ui::lvgl::StateIndicator;

    core::state::StatusBarState& state_;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* tempo_indicator_container_ = nullptr;
    lv_obj_t* tempo_lock_icon_ = nullptr;
    lv_obj_t* tempo_label_ = nullptr;
    lv_obj_t* cc_activity_icon_ = nullptr;
    lv_obj_t* play_icon_ = nullptr;
    lv_obj_t* transport_lock_icon_ = nullptr;

    std::unique_ptr<StateIndicator> beat_indicator_;
    oc::state::FixedSubscriptionList<7> subs_;
    bool cc_in_active_ = false;
    bool cc_out_active_ = false;

    void createLayout(lv_obj_t* parent);
    void createTempoWithBeat(lv_obj_t* parent);
    void createTransportCenter(lv_obj_t* parent);
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
};

}  // namespace core::ui

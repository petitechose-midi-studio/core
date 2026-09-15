#pragma once

/**
 * @file TransportBar.hpp
 * @brief Transport controls bar component
 */

#include <array>

#include <lvgl.h>
#include <oc/ui/lvgl/IComponent.hpp>
#include <oc/state/FixedSubscriptionList.hpp>

#include "state/StatusBarState.hpp"

namespace core::ui {

/** One persistent central footer surface, independent of the active context.
 * Local action strips flank it without publishing another copy of transport
 * state. Updates only invalidate this bounded area; no per-frame text allocation.
 */
class TransportBar : public oc::ui::lvgl::IComponent {
public:
    TransportBar(lv_obj_t* parent, const core::state::StatusBarState& state);
    ~TransportBar() override;

    TransportBar(const TransportBar&) = delete;
    TransportBar& operator=(const TransportBar&) = delete;

    void show() override;
    void hide() override;
    bool isVisible() const override;
    lv_obj_t* getElement() const override { return container_; }

private:
    const core::state::StatusBarState& state_;
    lv_obj_t* container_ = nullptr;
    oc::state::FixedSubscriptionList<7> subs_;
    std::array<char, 12> tempo_text_{};
    float tempo_ = 0;
    uint8_t flags_ = 0;
    bool initialized_ = false;
    void setupBindings();
    void refresh();
    void draw(lv_layer_t* layer) const;
    static void onDraw(lv_event_t* event);
};

}  // namespace core::ui

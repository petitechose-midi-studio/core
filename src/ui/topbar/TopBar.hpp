#pragma once

#include <lvgl.h>
#include <vector>
#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/IComponent.hpp>

#include "state/StatusBarState.hpp"

namespace core::ui {

/**
 * @brief Top bar component displaying current page name (centered)
 *
 * Subscribes to StatusBarState signals and auto-updates on changes.
 */
class TopBar : public oc::ui::lvgl::IComponent {
public:
    TopBar(lv_obj_t* parent, core::state::StatusBarState& state);
    ~TopBar() override;

    TopBar(const TopBar&) = delete;
    TopBar& operator=(const TopBar&) = delete;

    // IComponent interface
    void show() override;
    void hide() override;
    bool isVisible() const override;
    lv_obj_t* getElement() const override { return container_; }

private:
    core::state::StatusBarState& state_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* label_ = nullptr;
    std::vector<oc::state::Subscription> subs_;

    void createLayout(lv_obj_t* parent);
    void setupBindings();
    void render();
};

}  // namespace core::ui

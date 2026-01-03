#pragma once

#include <lvgl.h>
#include <vector>
#include <oc/state/Signal.hpp>

#include "state/StatusBarState.hpp"

namespace ui {

/**
 * @brief Top bar displaying current page name (centered)
 */
class TopBar {
public:
    TopBar(lv_obj_t* parent, state::StatusBarState& state);
    ~TopBar();

    TopBar(const TopBar&) = delete;
    TopBar& operator=(const TopBar&) = delete;

private:
    state::StatusBarState& state_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* label_ = nullptr;
    std::vector<oc::state::Subscription> subs_;

    void createLayout(lv_obj_t* parent);
    void setupBindings();
};

}  // namespace ui

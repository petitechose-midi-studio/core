#pragma once

/**
 * @file SequencerView.hpp
 * @brief Placeholder sequencer view (UI-first)
 */

#include <array>
#include <memory>
#include <vector>

#include <lvgl.h>

#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include "state/CoreState.hpp"
#include "ui/topbar/TopBar.hpp"

namespace core::ui {

class SequencerView : public oc::ui::lvgl::IView {
public:
    explicit SequencerView(lv_obj_t* parent, core::state::CoreState& coreState);
    ~SequencerView() override;

    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.sequencer"; }
    lv_obj_t* getElement() const override { return container_; }

private:
    void createLayout(lv_obj_t* parent);
    void createTopBar();
    void createSteps();
    void bindToState();
    void render();

    core::state::CoreState& core_state_;
    std::vector<oc::state::Subscription> subscriptions_;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* top_bar_container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    lv_obj_t* header_container_ = nullptr;
    lv_obj_t* page_label_ = nullptr;

    std::unique_ptr<core::ui::TopBar> top_bar_;
    lv_obj_t* grid_ = nullptr;
    std::array<lv_obj_t*, 8> steps_{};
    std::array<lv_obj_t*, 8> step_labels_{};
};

}  // namespace core::ui

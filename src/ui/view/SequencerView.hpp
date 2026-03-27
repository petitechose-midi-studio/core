#pragma once

/**
 * @file SequencerView.hpp
 * @brief Placeholder sequencer view (UI-first)
 */

#include <array>
#include <memory>

#include <lvgl.h>

#include <oc/state/SignalWatcher.hpp>
#include <oc/ui/lvgl/IView.hpp>

#include <ms/ui/component/LayoutView.hpp>

#include "state/CoreState.hpp"
#include "ui/sequencer/SequencerHeaderBar.hpp"
#include "ui/sequencer/StepGrid.hpp"
#include "ui/sequencer/StepPropertyStrip.hpp"

namespace core::ui {

class SequencerView : public oc::ui::lvgl::IView {
public:
    explicit SequencerView(lv_obj_t* parent, core::state::CoreState& coreState);
    ~SequencerView() override;

    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.sequencer"; }
    lv_obj_t* getElement() const override { return container_; }
    lv_obj_t* getPropertySelectorScopeElement() const;
    void setPropertySelectorScopeVisible(bool visible);

private:
    void createLayout(lv_obj_t* parent);
    void createHeaderBar();
    void createGrid();
    void bindToState();

    void requestRender();
    void requestHeaderRender();
    void requestStripRender();
    void requestGridRender();
    static void onRenderTimer(lv_timer_t* timer);
    void renderHeader(
        uint8_t len,
        uint8_t page,
        uint8_t focused,
        uint64_t enabledMask,
        int16_t playhead,
        core::state::sequencer::StepProperty property
    );
    void render();

    core::state::CoreState& core_state_;
    oc::state::SignalWatcher watcher_;

    bool dirty_ = false;
    bool header_dirty_ = true;
    bool strip_dirty_ = true;
    bool grid_dirty_ = true;
    lv_timer_t* render_timer_ = nullptr;

    std::unique_ptr<ms::ui::LayoutView> layout_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;

    std::unique_ptr<core::ui::SequencerHeaderBar> header_bar_;
    std::unique_ptr<core::ui::StepPropertyStrip> property_strip_;
    std::unique_ptr<core::ui::StepGrid> step_grid_;
};

}  // namespace core::ui

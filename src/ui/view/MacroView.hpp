#pragma once

/**
 * @file MacroView.hpp
 * @brief 8-macro parameter view for standalone mode
 *
 * Displays 8 MacroWidgets in a 4x2 grid layout with integrated TopBar.
 * Each widget shows channel + CC labels below.
 * Subscribes to MacroState for reactive updates.
 *
 * Pattern: View owns its TopBar internally (like RemoteControlsView in plugin-bitwig).
 */

#include <array>
#include <cassert>
#include <memory>
#include <vector>

#include <lvgl.h>

#include <oc/ui/lvgl/IView.hpp>

#include <ms/ui/component/LayoutView.hpp>

#include "state/CoreState.hpp"
#include "ui/topbar/TopBar.hpp"
#include "ui/widget/IMacroWidget.hpp"
#include "ui/widget/MacroKnobWidget.hpp"

namespace core::ui {

class MacroView : public oc::ui::lvgl::IView {
public:
    static constexpr uint8_t MACRO_COUNT = Config::MACRO_COUNT;
    static constexpr uint8_t COLS = 4;
    static constexpr uint8_t ROWS = 2;

    MacroView(lv_obj_t* parent, core::state::CoreState& coreState);
    ~MacroView() override;

    // IView interface
    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.macro"; }
    lv_obj_t* getElement() const override { return container_; }

    // Widget access
    core::ui::IMacroWidget& macro(uint8_t index) {
        assert(index < MACRO_COUNT);
        if (index >= MACRO_COUNT) index = 0;
        return *macros_[index];
    }
    const core::ui::IMacroWidget& macro(uint8_t index) const {
        assert(index < MACRO_COUNT);
        if (index >= MACRO_COUNT) index = 0;
        return *macros_[index];
    }

private:
    void createLayout(lv_obj_t* parent);
    void createTopBar();
    void createMacros();
    void bindToState();

    // Debounced update system
    void scheduleUpdate();
    void pauseUpdateIfIdle();
    void requestTopBarRender();
    void markAllDirty();
    void markAllConfigDirty();
    void markDirty(uint8_t index);
    void processDirtyFlags();
    static void onUpdateTimer(lv_timer_t* timer);

    core::state::CoreState& core_state_;
    std::vector<oc::state::Subscription> subscriptions_;
    std::array<bool, MACRO_COUNT> dirty_flags_{};
    std::array<bool, MACRO_COUNT> config_dirty_flags_{};
    bool has_dirty_ = false;
    bool top_bar_dirty_ = true;
    lv_timer_t* update_timer_ = nullptr;

    // UI structure: container_ (flex col) → top_bar_container_ + body_container_ (grid)
    std::unique_ptr<ms::ui::LayoutView> layout_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* top_bar_container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    std::unique_ptr<core::ui::TopBar> top_bar_;
    std::array<std::unique_ptr<core::ui::IMacroWidget>, MACRO_COUNT> macros_;
};
}  // namespace core::ui

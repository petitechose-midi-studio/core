#pragma once

/**
 * @file MacroView.hpp
 * @brief 8-macro parameter view for standalone mode
 *
 * Displays 8 MacroWidgets in a 4x2 grid layout.
 * Each widget shows channel + CC labels below.
 * Subscribes to MacroState for reactive updates.
 */

#include <array>
#include <memory>
#include <vector>

#include <lvgl.h>

#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/IView.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

#include "config/InputIDs.hpp"
#include "state/CoreState.hpp"
#include "ui/widget/IMacroWidget.hpp"
#include "ui/widget/MacroKnobWidget.hpp"

class MacroView : public oc::ui::lvgl::IView {
public:
    static constexpr uint8_t MACRO_COUNT = Config::MACRO_COUNT;
    static constexpr uint8_t COLS = 4;
    static constexpr uint8_t ROWS = 2;

    MacroView(lv_obj_t* parent, state::CoreState& coreState);
    ~MacroView() override;

    // IView interface
    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.macro"; }
    lv_obj_t* getElement() const override { return container_; }

    // Widget access
    ui::IMacroWidget& macro(uint8_t index) { return *macros_[index]; }
    const ui::IMacroWidget& macro(uint8_t index) const { return *macros_[index]; }

private:
    void createLayout(lv_obj_t* parent);
    void createMacros();
    void bindToState();
    void updateConfigLabel(uint8_t index);

    state::CoreState& coreState_;
    std::vector<oc::state::Subscription> subscriptions_;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* grid_ = nullptr;
    std::array<std::unique_ptr<ui::IMacroWidget>, MACRO_COUNT> macros_;
};

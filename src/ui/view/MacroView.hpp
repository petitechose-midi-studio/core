#pragma once

/**
 * @file MacroView.hpp
 * @brief 8-macro parameter view for standalone mode
 *
 * Displays 8 ParameterKnobs in a 4x2 grid layout.
 * Each knob has its own macro color from BaseTheme.
 * Subscribes to MacroState for reactive updates.
 */

#include <array>
#include <memory>
#include <vector>

#include <lvgl.h>

#include <oc/state/Signal.hpp>
#include <oc/ui/lvgl/IView.hpp>
#include <oc/ui/lvgl/component/ParameterKnob.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

#include "state/MacroState.hpp"
#include "ui/font/FontLoader.hpp"

namespace Theme = oc::ui::lvgl::BaseTheme;

class MacroView : public oc::ui::lvgl::IView {
public:
    static constexpr uint8_t MACRO_COUNT = 8;
    static constexpr uint8_t COLS = 4;
    static constexpr uint8_t ROWS = 2;

    MacroView(lv_obj_t* parent, state::MacroState& state);
    ~MacroView() override;

    // IView interface
    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.macro"; }
    lv_obj_t* getElement() const override { return container_; }

    // Widget access
    oc::ui::lvgl::ParameterKnob& macro(uint8_t index) { return *macros_[index]; }
    const oc::ui::lvgl::ParameterKnob& macro(uint8_t index) const { return *macros_[index]; }

private:
    void createLayout(lv_obj_t* parent);
    void createMacros();
    void configureMacro(uint8_t index);
    void bindToState();

    state::MacroState& state_;
    std::vector<oc::state::Subscription> subscriptions_;

    lv_obj_t* container_ = nullptr;
    lv_obj_t* grid_ = nullptr;
    std::array<std::unique_ptr<oc::ui::lvgl::ParameterKnob>, MACRO_COUNT> macros_;
};

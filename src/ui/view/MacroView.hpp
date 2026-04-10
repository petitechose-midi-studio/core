#pragma once

/**
 * @file MacroView.hpp
 * @brief 8-macro performance view for standalone mode
 *
 * Displays 8 macro widgets in a 4x2 grid framed by the standalone main-view layout:
 * header, reserved action strips, property strip and bottom controls.
 * Subscribes to macro/page/UI state for reactive updates.
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

#include <lvgl.h>

#include <oc/ui/lvgl/IView.hpp>

#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "ui/common/TrackNavigationStrip.hpp"
#include "ui/macro/MacroBottomControls.hpp"
#include "ui/macro/MacroHeaderBar.hpp"
#include "ui/macro/MacroPropertyStrip.hpp"
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/view/MainViewFrame.hpp"
#include "ui/view/MacroViewModelBuilder.hpp"
#include "ui/view/PausableLvglTimer.hpp"
#include "ui/widget/IMacroWidget.hpp"

namespace core::ui {

class MacroView : public oc::ui::lvgl::IView {
public:
    static constexpr uint8_t MACRO_COUNT = Config::MACRO_COUNT;
    static constexpr uint8_t COLS = 4;
    static constexpr uint8_t ROWS = 2;

    struct StateRefs {
        core::state::MacroState& macros;
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& structureNavigationFocus;
        core::state::StructureClipboardState& structureClipboard;
        oc::state::Signal<uint32_t>& configRevision;
        core::state::StatusBarState& statusBar;
    };

    MacroView(lv_obj_t* parent, StateRefs stateRefs);
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
    void createHeaderBar();
    void createBottomControls();
    void createActionStrips();
    void createPropertyStrip();
    void createMacros();
    void bindToState();

    // Debounced update system
    void scheduleUpdate();
    void pauseUpdateIfIdle();
    void requestHeaderRender();
    void requestTrackStripRender();
    void requestLeftActionStripRender();
    void requestBottomActionStripRender();
    void requestPropertyStripRender();
    void markAllDirty();
    void markAllConfigDirty();
    void markDirty(uint8_t index);
    void processDirtyFlags();
    static void onUpdateTimer(lv_timer_t* timer);
    MacroViewModelSource modelSource() const;

    StateRefs state_refs_;
    std::vector<oc::state::Subscription> subscriptions_;
    std::array<bool, MACRO_COUNT> dirty_flags_{};
    std::array<bool, MACRO_COUNT> config_dirty_flags_{};
    bool has_dirty_ = false;
    bool header_dirty_ = true;
    bool track_strip_dirty_ = true;
    bool left_action_strip_dirty_ = true;
    bool bottom_action_strip_dirty_ = true;
    bool property_strip_dirty_ = true;
    std::unique_ptr<PausableLvglTimer> update_timer_;

    // UI structure: frame_ owns the shared standalone layout skeleton.
    std::unique_ptr<core::ui::MainViewFrame> frame_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* top_bar_container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    lv_obj_t* interaction_container_ = nullptr;
    lv_obj_t* center_column_ = nullptr;
    lv_obj_t* macro_grid_container_ = nullptr;
    lv_obj_t* structure_row_container_ = nullptr;
    std::unique_ptr<core::ui::MacroHeaderBar> header_bar_;
    std::unique_ptr<core::ui::MacroBottomControls> bottom_controls_;
    std::unique_ptr<core::ui::TrackNavigationStrip> track_strip_;
    std::unique_ptr<core::ui::ContextActionStrip> left_action_strip_;
    std::unique_ptr<core::ui::ContextActionStrip> bottom_action_strip_;
    std::unique_ptr<core::ui::MacroPropertyStrip> property_strip_;
    std::array<std::unique_ptr<core::ui::IMacroWidget>, MACRO_COUNT> macros_;
};
}  // namespace core::ui

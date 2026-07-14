#pragma once

/**
 * @file MacroView.hpp
 * @brief 8-macro performance view for standalone mode
 *
 * Displays 8 macro widgets in a 4x2 grid framed by the standalone main-view layout.
 * Subscribes to macro/page/UI state for reactive updates.
 */

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IView.hpp>
#include <oc/state/FixedSubscriptionList.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/DataManagerState.hpp"
#include "state/DeviceSettingsState.hpp"
#include "state/MacroEditState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/ViewSelectorState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "ui/macro/MacroHeaderBar.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
#include "ui/sequencer/StepPropertySelectionOverlay.hpp"
#include "ui/strip/ContextActionStrip.hpp"
#include "ui/view/MainViewFrame.hpp"
#include "ui/view/MacroViewModelBuilder.hpp"
#include "ui/widget/MacroKnobWidget.hpp"

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
        core::state::TrackNavigationState& trackNavigation;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& structureNavigationFocus;
        oc::state::Signal<uint8_t, 8>& sharedTrackActive;
        oc::state::Signal<uint16_t, 16>& sharedTrackEnabledMask;
        core::state::StructureClipboardState& structureClipboard;
        oc::state::Signal<uint32_t>& configRevision;
        core::state::StatusBarState& statusBar;
        core::state::MacroEditState& macroEdit;
        core::state::ViewSelectorState& viewSelector;
        core::state::DeviceSettingsState& deviceSettings;
        core::state::DataManagerState& dataManager;
    };

    MacroView(lv_obj_t* parent, StateRefs stateRefs);
    ~MacroView() override;

    // IView interface
    void onActivate() override;
    void onDeactivate() override;
    const char* getViewId() const override { return "core.macro"; }
    lv_obj_t* getElement() const override { return container_; }
    bool valid() const { return initialized_; }

private:
    enum RenderFlag : uint32_t {
        RENDER_HEADER = 1U << 0,
        RENDER_LEFT_ACTION_STRIP = 1U << 1,
        RENDER_BOTTOM_ACTION_STRIP = 1U << 2,
        RENDER_SLOT_PROPERTY_OVERLAY = 1U << 3,
    };
    static constexpr uint8_t VALUE_FLAG_SHIFT = 4;
    static constexpr uint8_t CONFIG_FLAG_SHIFT = VALUE_FLAG_SHIFT + MACRO_COUNT;
    static constexpr uint32_t RENDER_VALUE_MASK =
        ((1U << MACRO_COUNT) - 1U) << VALUE_FLAG_SHIFT;
    static constexpr uint32_t RENDER_CONFIG_MASK =
        ((1U << MACRO_COUNT) - 1U) << CONFIG_FLAG_SHIFT;
    static constexpr uint32_t RENDER_ALL =
        RENDER_HEADER | RENDER_LEFT_ACTION_STRIP | RENDER_BOTTOM_ACTION_STRIP |
        RENDER_SLOT_PROPERTY_OVERLAY | RENDER_VALUE_MASK | RENDER_CONFIG_MASK;

    static constexpr uint32_t valueRenderFlag(uint8_t index) {
        return 1U << (VALUE_FLAG_SHIFT + index);
    }
    static constexpr uint32_t configRenderFlag(uint8_t index) {
        return 1U << (CONFIG_FLAG_SHIFT + index);
    }

    void createLayout(lv_obj_t* parent);
    void createHeaderBar();
    void createActionStrips();
    void createSlotPropertyOverlay();
    void createMacros();
    bool bindToState();
    bool hasBlockingOverlay() const;
    void handleOverlayVisibilityChanged();

    void requestRender(uint32_t flags, bool ready = false);
    void requestHeaderRender();
    void requestLeftActionStripRender();
    void requestBottomActionStripRender();
    void requestSlotPropertyOverlayRender();
    void markAllDirty();
    void markAllConfigDirty();
    void markAutomationRecordingDirtyIfChanged();
    void markConfigDirtyIfChanged();
    void markDirty(uint8_t index);
    static bool canDrainRender(void* context);
    static void drainRender(void* context, uint32_t flags);
    void processRenderFlags(uint32_t flags);
    MacroViewModelSource modelSource() const;

    StateRefs state_refs_;
    // Fixed PSRAM-backed headroom avoids heap traffic without coupling boot
    // correctness to a manually maintained exact subscription count.
    static constexpr size_t SUBSCRIPTION_CAPACITY = 64;
    oc::state::CheckedSubscriptionList<SUBSCRIPTION_CAPACITY> subscriptions_;
    std::array<bool, MACRO_COUNT> rendered_automation_active_{};
    std::array<bool, MACRO_COUNT> rendered_automation_recording_{};
    std::array<bool, MACRO_COUNT> rendered_automation_manual_override_{};
    std::array<uint8_t, MACRO_COUNT> rendered_source_state_{};
    std::array<bool, MACRO_COUNT> rendered_active_{};
    std::array<bool, MACRO_COUNT> rendered_add_slot_{};
    std::array<bool, MACRO_COUNT> rendered_focused_{};
    std::array<uint8_t, MACRO_COUNT> rendered_ccs_{};
    core::app::ExtmemUniquePtr<core::ui::CoalescedLvglRenderScheduler>
        render_scheduler_;

    // UI structure: frame_ owns the shared standalone layout skeleton.
    core::app::ExtmemUniquePtr<core::ui::MainViewFrame> frame_;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* top_bar_container_ = nullptr;
    lv_obj_t* body_container_ = nullptr;
    lv_obj_t* interaction_container_ = nullptr;
    lv_obj_t* center_column_ = nullptr;
    lv_obj_t* macro_grid_container_ = nullptr;
    core::app::ExtmemUniquePtr<core::ui::MacroHeaderBar> header_bar_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip> left_action_strip_;
    core::app::ExtmemUniquePtr<core::ui::ContextActionStrip> bottom_action_strip_;
    core::app::ExtmemUniquePtr<core::ui::StepPropertySelectionOverlay>
        slot_property_overlay_;
    std::array<core::app::ExtmemUniquePtr<core::ui::MacroKnobWidget>, MACRO_COUNT>
        macros_;
    bool initialized_ = false;
};
}  // namespace core::ui

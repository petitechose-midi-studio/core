#pragma once

#include "app/ExtmemAllocator.hpp"
#include <lvgl.h>

#include <oc/state/StaticSignalWatcher.hpp>
#include <oc/type/Ids.hpp>
#include <oc/ui/lvgl/RetainedSurfaceParkingLot.hpp>

#include "ui/common/StructureSelectionInvalidation.hpp"
#include "ui/common/TrackNavigationStrip.hpp"

namespace core::state {
struct CoreState;
}

namespace ms::ui {
class ViewContainer;
}  // namespace ms::ui

namespace core::ui {
class ContextSoftkeyBar;
class DeviceSettingsView;
class MacroView;
class CoalescedLvglRenderScheduler;
class ProjectView;
class SequencerView;
class TransportBar;
}  // namespace core::ui

namespace core::context::standalone {

/**
 * Owns the standalone LVGL view tree.
 *
 * This assembly creates the root view container, macro/sequencer views, global
 * track strip, transport bar, and context softkey bar. Feature handlers and
 * realtime sequencer runtime are wired outside this class.
 */
class StandaloneUiAssembly {
public:
    explicit StandaloneUiAssembly(core::state::CoreState& state);
    ~StandaloneUiAssembly();

    StandaloneUiAssembly(const StandaloneUiAssembly&) = delete;
    StandaloneUiAssembly& operator=(const StandaloneUiAssembly&) = delete;

    bool initialize();
    bool valid() const { return initialized_; }
    void show();
    lv_obj_t* mainZone() const;
    lv_obj_t* overlayRoot() const;
    oc::type::ScopeID macroViewScope() const;
    oc::type::ScopeID sequencerViewScope() const;
    oc::type::ScopeID projectViewScope() const;
    oc::type::ScopeID deviceSettingsViewScope() const;
    lv_obj_t* macroViewElement() const;
    lv_obj_t* sequencerViewElement() const;
    lv_obj_t* projectViewElement() const;
    lv_obj_t* deviceSettingsViewElement() const;
    core::ui::TransportBar& transportBar() const;
    core::ui::ContextSoftkeyBar& contextSoftkeyBar() const;
    void activateMacroView() const;
    void deactivateMacroView() const;
    void activateSequencerView() const;
    void deactivateSequencerView() const;
    void activateProjectView() const;
    void deactivateProjectView() const;
    void activateDeviceSettingsView() const;
    void deactivateDeviceSettingsView() const;

private:
    bool createViewContainer();
    bool createGlobalTrackStrip();
    bool createViews();
    bool createBottomBar();
    void cacheViewScopes();
    bool bindGlobalTrackStrip();
    void applyOverlayExclusivity();
    void scheduleGlobalTrackStripRender(bool ready = false);
    void renderGlobalTrackStrip();
    void requestGlobalTrackStripRender();
    void requestGlobalTrackStripRenderReady();
    static void drainGlobalTrackStripRender(void* context, uint32_t flags);

    core::state::CoreState& core_state_;
    oc::type::ScopeID macro_view_scope_ = 0;
    oc::type::ScopeID sequencer_view_scope_ = 0;
    oc::type::ScopeID project_view_scope_ = 0;
    oc::type::ScopeID device_settings_view_scope_ = 0;
    oc::state::StaticWatchGroup<2> global_track_context_watcher_;
    oc::state::StaticWatchGroup<
        5U + core::ui::STRUCTURE_SELECTION_INVALIDATION_SIGNAL_COUNT>
        global_track_structure_watcher_;
    oc::state::StaticWatchGroup<8> global_track_activity_low_watcher_;
    oc::state::StaticWatchGroup<8> global_track_activity_high_watcher_;
    oc::state::StaticWatchGroup<1> overlay_visibility_watcher_;
    bool overlay_exclusive_mode_ = false;
    core::app::ExtmemUniquePtr<ms::ui::ViewContainer> view_container_;
    oc::ui::lvgl::RetainedSurfaceParkingLot retained_view_parking_{};
    lv_obj_t* macro_view_parking_host_ = nullptr;
    lv_obj_t* sequencer_view_parking_host_ = nullptr;
    lv_obj_t* project_view_parking_host_ = nullptr;
    lv_obj_t* device_settings_view_parking_host_ = nullptr;
    lv_obj_t* views_host_ = nullptr;
    lv_obj_t* full_view_host_ = nullptr;
    lv_obj_t* overlay_curtain_ = nullptr;
    lv_obj_t* global_track_strip_container_ = nullptr;
    core::app::ExtmemUniquePtr<core::ui::TrackNavigationStrip> global_track_strip_;
    core::app::ExtmemUniquePtr<core::ui::CoalescedLvglRenderScheduler>
        global_track_strip_scheduler_;
    core::ui::TrackNavigationStripProps global_track_strip_props_cache_{};
    bool global_track_strip_props_initialized_ = false;
    core::app::ExtmemUniquePtr<core::ui::MacroView> macro_view_;
    core::app::ExtmemUniquePtr<core::ui::SequencerView> sequencer_view_;
    core::app::ExtmemUniquePtr<core::ui::ProjectView> project_view_;
    core::app::ExtmemUniquePtr<core::ui::DeviceSettingsView> device_settings_view_;
    core::app::ExtmemUniquePtr<core::ui::TransportBar> transport_bar_;
    core::app::ExtmemUniquePtr<core::ui::ContextSoftkeyBar> context_softkey_bar_;
    bool initialized_ = false;
};

}  // namespace core::context::standalone

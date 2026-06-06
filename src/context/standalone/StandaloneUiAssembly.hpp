#pragma once

#include <memory>
#include "app/ExtmemAllocator.hpp"
#include <lvgl.h>

#include <oc/state/SignalWatcher.hpp>
#include <oc/type/Ids.hpp>

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
class PausableLvglTimer;
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
    void createViewContainer();
    void createGlobalTrackStrip();
    void createViews();
    void createBottomBar();
    void cacheViewScopes();
    void bindGlobalTrackStrip();
    void applyOverlayExclusivity();
    void scheduleGlobalTrackStripRender(bool ready = false);
    void renderGlobalTrackStrip();
    static void onGlobalTrackStripTimer(lv_timer_t* timer);

    core::state::CoreState& core_state_;
    oc::type::ScopeID macro_view_scope_ = 0;
    oc::type::ScopeID sequencer_view_scope_ = 0;
    oc::type::ScopeID project_view_scope_ = 0;
    oc::type::ScopeID device_settings_view_scope_ = 0;
    oc::state::SignalWatcher global_track_strip_watcher_;
    bool overlay_exclusive_mode_ = false;
    core::app::ExtmemUniquePtr<ms::ui::ViewContainer> view_container_;
    lv_obj_t* views_host_ = nullptr;
    lv_obj_t* global_track_strip_container_ = nullptr;
    std::unique_ptr<core::ui::TrackNavigationStrip> global_track_strip_;
    std::unique_ptr<core::ui::PausableLvglTimer> global_track_strip_timer_;
    core::ui::TrackNavigationStripProps global_track_strip_props_cache_{};
    bool global_track_strip_props_initialized_ = false;
    bool global_track_strip_dirty_ = true;
    core::app::ExtmemUniquePtr<core::ui::MacroView> macro_view_;
    core::app::ExtmemUniquePtr<core::ui::SequencerView> sequencer_view_;
    core::app::ExtmemUniquePtr<core::ui::ProjectView> project_view_;
    core::app::ExtmemUniquePtr<core::ui::DeviceSettingsView> device_settings_view_;
    core::app::ExtmemUniquePtr<core::ui::TransportBar> transport_bar_;
    core::app::ExtmemUniquePtr<core::ui::ContextSoftkeyBar> context_softkey_bar_;
};

}  // namespace core::context::standalone

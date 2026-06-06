#pragma once

#include <memory>

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "handler/settings/DataManagerDomainServices.hpp"
#include "handler/settings/DeviceSettingsDomainServices.hpp"
#include "handler/settings/DataManagerHandler.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/settings/SequencerSettingsDomainServices.hpp"
#include "state/CoreSettings.hpp"
#include "state/DataManagerState.hpp"
#include "state/DeviceSettingsState.hpp"
#include "state/SequencerSettingsState.hpp"
#include "state/ViewSelectorState.hpp"
#include "state/MidiSyncState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

#if defined(MS_UX_RECORDER)
#include "context/standalone/ux/StandaloneUxSurfaces.hpp"
#endif

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}

namespace core::ui {
class ContextSoftkeyBar;
class TransportBar;
}

namespace core::context::standalone {
class DataManagerPresenter;
class DeviceSettingsSelectorPresenter;
class SequencerSettingsOverlayPresenter;
}  // namespace core::context::standalone

namespace core::handler {
class DataManagerHandler;
class DeviceSettingsHandler;
class SequencerSettingsHandler;
}  // namespace core::handler

namespace core::context::standalone {

/**
 * Owns settings and Data Manager overlays, presenters, and modal handlers.
 *
 * Applying setting changes and persistence actions is delegated to the domain
 * services supplied at construction.
 */
class SettingsFeatureModule {
public:
    struct StateRefs {
        core::state::DeviceSettingsState& deviceSettings;
        core::state::SequencerSettingsState& sequencerSettings;
        core::state::ViewSelectorState& viewSelector;
        core::state::MidiSyncState& midiSync;
        core::state::CoreSettings& settings;
        core::state::DataManagerState& dataManager;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& sequencerTracks;
        core::handler::SequencerHistoryDomainServices history;
    };

    SettingsFeatureModule(StateRefs stateRefs,
                          core::handler::DeviceSettingsDomainServices deviceSettingsServices,
                          core::handler::SequencerSettingsDomainServices sequencerSettingsServices,
                          core::handler::DataManagerDomainServices dataManagerServices,
                          oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                          oc::api::EncoderAPI& encoders,
                          oc::api::ButtonAPI& buttons,
                          lv_obj_t* mainZone,
                          core::ui::ContextSoftkeyBar& softkeyBar,
                          core::ui::TransportBar& transportBar,
                          oc::type::ScopeID deviceSettingsViewScope,
                          core::handler::DataManagerHandler::ViewScopes viewScopes
#if defined(MS_UX_RECORDER)
                          ,
                          core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
    );
    ~SettingsFeatureModule();

    SettingsFeatureModule(const SettingsFeatureModule&) = delete;
    SettingsFeatureModule& operator=(const SettingsFeatureModule&) = delete;

private:
#if defined(MS_UX_RECORDER)
    core::context::standalone::ux::DeviceSettingsUxSurface device_settings_ux_surface_;
    core::context::standalone::ux::DataManagerUxSurface data_manager_ux_surface_;
#endif

    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay>
        device_settings_selector_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay> data_manager_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay> sequencer_settings_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay>
        sequencer_settings_selector_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay> data_manager_dialog_overlay_;
    core::app::ExtmemUniquePtr<core::context::standalone::DeviceSettingsSelectorPresenter>
        device_settings_presenter_;
    core::app::ExtmemUniquePtr<core::context::standalone::DataManagerPresenter>
        data_manager_presenter_;
    core::app::ExtmemUniquePtr<core::context::standalone::SequencerSettingsOverlayPresenter>
        sequencer_settings_presenter_;
    std::unique_ptr<core::handler::DeviceSettingsHandler> device_settings_handler_;
    std::unique_ptr<core::handler::DataManagerHandler> data_manager_handler_;
    std::unique_ptr<core::handler::SequencerSettingsHandler> sequencer_settings_handler_;
};

}  // namespace core::context::standalone

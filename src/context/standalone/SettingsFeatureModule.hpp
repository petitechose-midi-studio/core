#pragma once

#include <memory>

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "handler/settings/DataManagerDomainServices.hpp"
#include "handler/settings/GlobalSettingsDomainServices.hpp"
#include "handler/settings/DataManagerHandler.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/settings/SequencerSettingsDomainServices.hpp"
#include "state/CoreSettings.hpp"
#include "state/DataManagerState.hpp"
#include "state/GlobalSettingsState.hpp"
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
class GlobalSettingsOverlayPresenter;
class SequencerSettingsOverlayPresenter;
}  // namespace core::context::standalone

namespace core::handler {
class DataManagerHandler;
class GlobalSettingsHandler;
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
        core::state::GlobalSettingsState& globalSettings;
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
                          core::handler::GlobalSettingsDomainServices globalSettingsServices,
                          core::handler::SequencerSettingsDomainServices sequencerSettingsServices,
                          core::handler::DataManagerDomainServices dataManagerServices,
                          oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                          oc::api::EncoderAPI& encoders,
                          oc::api::ButtonAPI& buttons,
                          lv_obj_t* mainZone,
                          core::ui::ContextSoftkeyBar& softkeyBar,
                          core::ui::TransportBar& transportBar,
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
    core::context::standalone::ux::GlobalSettingsUxSurface global_settings_ux_surface_;
    core::context::standalone::ux::DataManagerUxSurface data_manager_ux_surface_;
#endif

    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay> global_settings_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay>
        global_settings_selector_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay> data_manager_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListKeyValueOverlay> sequencer_settings_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay>
        sequencer_settings_selector_overlay_;
    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay> data_manager_dialog_overlay_;
    core::app::ExtmemUniquePtr<core::context::standalone::GlobalSettingsOverlayPresenter>
        global_settings_presenter_;
    core::app::ExtmemUniquePtr<core::context::standalone::DataManagerPresenter>
        data_manager_presenter_;
    core::app::ExtmemUniquePtr<core::context::standalone::SequencerSettingsOverlayPresenter>
        sequencer_settings_presenter_;
    std::unique_ptr<core::handler::GlobalSettingsHandler> global_settings_handler_;
    std::unique_ptr<core::handler::DataManagerHandler> data_manager_handler_;
    std::unique_ptr<core::handler::SequencerSettingsHandler> sequencer_settings_handler_;
};

}  // namespace core::context::standalone

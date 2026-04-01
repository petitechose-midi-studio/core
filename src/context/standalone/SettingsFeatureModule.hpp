#pragma once

#include <memory>

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/Signal.hpp>

#include "handler/settings/DataManagerDomainServices.hpp"
#include "handler/settings/GlobalSettingsDomainServices.hpp"
#include "handler/settings/DataManagerHandler.hpp"
#include "state/CoreSettings.hpp"
#include "state/DataManagerState.hpp"
#include "state/GlobalSettingsState.hpp"
#include "state/MidiSyncState.hpp"
#include "ui/OverlayTypes.hpp"
#include "ui/ViewTypes.hpp"

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
}  // namespace core::context::standalone

namespace core::handler {
class DataManagerHandler;
class GlobalSettingsHandler;
}  // namespace core::handler

namespace core::context::standalone {

class SettingsFeatureModule {
public:
    struct StateRefs {
        core::state::GlobalSettingsState& globalSettings;
        core::state::MidiSyncState& midiSync;
        core::state::CoreSettings& settings;
        core::state::DataManagerState& dataManager;
        oc::state::Signal<core::ui::ViewType, 8>& activeView;
    };

    SettingsFeatureModule(StateRefs stateRefs,
                          core::handler::GlobalSettingsDomainServices globalSettingsServices,
                          core::handler::DataManagerDomainServices dataManagerServices,
                          oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                          oc::api::EncoderAPI& encoders,
                          oc::api::ButtonAPI& buttons,
                          lv_obj_t* mainZone,
                          core::ui::ContextSoftkeyBar& softkeyBar,
                          core::ui::TransportBar& transportBar,
                          core::handler::DataManagerHandler::ViewScopes viewScopes);
    ~SettingsFeatureModule();

    SettingsFeatureModule(const SettingsFeatureModule&) = delete;
    SettingsFeatureModule& operator=(const SettingsFeatureModule&) = delete;

private:
    std::unique_ptr<ms::ui::VirtualListKeyValueOverlay> global_settings_overlay_;
    std::unique_ptr<ms::ui::VirtualListSelectorOverlay> global_settings_selector_overlay_;
    std::unique_ptr<ms::ui::VirtualListKeyValueOverlay> data_manager_overlay_;
    std::unique_ptr<ms::ui::VirtualListSelectorOverlay> data_manager_dialog_overlay_;
    std::unique_ptr<core::context::standalone::GlobalSettingsOverlayPresenter>
        global_settings_presenter_;
    std::unique_ptr<core::context::standalone::DataManagerPresenter> data_manager_presenter_;
    std::unique_ptr<core::handler::GlobalSettingsHandler> global_settings_handler_;
    std::unique_ptr<core::handler::DataManagerHandler> data_manager_handler_;
};

}  // namespace core::context::standalone

#pragma once

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "handler/settings/DeviceSettingsDomainServices.hpp"
#include "state/DeviceSettingsState.hpp"
#include "state/MidiSyncState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"

#if defined(MS_UX_RECORDER)
#include "context/standalone/ux/StandaloneUxSurfaces.hpp"
#endif

namespace ms::ui {
class VirtualListSelectorOverlay;
}

namespace core::context::standalone {
class DeviceSettingsSelectorPresenter;
class OverlayPresentationRegistry;
}  // namespace core::context::standalone

namespace core::handler {
class DeviceSettingsHandler;
}  // namespace core::handler

namespace core::context::standalone {

/**
 * Owns the hardware settings selector and its modal handler.
 *
 * Applying setting changes and persistence actions is delegated to the domain
 * services supplied at construction.
 */
class SettingsFeatureModule {
public:
    struct StateRefs {
        core::state::DeviceSettingsState& deviceSettings;
        core::state::MidiSyncState& midiSync;
    };

    SettingsFeatureModule(StateRefs stateRefs,
                          core::handler::DeviceSettingsDomainServices deviceSettingsServices,
                          oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                          OverlayPresentationRegistry& overlayPresentations,
                          oc::api::EncoderAPI& encoders,
                          oc::api::ButtonAPI& buttons,
                          lv_obj_t* mainZone,
                          oc::type::ScopeID deviceSettingsViewScope
#if defined(MS_UX_RECORDER)
                          ,
                          core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
    );
    ~SettingsFeatureModule();

    SettingsFeatureModule(const SettingsFeatureModule&) = delete;
    SettingsFeatureModule& operator=(const SettingsFeatureModule&) = delete;

    [[nodiscard]] bool valid() const { return valid_; }

private:
#if defined(MS_UX_RECORDER)
    core::context::standalone::ux::DeviceSettingsUxSurface device_settings_ux_surface_;
#endif

    core::app::ExtmemUniquePtr<ms::ui::VirtualListSelectorOverlay>
        device_settings_selector_overlay_;
    core::app::ExtmemUniquePtr<core::context::standalone::DeviceSettingsSelectorPresenter>
        device_settings_presenter_;
    core::app::ExtmemUniquePtr<core::handler::DeviceSettingsHandler> device_settings_handler_;
    bool valid_ = false;
};

}  // namespace core::context::standalone

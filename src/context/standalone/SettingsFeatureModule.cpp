#include "context/standalone/SettingsFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/DeviceSettingsSelectorPresenter.hpp"
#include "context/standalone/OverlayPresentationRegistry.hpp"
#include "context/standalone/SequencerSettingsOverlayPresenter.hpp"
#include "handler/settings/DeviceSettingsDomainServices.hpp"
#include "handler/settings/DeviceSettingsHandler.hpp"
#include "handler/settings/SequencerSettingsDomainServices.hpp"
#include "handler/settings/SequencerSettingsHandler.hpp"

namespace core::context::standalone {

FLASHMEM SettingsFeatureModule::SettingsFeatureModule(
    StateRefs stateRefs,
    core::handler::DeviceSettingsDomainServices deviceSettingsServices,
    core::handler::SequencerSettingsDomainServices sequencerSettingsServices,
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
)
#if defined(MS_UX_RECORDER)
    : device_settings_ux_surface_(stateRefs.deviceSettings, stateRefs.midiSync)
#endif
{
#if defined(MS_UX_RECORDER)
    if (uxRegistry &&
        !uxRegistry->add(
            device_settings_ux_surface_,
            core::context::standalone::ux::priority::DEVICE_SETTINGS
        )) return;
#endif

    if (!mainZone || deviceSettingsViewScope == 0) return;
    device_settings_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    if (!device_settings_selector_overlay_ ||
        !device_settings_selector_overlay_->getElement() || !registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::DEVICE_SETTINGS_SELECTOR,
        device_settings_selector_overlay_->getElement()
    )) return;

    sequencer_settings_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    if (!sequencer_settings_overlay_ || !sequencer_settings_overlay_->getElement() ||
        !registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::SEQUENCER_SETTINGS,
        sequencer_settings_overlay_->getElement()
    )) return;

    sequencer_settings_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    if (!sequencer_settings_selector_overlay_ ||
        !sequencer_settings_selector_overlay_->getElement() || !registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::SEQUENCER_SETTINGS_SELECTOR,
        sequencer_settings_selector_overlay_->getElement()
    )) return;

    device_settings_presenter_ =
        core::app::makeExtmemUnique<DeviceSettingsSelectorPresenter>(
            DeviceSettingsSelectorPresenter::StateRefs{
                stateRefs.deviceSettings,
                stateRefs.midiSync,
            },
            *device_settings_selector_overlay_
        );
    if (!device_settings_presenter_ || !device_settings_presenter_->bind()) return;

    sequencer_settings_presenter_ =
        core::app::makeExtmemUnique<SequencerSettingsOverlayPresenter>(
            SequencerSettingsOverlayPresenter::StateRefs{
                stateRefs.sequencerSettings,
                stateRefs.sequencerTracks,
            },
            *sequencer_settings_overlay_,
            *sequencer_settings_selector_overlay_
        );
    if (!sequencer_settings_presenter_ || !sequencer_settings_presenter_->bind()) return;

    device_settings_handler_ =
        core::app::makeExtmemUnique<core::handler::DeviceSettingsHandler>(
        core::handler::DeviceSettingsHandler::StateRefs{
            stateRefs.deviceSettings,
        },
        deviceSettingsServices,
        overlays,
        encoders,
        buttons,
        deviceSettingsViewScope,
        oc::ui::lvgl::scopeID(device_settings_selector_overlay_->getElement())
    );

    sequencer_settings_handler_ =
        core::app::makeExtmemUnique<core::handler::SequencerSettingsHandler>(
        core::handler::SequencerSettingsHandler::StateRefs{
            stateRefs.sequencerSettings,
            stateRefs.viewSelector,
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
            stateRefs.history,
        },
        sequencerSettingsServices,
        overlays,
        encoders,
        buttons,
        oc::ui::lvgl::scopeID(sequencer_settings_overlay_->getElement()),
        oc::ui::lvgl::scopeID(sequencer_settings_selector_overlay_->getElement())
    );
    valid_ = device_settings_handler_ && sequencer_settings_handler_;
}

FLASHMEM SettingsFeatureModule::~SettingsFeatureModule() = default;

}  // namespace core::context::standalone

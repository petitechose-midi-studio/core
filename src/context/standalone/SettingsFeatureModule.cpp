#include "context/standalone/SettingsFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/DataManagerPresenter.hpp"
#include "context/standalone/GlobalSettingsOverlayPresenter.hpp"
#include "context/standalone/SequencerSettingsOverlayPresenter.hpp"
#include "handler/settings/DataManagerHandler.hpp"
#include "handler/settings/GlobalSettingsDomainServices.hpp"
#include "handler/settings/GlobalSettingsHandler.hpp"
#include "handler/settings/SequencerSettingsDomainServices.hpp"
#include "handler/settings/SequencerSettingsHandler.hpp"

namespace core::context::standalone {

FLASHMEM SettingsFeatureModule::SettingsFeatureModule(
    StateRefs stateRefs,
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
)
#if defined(MS_UX_RECORDER)
    : global_settings_ux_surface_(stateRefs.globalSettings, stateRefs.midiSync),
      data_manager_ux_surface_(stateRefs.dataManager)
#endif
{
#if defined(MS_UX_RECORDER)
    if (uxRegistry) {
        uxRegistry->add(
            global_settings_ux_surface_,
            core::context::standalone::ux::priority::GLOBAL_SETTINGS
        );
        uxRegistry->add(
            data_manager_ux_surface_,
            core::context::standalone::ux::priority::DATA_MANAGER
        );
    }
#endif

    global_settings_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::GLOBAL_SETTINGS,
        oc::ui::lvgl::scopeID(global_settings_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    global_settings_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR,
        oc::ui::lvgl::scopeID(global_settings_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    data_manager_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::DATA_MANAGER,
        oc::ui::lvgl::scopeID(data_manager_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    data_manager_dialog_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::DATA_MANAGER_DIALOG,
        oc::ui::lvgl::scopeID(data_manager_dialog_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    sequencer_settings_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::SEQUENCER_SETTINGS,
        oc::ui::lvgl::scopeID(sequencer_settings_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    sequencer_settings_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::SEQUENCER_SETTINGS_SELECTOR,
        oc::ui::lvgl::scopeID(sequencer_settings_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    global_settings_presenter_ =
        core::app::makeExtmemUnique<GlobalSettingsOverlayPresenter>(
            GlobalSettingsOverlayPresenter::StateRefs{
                stateRefs.globalSettings,
                stateRefs.midiSync,
            },
            *global_settings_overlay_,
            *global_settings_selector_overlay_
        );
    global_settings_presenter_->bind();

    data_manager_presenter_ = core::app::makeExtmemUnique<DataManagerPresenter>(
        DataManagerPresenter::StateRefs{
            stateRefs.dataManager,
        },
        *data_manager_overlay_,
        *data_manager_dialog_overlay_,
        softkeyBar,
        transportBar
    );
    data_manager_presenter_->bind();
    data_manager_presenter_->renderSoftkeyBar();

    sequencer_settings_presenter_ =
        core::app::makeExtmemUnique<SequencerSettingsOverlayPresenter>(
            SequencerSettingsOverlayPresenter::StateRefs{
                stateRefs.sequencerSettings,
                stateRefs.sequencerTracks,
            },
            *sequencer_settings_overlay_,
            *sequencer_settings_selector_overlay_
        );
    sequencer_settings_presenter_->bind();

    global_settings_handler_ = std::make_unique<core::handler::GlobalSettingsHandler>(
        core::handler::GlobalSettingsHandler::StateRefs{
            stateRefs.globalSettings,
            stateRefs.viewSelector,
        },
        globalSettingsServices,
        overlays,
        encoders,
        buttons,
        oc::ui::lvgl::scopeID(global_settings_overlay_->getElement()),
        oc::ui::lvgl::scopeID(global_settings_selector_overlay_->getElement())
    );

    data_manager_handler_ = std::make_unique<core::handler::DataManagerHandler>(
        core::handler::DataManagerHandler::StateRefs{
            stateRefs.dataManager,
            stateRefs.activeView,
        },
        dataManagerServices,
        overlays,
        encoders,
        buttons,
        viewScopes,
        oc::ui::lvgl::scopeID(data_manager_overlay_->getElement()),
        oc::ui::lvgl::scopeID(data_manager_dialog_overlay_->getElement())
    );

    sequencer_settings_handler_ = std::make_unique<core::handler::SequencerSettingsHandler>(
        core::handler::SequencerSettingsHandler::StateRefs{
            stateRefs.sequencerSettings,
            stateRefs.viewSelector,
        },
        sequencerSettingsServices,
        overlays,
        encoders,
        buttons,
        oc::ui::lvgl::scopeID(sequencer_settings_overlay_->getElement()),
        oc::ui::lvgl::scopeID(sequencer_settings_selector_overlay_->getElement())
    );
}

SettingsFeatureModule::~SettingsFeatureModule() = default;

}  // namespace core::context::standalone

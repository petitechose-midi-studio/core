#include "context/standalone/SettingsFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/DataManagerPresenter.hpp"
#include "context/standalone/GlobalSettingsOverlayPresenter.hpp"
#include "handler/settings/DataManagerHandler.hpp"
#include "handler/settings/GlobalSettingsHandler.hpp"

namespace core::context::standalone {

FLASHMEM SettingsFeatureModule::SettingsFeatureModule(
    StateRefs stateRefs,
    core::handler::DataManagerHandler::Services services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    lv_obj_t* mainZone,
    core::ui::ContextSoftkeyBar& softkeyBar,
    core::ui::TransportBar& transportBar,
    core::handler::DataManagerHandler::ViewScopes viewScopes
) {
    global_settings_overlay_ = std::make_unique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::GLOBAL_SETTINGS,
        oc::ui::lvgl::scopeID(global_settings_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    global_settings_selector_overlay_ =
        std::make_unique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR,
        oc::ui::lvgl::scopeID(global_settings_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    data_manager_overlay_ = std::make_unique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::DATA_MANAGER,
        oc::ui::lvgl::scopeID(data_manager_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    data_manager_dialog_overlay_ =
        std::make_unique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::DATA_MANAGER_DIALOG,
        oc::ui::lvgl::scopeID(data_manager_dialog_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    global_settings_presenter_ =
        std::make_unique<GlobalSettingsOverlayPresenter>(
            GlobalSettingsOverlayPresenter::StateRefs{
                stateRefs.globalSettings,
                stateRefs.midiSync,
            },
            *global_settings_overlay_,
            *global_settings_selector_overlay_
        );
    global_settings_presenter_->bind();

    data_manager_presenter_ = std::make_unique<DataManagerPresenter>(
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

    global_settings_handler_ = std::make_unique<core::handler::GlobalSettingsHandler>(
        core::handler::GlobalSettingsHandler::StateRefs{
            stateRefs.globalSettings,
            stateRefs.midiSync,
            stateRefs.settings,
        },
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
        services,
        overlays,
        encoders,
        buttons,
        viewScopes,
        oc::ui::lvgl::scopeID(data_manager_overlay_->getElement()),
        oc::ui::lvgl::scopeID(data_manager_dialog_overlay_->getElement())
    );
}

SettingsFeatureModule::~SettingsFeatureModule() = default;

}  // namespace core::context::standalone

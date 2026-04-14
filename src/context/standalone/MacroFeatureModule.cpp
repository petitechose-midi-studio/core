#include "context/standalone/MacroFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/time/Time.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/MacroOverlayPresenter.hpp"
#include "handler/macro/MacroDomainServices.hpp"
#include "handler/macro/MacroEditHandler.hpp"
#include "handler/macro/MacroMidiHandler.hpp"
#include "handler/macro/MacroPerformanceHandler.hpp"
#include "handler/macro/MacroValueHandler.hpp"

namespace core::context::standalone {

FLASHMEM MacroFeatureModule::MacroFeatureModule(StateRefs stateRefs,
                                                core::handler::MacroDomainServices services,
                                                oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                                oc::api::EncoderAPI& encoders,
                                                oc::api::ButtonAPI& buttons,
                                                oc::api::MidiAPI& midi,
                                                lv_obj_t* mainZone,
                                                lv_obj_t* macroViewScope) {
    edit_overlay_ = core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::MACRO_EDIT,
        oc::ui::lvgl::scopeID(edit_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    edit_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::MACRO_EDIT_SELECTOR,
        oc::ui::lvgl::scopeID(edit_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    page_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::PAGE_SELECTOR,
        oc::ui::lvgl::scopeID(page_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    target_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR,
        oc::ui::lvgl::scopeID(target_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    presenter_ = core::app::makeExtmemUnique<MacroOverlayPresenter>(
        MacroOverlayPresenter::StateRefs{
            stateRefs.macroEdit,
            stateRefs.pages,
            stateRefs.configRevision,
        },
        *edit_overlay_,
        *edit_selector_overlay_,
        *page_selector_overlay_,
        *target_selector_overlay_
    );
    presenter_->bind();

    const auto macroViewScopeId = oc::ui::lvgl::scopeID(macroViewScope);
    value_handler_ = std::make_unique<core::handler::MacroValueHandler>(
        core::handler::MacroValueHandler::StateRefs{
            stateRefs.macroUi,
            stateRefs.activeView,
            stateRefs.macroEdit,
        },
        services,
        overlays,
        encoders,
        midi,
        macroViewScopeId
    );
    performance_handler_ = std::make_unique<core::handler::MacroPerformanceHandler>(
        core::handler::MacroPerformanceHandler::StateRefs{
            stateRefs.macroUi,
            stateRefs.pages,
            stateRefs.trackNavigation,
            stateRefs.sharedTrackActive,
            stateRefs.structureNavigationFocus,
            stateRefs.structureClipboard,
        },
        services,
        overlays,
        encoders,
        buttons,
        macroViewScopeId
    );
    midi_handler_ = std::make_unique<core::handler::MacroMidiHandler>(
        core::handler::MacroMidiHandler::StateRefs{stateRefs.activeView},
        services,
        encoders
    );
    edit_handler_ = std::make_unique<core::handler::MacroEditHandler>(
        core::handler::MacroEditHandler::StateRefs{
            stateRefs.macroEdit,
            stateRefs.pages,
            stateRefs.macroUi,
        },
        services,
        overlays,
        encoders,
        buttons,
        macroViewScopeId,
        oc::ui::lvgl::scopeID(edit_overlay_->getElement()),
        oc::ui::lvgl::scopeID(edit_selector_overlay_->getElement()),
        oc::ui::lvgl::scopeID(page_selector_overlay_->getElement()),
        oc::ui::lvgl::scopeID(target_selector_overlay_->getElement()),
        oc::time::millis
    );
}

MacroFeatureModule::~MacroFeatureModule() = default;

void MacroFeatureModule::onCC(uint8_t channel, uint8_t cc, uint8_t value) {
    if (midi_handler_) {
        midi_handler_->onCC(channel, cc, value);
    }
}

void MacroFeatureModule::onNoteIn() {
    if (midi_handler_) {
        midi_handler_->onNoteIn();
    }
}

}  // namespace core::context::standalone

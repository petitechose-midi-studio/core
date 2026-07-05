#include "context/standalone/MacroFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/time/Time.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/MacroOverlayPresenter.hpp"
#include "handler/macro/MacroAutomationHandler.hpp"
#include "handler/macro/MacroEditHandler.hpp"
#include "handler/macro/MacroMidiHandler.hpp"
#include "handler/macro/MacroPerformanceHandler.hpp"
#include "handler/macro/MacroValueHandler.hpp"

namespace core::context::standalone {

FLASHMEM MacroFeatureModule::MacroFeatureModule(
                                                StateRefs stateRefs,
                                                core::handler::MacroEditDomainServices editServices,
                                                core::handler::MacroPerformanceDomainServices performanceServices,
                                                core::handler::MacroStructureDomainServices structureServices,
                                                oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                                                oc::api::EncoderAPI& encoders,
                                                oc::api::ButtonAPI& buttons,
                                                oc::api::MidiAPI& midi,
                                                lv_obj_t* mainZone,
                                                lv_obj_t* macroViewScope
#if defined(MS_UX_RECORDER)
                                                ,
                                                core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
)
#if defined(MS_UX_RECORDER)
    : macro_edit_ux_surface_(
          stateRefs.activeView,
          stateRefs.macroEdit,
          stateRefs.pages,
          stateRefs.macroUi,
          stateRefs.configRevision
      ),
      macro_structure_ux_surface_(
          stateRefs.activeView,
          stateRefs.structureNavigationFocus,
          stateRefs.trackNavigation,
          stateRefs.structureClipboard,
          stateRefs.macroUi,
          stateRefs.pages,
          stateRefs.macroEdit,
          &structure_ux_trace_state_
      ),
      macro_performance_ux_surface_(
          stateRefs.activeView,
          stateRefs.macroUi,
          stateRefs.macroEdit
      ),
      macro_value_ux_surface_(
          stateRefs.activeView,
          stateRefs.macros,
          stateRefs.pages,
          stateRefs.macroUi,
          stateRefs.macroEdit
      )
#endif
{
#if defined(MS_UX_RECORDER)
    if (uxRegistry) {
        uxRegistry->add(
            macro_edit_ux_surface_,
            core::context::standalone::ux::priority::MACRO_EDIT
        );
        uxRegistry->add(
            macro_structure_ux_surface_,
            core::context::standalone::ux::priority::MACRO_STRUCTURE
        );
        uxRegistry->add(
            macro_performance_ux_surface_,
            core::context::standalone::ux::priority::MACRO_PERFORMANCE
        );
        uxRegistry->add(
            macro_value_ux_surface_,
            core::context::standalone::ux::priority::MACRO_VALUE
        );
    }
#endif
    edit_overlay_ = core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::MACRO_EDIT,
        oc::ui::lvgl::scopeID(edit_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );

    automation_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    overlays.registerCleanup(
        core::ui::OverlayType::MACRO_AUTOMATION,
        oc::ui::lvgl::scopeID(automation_overlay_->getElement()),
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
            stateRefs.macroUi,
            stateRefs.configRevision,
        },
        *edit_overlay_,
        *automation_overlay_,
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
        performanceServices,
        overlays,
        encoders,
        buttons,
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
        performanceServices,
        structureServices,
        overlays,
        encoders,
        buttons,
        macroViewScopeId
#if defined(MS_UX_RECORDER)
        ,
        &structure_ux_trace_state_
#endif
    );
    midi_handler_ = std::make_unique<core::handler::MacroMidiHandler>(
        core::handler::MacroMidiHandler::StateRefs{stateRefs.activeView},
        performanceServices,
        encoders
    );
    automation_playback_ = std::make_unique<core::handler::MacroAutomationPlaybackService>(
        core::handler::MacroAutomationPlaybackService::StateRefs{
            stateRefs.pages,
            stateRefs.macroUi,
            stateRefs.statusBar,
        },
        performanceServices,
        midi
    );
    edit_handler_ = std::make_unique<core::handler::MacroEditHandler>(
        core::handler::MacroEditHandler::StateRefs{
            stateRefs.macroEdit,
            stateRefs.pages,
            stateRefs.macroUi,
        },
        editServices,
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
    automation_handler_ = std::make_unique<core::handler::MacroAutomationHandler>(
        core::handler::MacroAutomationHandler::StateRefs{
            stateRefs.macroEdit,
        },
        editServices,
        overlays,
        encoders,
        buttons,
        oc::ui::lvgl::scopeID(automation_overlay_->getElement())
    );
}

FLASHMEM MacroFeatureModule::~MacroFeatureModule() = default;

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

void MacroFeatureModule::update(uint32_t nowMs) {
    if (automation_playback_) {
        automation_playback_->update(nowMs);
    }
}

}  // namespace core::context::standalone

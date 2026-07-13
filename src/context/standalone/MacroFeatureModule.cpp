#include "context/standalone/MacroFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/time/Time.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "context/standalone/MacroOverlayPresenter.hpp"
#include "context/standalone/OverlayPresentationRegistry.hpp"
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
                                                OverlayPresentationRegistry& overlayPresentations,
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
    if (!mainZone || !macroViewScope) return;

    edit_overlay_ = core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    if (!edit_overlay_ || !edit_overlay_->getElement() || !registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::MACRO_EDIT,
        edit_overlay_->getElement()
    )) return;

    automation_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    if (!automation_overlay_ || !automation_overlay_->getElement() || !registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::MACRO_AUTOMATION,
        automation_overlay_->getElement()
    )) return;

    edit_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    if (!edit_selector_overlay_ || !edit_selector_overlay_->getElement() ||
        !registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::MACRO_EDIT_SELECTOR,
        edit_selector_overlay_->getElement()
    )) return;

    page_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    if (!page_selector_overlay_ || !page_selector_overlay_->getElement() ||
        !registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::PAGE_SELECTOR,
        page_selector_overlay_->getElement()
    )) return;

    target_selector_overlay_ =
        core::app::makeExtmemUnique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    if (!target_selector_overlay_ || !target_selector_overlay_->getElement() ||
        !registerOverlaySurface(
        overlays,
        overlayPresentations,
        core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR,
        target_selector_overlay_->getElement()
    )) return;

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
    if (!presenter_ || !presenter_->bind()) return;

    // One EXTMEM-owned resolver instance is the only production owner of
    // classic-CC emission for Macro authors. Keeping the adapter beside it
    // prevents handlers from opening or publishing partial resolver frames.
    midi_cc_runtime_ =
        core::app::makeExtmemUnique<core::handler::MidiCcRuntimeAggregator>(midi);
    if (!midi_cc_runtime_) return;
    macro_midi_runtime_ =
        core::app::makeExtmemUnique<core::handler::MacroMidiCcRuntimeAdapter>(
            core::handler::MacroMidiCcRuntimeAdapter::StateRefs{
                stateRefs.pages,
                stateRefs.macroUi,
            },
            performanceServices,
            *midi_cc_runtime_
        );
    if (!macro_midi_runtime_) return;

    const auto macroViewScopeId = oc::ui::lvgl::scopeID(macroViewScope);
    if (macroViewScopeId == 0) return;
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
        *macro_midi_runtime_,
        macroViewScopeId
    );
    performance_handler_ = core::app::makeExtmemUnique<core::handler::MacroPerformanceHandler>(
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
        macroViewScopeId,
        core::time_compat::millis
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
        *macro_midi_runtime_
    );
    edit_handler_ = core::app::makeExtmemUnique<core::handler::MacroEditHandler>(
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
    automation_handler_ = core::app::makeExtmemUnique<core::handler::MacroAutomationHandler>(
        core::handler::MacroAutomationHandler::StateRefs{
            stateRefs.macroEdit,
        },
        editServices,
        overlays,
        encoders,
        buttons,
        oc::ui::lvgl::scopeID(automation_overlay_->getElement())
    );
    valid_ = value_handler_ && midi_handler_ && automation_playback_ &&
             performance_handler_ && edit_handler_ && automation_handler_;
}

FLASHMEM MacroFeatureModule::~MacroFeatureModule() = default;

const core::state::shared::MidiCcResolutionTelemetry*
MacroFeatureModule::midiCcTelemetry() const {
    return macro_midi_runtime_ != nullptr
        ? &macro_midi_runtime_->telemetry()
        : nullptr;
}

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
    if (performance_handler_) {
        performance_handler_->update(nowMs);
    }
    if (automation_playback_) {
        automation_playback_->update(nowMs);
    }
}

}  // namespace core::context::standalone

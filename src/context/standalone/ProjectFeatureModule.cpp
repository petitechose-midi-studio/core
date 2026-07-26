#include "context/standalone/ProjectFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/ui/lvgl/Scope.hpp>

namespace core::context::standalone {

FLASHMEM ProjectFeatureModule::ProjectFeatureModule(StateRefs stateRefs,
                                                    core::handler::SequencerSettingsDomainServices sequencerSettings,
                                                    core::handler::MacroEditDomainServices macroEditServices,
                                                    oc::api::EncoderAPI& encoders,
                                                    oc::api::ButtonAPI& buttons,
                                                    lv_obj_t* projectViewElement
#if defined(MS_UX_RECORDER)
                                                    ,
                                                    core::validation::ux::SemanticUxSurfaceRegistry* uxRegistry
#endif
)
#if defined(MS_UX_RECORDER)
    : modulators_ux_surface_(
          stateRefs.activeView,
          stateRefs.navigation,
          stateRefs.pages,
          stateRefs.macroUi,
          stateRefs.clipboard,
          stateRefs.macroHistory
      )
#endif
{
#if defined(MS_UX_RECORDER)
    if (uxRegistry &&
        !uxRegistry->add(
            modulators_ux_surface_,
            core::context::standalone::ux::priority::PROJECT_MODULATORS
        )) return;
#endif
    if (!projectViewElement) return;
    const auto viewScope = oc::ui::lvgl::scopeID(projectViewElement);
    if (viewScope == 0) return;
    handler_ = core::app::makeExtmemUnique<core::handler::ProjectHandler>(
        core::handler::ProjectHandler::StateRefs{
            stateRefs.overlays,
            stateRefs.activeView,
            stateRefs.navigation,
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
            stateRefs.projectTracks,
            stateRefs.trackDomain,
            stateRefs.statusBar,
            stateRefs.midiSync,
            stateRefs.pages,
            stateRefs.macroUi,
            stateRefs.macros,
            stateRefs.macroEdit,
            stateRefs.configRevision,
            stateRefs.macroHistory,
            stateRefs.settingsHistory,
            stateRefs.clipboard,
            stateRefs.history,
            stateRefs.lifecycle,
        },
        sequencerSettings,
        macroEditServices,
        encoders,
        buttons,
        viewScope,
        oc::time::millis
    );
}

void ProjectFeatureModule::update(uint32_t nowMs) {
    if (handler_) handler_->update(nowMs);
}

FLASHMEM ProjectFeatureModule::~ProjectFeatureModule() = default;

FLASHMEM void ProjectFeatureModule::syncFocusedEncoder() const {
    if (handler_) handler_->syncFocusedEncoder();
}

}  // namespace core::context::standalone

#include "context/standalone/ProjectFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include <oc/time/Time.hpp>

namespace core::context::standalone {

FLASHMEM ProjectFeatureModule::ProjectFeatureModule(StateRefs stateRefs,
                                                    core::handler::SequencerSettingsDomainServices sequencerSettings,
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
          stateRefs.clipboard,
          stateRefs.macroHistory
      )
#endif
{
#if defined(MS_UX_RECORDER)
    if (uxRegistry) {
        uxRegistry->add(
            modulators_ux_surface_,
            core::context::standalone::ux::priority::PROJECT_MODULATORS
        );
    }
#endif
    if (!projectViewElement) return;
    const auto viewScope = oc::ui::lvgl::scopeID(projectViewElement);
    if (viewScope == 0) return;
    navigation_ = &stateRefs.navigation;
    handler_ = core::app::makeExtmemUnique<core::handler::ProjectHandler>(
        core::handler::ProjectHandler::StateRefs{
            stateRefs.overlays,
            stateRefs.activeView,
            stateRefs.navigation,
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
            stateRefs.statusBar,
            stateRefs.midiSync,
            stateRefs.pages,
            stateRefs.configRevision,
            stateRefs.macroHistory,
            stateRefs.clipboard,
            stateRefs.history,
            stateRefs.lifecycle,
        },
        sequencerSettings,
        encoders,
        buttons,
        viewScope,
        oc::time::millis
    );
}

void ProjectFeatureModule::update(uint32_t nowMs) {
    if (handler_) handler_->update(nowMs);
    if (!navigation_ ||
        navigation_->activeTab.get() !=
            core::state::project::ProjectTab::MODULATORS ||
        (nowMs - last_telemetry_refresh_ms_) < 100U) {
        return;
    }
    last_telemetry_refresh_ms_ = nowMs;
    navigation_->notifyTelemetryChanged();
}

FLASHMEM ProjectFeatureModule::~ProjectFeatureModule() = default;

FLASHMEM void ProjectFeatureModule::syncFocusedEncoder() const {
    if (handler_) handler_->syncFocusedEncoder();
}

}  // namespace core::context::standalone

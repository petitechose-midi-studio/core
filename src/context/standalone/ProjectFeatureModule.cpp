#include "context/standalone/ProjectFeatureModule.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/ui/lvgl/Scope.hpp>

namespace core::context::standalone {

FLASHMEM ProjectFeatureModule::ProjectFeatureModule(StateRefs stateRefs,
                                                    core::handler::SequencerSettingsDomainServices sequencerSettings,
                                                    oc::api::EncoderAPI& encoders,
                                                    oc::api::ButtonAPI& buttons,
                                                    lv_obj_t* projectViewElement) {
    if (!projectViewElement) return;
    const auto viewScope = oc::ui::lvgl::scopeID(projectViewElement);
    if (viewScope == 0) return;
    handler_ = core::app::makeExtmemUnique<core::handler::ProjectHandler>(
        core::handler::ProjectHandler::StateRefs{
            stateRefs.overlays,
            stateRefs.navigation,
            stateRefs.sequencer,
            stateRefs.sequencerTracks,
            stateRefs.statusBar,
            stateRefs.midiSync,
            stateRefs.history,
            stateRefs.lifecycle,
        },
        sequencerSettings,
        encoders,
        buttons,
        viewScope
    );
}

FLASHMEM ProjectFeatureModule::~ProjectFeatureModule() = default;

FLASHMEM void ProjectFeatureModule::syncFocusedEncoder() const {
    if (handler_) handler_->syncFocusedEncoder();
}

}  // namespace core::context::standalone

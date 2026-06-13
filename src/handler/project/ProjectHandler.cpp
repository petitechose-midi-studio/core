#include "handler/project/ProjectHandlerInternals.hpp"

namespace core::handler {

using namespace project_handler_internal;

FLASHMEM ProjectHandler::ProjectHandler(StateRefs state,
                                        SequencerSettingsDomainServices sequencerSettings,
                                        oc::api::EncoderAPI& encoders,
                                        oc::api::ButtonAPI& buttons,
                                        oc::type::ScopeID projectViewScope)
    : overlays_(state.overlays)
    , navigation_(state.navigation)
    , sequencer_(state.sequencer)
    , sequencer_tracks_(state.sequencerTracks)
    , status_bar_(state.statusBar)
    , midi_sync_(state.midiSync)
    , history_(state.history)
    , lifecycle_(state.lifecycle)
    , sequencer_settings_(sequencerSettings)
    , encoders_(encoders)
    , buttons_(buttons)
    , project_view_scope_(projectViewScope) {
    setupBindings();
}

FLASHMEM bool ProjectHandler::canHandleProjectInput() const {
    return !overlays_.hasVisible();
}

FLASHMEM bool ProjectHandler::projectConfirmationActive() const {
    return core::state::project::projectNavigationInProjectConfirmation(navigation_);
}

FLASHMEM bool ProjectHandler::physicalHoldActive() const {
    return canHandleProjectInput() && !projectConfirmationActive() &&
           navigation_.physicalHoldActive.get();
}

FLASHMEM bool ProjectHandler::regularProjectInputActive() const {
    return canHandleProjectInput() && !navigation_.physicalHoldActive.get();
}

FLASHMEM void ProjectHandler::enterPhysicalHoldLayer() {
    navigation_.physicalHoldActive.set(true);
}

FLASHMEM void ProjectHandler::leavePhysicalHoldLayer() {
    navigation_.physicalHoldActive.set(false);
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::resetProject() {
    lifecycle_.resetMusicalProject();
}

FLASHMEM void ProjectHandler::back() {
    core::state::project::backProjectNavigation(navigation_);
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::consumeUndo() {
    history_.undo();
}

FLASHMEM void ProjectHandler::consumeRedo() {
    history_.redo();
}


}  // namespace core::handler

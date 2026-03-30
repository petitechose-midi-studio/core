#include "state/CoreState.hpp"

#include <oc/log/Log.hpp>

#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "macro/MacroPersistenceWorkflow.hpp"
#include "macro/MacroWorkflow.hpp"
#include "sequencer/SequencerPersistenceWorkflow.hpp"
#include "sequencer/SequencerSnapshotOps.hpp"
#include "sequencer/SequencerTrackBankOps.hpp"

namespace core::state {

CoreState::CoreState(oc::interface::IStorage& settingsStorage,
                     oc::interface::IStorage& macroWorkspaceStorage,
                     oc::interface::IStorage& macroLibraryStorage,
                     oc::interface::IStorage& sequencerWorkspaceStorage,
                     oc::interface::IStorage& sequencerPatternLibraryStorage,
                     oc::interface::IStorage& sequencerSetLibraryStorage)
    : macroDomain_(macroWorkspaceStorage, macroLibraryStorage)
    , sequencerDomain_(sequencerWorkspaceStorage,
                       sequencerPatternLibraryStorage,
                       sequencerSetLibraryStorage)
    , settings(settingsStorage)
    , macros(macroDomain_.runtime)
    , pages(macroDomain_.pages)
    , configRevision(macroDomain_.configRevision)
    , macroPersistence(macroDomain_.persistence)
    , sequencer(sequencerDomain_.editor)
    , sequencerTracks(sequencerDomain_.tracks)
    , sequencerPersistence(sequencerDomain_.persistence)
    , overlays(systemUi_.overlays)
    , activeView(systemUi_.activeView)
    , viewSelector(systemUi_.viewSelector)
    , statusBar(systemUi_.statusBar)
    , midiSync(systemUi_.midiSync)
    , globalSettings(systemUi_.globalSettings)
    , dataManager(systemUi_.dataManager)
    , macroEdit(systemUi_.macroEdit) {
    CoreStateBootstrap::initialize(*this);
}

void CoreState::update() {
    CoreStateLifecycle::update(*this);
}

void CoreState::factoryReset() {
    CoreStateLifecycle::factoryReset(*this);
}

void CoreState::flush() {
    CoreStateLifecycle::flush(*this);
}

void CoreState::resetStandaloneTransientUi() {
    CoreStateLifecycle::resetStandaloneTransientUi(*this);
}

bool CoreState::isMacroPersistenceReady() const {
    return macroDomain_.persistenceReady;
}

bool CoreState::isSequencerPersistenceReady() const {
    return sequencerDomain_.persistenceReady;
}

void CoreState::persistMacroWorkspace() {
    persistMacroWorkspace_();
}

void CoreState::persistSequencerWorkspace() {
    persistSequencerWorkspace_();
}

void CoreState::queuePendingSequencerApply(const sequencer::SequencerState& staged, bool merge) {
    queueSequencerApply_(staged, merge);
}

void CoreState::queuePendingSequencerBankApply(
    const sequencer::SequencerTrackBankSnapshot& staged
) {
    queueSequencerBankApply_(staged);
}

void CoreState::clearPendingSequencerApply() {
    clearPendingSequencerApply_();
}

bool CoreState::hasPendingSequencerApply() const {
    return sequencerDomain_.pendingApply.valid;
}

void CoreState::queueSequencerApply_(const sequencer::SequencerState& staged, bool merge) {
    CoreStateLifecycle::queuePendingSequencerApply(*this, staged, merge);
}

void CoreState::queueSequencerBankApply_(const sequencer::SequencerTrackBankSnapshot& staged) {
    sequencerDomain_.pendingApply.bankSnapshot = staged;
    sequencerDomain_.pendingApply.anchorPlayhead = sequencer.playheadStep.get();
    sequencerDomain_.pendingApply.merge = false;
    sequencerDomain_.pendingApply.fullBank = true;
    sequencerDomain_.pendingApply.valid = true;
}

void CoreState::persistMacroWorkspace_() {
    if (!macroDomain_.persistenceReady) return;
    const auto status = macroPersistence.saveWorkspaceStatus(pages);
    if (status != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] Failed to persist macro workspace: {}",
                    persistence::persistenceWriteStatusLabel(status));
    }
}

void CoreState::persistSequencerWorkspace_() {
    if (!sequencerDomain_.persistenceReady) return;
    sequencer::storeActiveTrack(sequencerTracks, sequencer);
    const auto status = sequencerPersistence.saveWorkspaceStatus(sequencerTracks, sequencer);
    if (status != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] Failed to persist sequencer workspace: {}",
                    persistence::persistenceWriteStatusLabel(status));
    }
}

void CoreState::clearPendingSequencerApply_() {
    CoreStateLifecycle::clearPendingSequencerApply(*this);
}

}  // namespace core::state

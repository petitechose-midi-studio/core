#include "state/CoreState.hpp"

#include <new>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "state/shared/SharedTrackCoordinator.hpp"
#include "macro/MacroPersistenceWorkflow.hpp"
#include "macro/MacroWorkflow.hpp"
#include "sequencer/SequencerPersistenceWorkflow.hpp"
#include "sequencer/SequencerTrackBankOps.hpp"

namespace core::state {

namespace {

SequencerDomainState::PendingApply* createPendingApply() {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    void* memory = extmem_malloc(sizeof(SequencerDomainState::PendingApply));
    if (!memory) return nullptr;
    return new(memory) SequencerDomainState::PendingApply();
#else
    return new SequencerDomainState::PendingApply();
#endif
}

core::app::ExtmemUniquePtr<UiSystemState> createUiSystemState() {
    return core::app::makeExtmemUnique<UiSystemState>();
}

core::app::ExtmemUniquePtr<sequencer::SequencerState> createSequencerEditorState() {
    return core::app::makeExtmemUnique<sequencer::SequencerState>();
}

core::app::ExtmemUniquePtr<sequencer::SequencerTrackBankState> createSequencerTrackBankState() {
    return core::app::makeExtmemUnique<sequencer::SequencerTrackBankState>();
}

shared::SharedTrackCoordinator::StateRefs sharedTrackRefs(CoreState& state) {
    return shared::SharedTrackCoordinator::StateRefs{
        state.sharedTrackActive,
        state.sharedTrackEnabledMask,
        state.pages,
        state.sequencerTracks,
        state.sequencer,
    };
}

}  // namespace

SequencerDomainState::SequencerDomainState(oc::interface::IStorage& workspaceStorage,
                                           oc::interface::IStorage& patternLibraryStorage,
                                           oc::interface::IStorage& setLibraryStorage)
    : editor(createSequencerEditorState())
    , tracks(createSequencerTrackBankState())
    , persistence(workspaceStorage, patternLibraryStorage, setLibraryStorage)
    , pendingApply(nullptr) {
    if (!editor) {
        OC_LOG_ERROR("[CoreState] Failed to allocate sequencer editor state");
        while (true) {}
    }
    if (!tracks) {
        OC_LOG_ERROR("[CoreState] Failed to allocate sequencer track bank");
        while (true) {}
    }
}

void SequencerDomainState::PendingApplyDeleter::operator()(PendingApply* ptr) const noexcept {
    if (!ptr) return;
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    ptr->~PendingApply();
    extmem_free(ptr);
#else
    delete ptr;
#endif
}

FLASHMEM CoreState::CoreState(oc::interface::IStorage& settingsStorage,
                              oc::interface::IStorage& macroWorkspaceStorage,
                              oc::interface::IStorage& macroLibraryStorage,
                              oc::interface::IStorage& sequencerWorkspaceStorage,
                              oc::interface::IStorage& sequencerPatternLibraryStorage,
                              oc::interface::IStorage& sequencerSetLibraryStorage)
    : macroDomain_(macroWorkspaceStorage, macroLibraryStorage)
    , sequencerDomain_(sequencerWorkspaceStorage,
                       sequencerPatternLibraryStorage,
                       sequencerSetLibraryStorage)
    , systemUi_(createUiSystemState())
    , settings(settingsStorage)
    , macros(*macroDomain_.runtime)
    , pages(*macroDomain_.pages)
    , configRevision(macroDomain_.configRevision)
    , macroPersistence(macroDomain_.persistence)
    , sequencer(*sequencerDomain_.editor)
    , sequencerTracks(*sequencerDomain_.tracks)
    , sequencerHistory(sequencerDomain_.history)
    , sequencerPersistence(sequencerDomain_.persistence)
    , overlays(systemUi_->overlays)
    , activeView(systemUi_->activeView)
    , structureNavigationFocus(systemUi_->structureNavigationFocus)
    , sharedTrackActive(systemUi_->sharedTracks.activeIndex)
    , sharedTrackEnabledMask(systemUi_->sharedTracks.enabledMask)
    , trackNavigation(systemUi_->trackNavigation)
    , structureClipboard(systemUi_->structureClipboard)
    , viewSelector(systemUi_->viewSelector)
    , statusBar(systemUi_->statusBar)
    , midiSync(systemUi_->midiSync)
    , globalSettings(systemUi_->globalSettings)
    , sequencerSettings(systemUi_->sequencerSettings)
    , patternPitchSettings(systemUi_->patternPitchSettings)
    , dataManager(systemUi_->dataManager)
    , macroEdit(systemUi_->macroEdit)
    , macroUi(systemUi_->macroUi) {
    if (!macroDomain_.runtime) {
        OC_LOG_ERROR("[CoreState] Failed to allocate macro runtime state");
        while (true) {}
    }
    if (!macroDomain_.pages) {
        OC_LOG_ERROR("[CoreState] Failed to allocate macro pages state");
        while (true) {}
    }
    if (!systemUi_) {
        OC_LOG_ERROR("[CoreState] Failed to allocate UI system state");
        while (true) {}
    }
    sequencerDomain_.pendingApply.reset(createPendingApply());
    if (!sequencerDomain_.pendingApply) {
        OC_LOG_ERROR("[CoreState] Failed to allocate sequencer pending apply buffer");
        while (true) {}
    }
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

void CoreState::flushAutoPersist() {
    CoreStateLifecycle::flushAutoPersist(*this);
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

void CoreState::requestMacroWorkspacePersist() {
    requestMacroWorkspacePersist_();
}

void CoreState::persistSequencerWorkspace() {
    persistSequencerWorkspace_();
}

bool CoreState::recordSequencerPatternHistory(
    sequencer::SequencerHistoryPatternSnapshot before,
    sequencer::SequencerHistoryPatternSnapshot after
) {
    if (!sequencerHistory.recordPattern(std::move(before), std::move(after))) {
        return false;
    }

    sequencer::storeActiveTrack(sequencerTracks, sequencer);
    refreshSharedTrackStateFromSequencer();
    persistSequencerWorkspace_();
    return true;
}

bool CoreState::undoSequencerHistory() {
    if (!sequencerHistory.undo(sequencerTracks, sequencer)) {
        return false;
    }

    refreshSharedTrackStateFromSequencer();
    persistSequencerWorkspace_();
    return true;
}

bool CoreState::redoSequencerHistory() {
    if (!sequencerHistory.redo(sequencerTracks, sequencer)) {
        return false;
    }

    refreshSharedTrackStateFromSequencer();
    persistSequencerWorkspace_();
    return true;
}

void CoreState::queuePendingSequencerApply(const sequencer::SequencerState& staged, bool merge) {
    queueSequencerApply_(staged, merge);
}

void CoreState::queuePendingSequencerBankApply(
    const sequencer::SequencerTrackBankState& stagedBank,
    const sequencer::SequencerState& staged
) {
    queueSequencerBankApply_(stagedBank, staged);
}

void CoreState::clearPendingSequencerApply() {
    clearPendingSequencerApply_();
}

bool CoreState::hasPendingSequencerApply() const {
    return sequencerDomain_.pendingApply && sequencerDomain_.pendingApply->valid;
}

uint16_t CoreState::currentSharedTrackEnabledMask() const {
    return sharedTrackEnabledMask.get();
}

uint8_t CoreState::currentSharedActiveTrack() const {
    return sharedTrackActive.get();
}

bool CoreState::setSharedTrackState(uint16_t enabledMask, uint8_t activeTrack) {
    return setSharedTrackState_(enabledMask, activeTrack, true);
}

bool CoreState::refreshSharedTrackStateFromMacroPages() {
    return refreshSharedTrackStateFromMacroPages_(true);
}

bool CoreState::refreshSharedTrackStateFromSequencer() {
    return refreshSharedTrackStateFromSequencer_(true);
}

void CoreState::noteMacroInteraction() {
    macroDomain_.lastInteractionTimestampMs = oc::time::millis();
    if (macroDomain_.lastInteractionTimestampMs == 0) {
        macroDomain_.lastInteractionTimestampMs = 1;
    }
}

persistence::PersistenceWriteStatus CoreState::recoverPersistenceFromRamAfterStorageReopen() {
    auto status = settings.saveAllStatus(
        midiSync,
        sharedTrackEnabledMask.get(),
        sharedTrackActive.get()
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    status = settings.saveDataManagerMacroShortcutLeftStatus(
        static_cast<uint8_t>(dataManager.macroShortcutLeft.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.saveDataManagerMacroShortcutRightStatus(
        static_cast<uint8_t>(dataManager.macroShortcutRight.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.saveDataManagerSeqShortcutLeftStatus(
        static_cast<uint8_t>(dataManager.seqShortcutLeft.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.saveDataManagerSeqShortcutRightStatus(
        static_cast<uint8_t>(dataManager.seqShortcutRight.get())
    );
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    status = settings.commitStatus();
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    status = macroPersistence.initStatus();
    macroDomain_.persistenceReady = status == persistence::PersistenceWriteStatus::OK;
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    status = macroPersistence.saveWorkspaceStatus(pages);
    if (status != persistence::PersistenceWriteStatus::OK) return status;
    macroDomain_.workspacePersistPending = false;
    macroDomain_.workspacePersistTimestampMs = 0;

    status = sequencerPersistence.initStatus();
    sequencerDomain_.persistenceReady = status == persistence::PersistenceWriteStatus::OK;
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    sequencer::storeActiveTrack(sequencerTracks, sequencer);
    status = sequencerPersistence.saveWorkspaceStatus(sequencerTracks, sequencer);
    if (status != persistence::PersistenceWriteStatus::OK) return status;

    sharedTrackPersistPending_ = false;
    sharedTrackPersistTimestampMs_ = 0;
    return persistence::PersistenceWriteStatus::OK;
}

void CoreState::queueSequencerApply_(const sequencer::SequencerState& staged, bool merge) {
    CoreStateLifecycle::queuePendingSequencerApply(*this, staged, merge);
}

void CoreState::queueSequencerBankApply_(
    const sequencer::SequencerTrackBankState& stagedBank,
    const sequencer::SequencerState& staged
) {
    CoreStateLifecycle::queuePendingSequencerBankApply(*this, stagedBank, staged);
}

void CoreState::requestMacroWorkspacePersist_() {
    if (!macroDomain_.persistenceReady) return;

    macroDomain_.workspacePersistPending = true;
    macroDomain_.workspacePersistTimestampMs = oc::time::millis();
    if (macroDomain_.workspacePersistTimestampMs == 0) {
        macroDomain_.workspacePersistTimestampMs = 1;
    }
}

void CoreState::persistMacroWorkspaceNow_() {
    if (!macroDomain_.persistenceReady) return;
    const auto status = macroPersistence.saveWorkspaceStatus(pages);
    if (status == persistence::PersistenceWriteStatus::OK) {
        macroDomain_.workspacePersistPending = false;
        macroDomain_.workspacePersistTimestampMs = 0;
        return;
    }

    OC_LOG_WARN("[CoreState] Failed to persist macro workspace: {}",
                persistence::persistenceWriteStatusLabel(status));
    if (status == persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE) {
        macroDomain_.workspacePersistPending = true;
        macroDomain_.workspacePersistTimestampMs = oc::time::millis();
        if (macroDomain_.workspacePersistTimestampMs == 0) {
            macroDomain_.workspacePersistTimestampMs = 1;
        }
    } else {
        macroDomain_.workspacePersistPending = false;
        macroDomain_.workspacePersistTimestampMs = 0;
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

void CoreState::requestSharedTrackPersist_() {
    sharedTrackPersistPending_ = true;
    sharedTrackPersistTimestampMs_ = oc::time::millis();
    if (sharedTrackPersistTimestampMs_ == 0) {
        sharedTrackPersistTimestampMs_ = 1;
    }
}

void CoreState::persistSharedTrackState_() {
    if (!sharedTrackPersistPending_) return;

    const auto persistStatus = settings.saveSharedTrackStateStatus(
        sharedTrackEnabledMask.get(),
        sharedTrackActive.get()
    );
    if (persistStatus == persistence::PersistenceWriteStatus::OK) {
        sharedTrackPersistPending_ = false;
        sharedTrackPersistTimestampMs_ = 0;
        return;
    }

    OC_LOG_WARN("[CoreState] Failed to persist shared track state: {}",
                persistence::persistenceWriteStatusLabel(persistStatus));
    if (persistStatus == persistence::PersistenceWriteStatus::STORAGE_UNAVAILABLE) {
        sharedTrackPersistPending_ = true;
        sharedTrackPersistTimestampMs_ = oc::time::millis();
        if (sharedTrackPersistTimestampMs_ == 0) {
            sharedTrackPersistTimestampMs_ = 1;
        }
    } else {
        sharedTrackPersistPending_ = false;
        sharedTrackPersistTimestampMs_ = 0;
    }
}

void CoreState::clearPendingSequencerApply_() {
    CoreStateLifecycle::clearPendingSequencerApply(*this);
}

bool CoreState::refreshSharedTrackStateFromMacroPages_(bool persist) {
    const auto result = shared::SharedTrackCoordinator::refreshFromMacroPages(sharedTrackRefs(*this));
    if (result.changed && persist) {
        requestSharedTrackPersist_();
    }
    return result.changed;
}

bool CoreState::refreshSharedTrackStateFromSequencer_(bool persist) {
    const auto result = shared::SharedTrackCoordinator::refreshFromSequencer(sharedTrackRefs(*this));
    if (result.changed && persist) {
        requestSharedTrackPersist_();
    }
    return result.changed;
}

bool CoreState::setSharedTrackState_(uint16_t enabledMask, uint8_t activeTrack, bool persist) {
    const auto result = shared::SharedTrackCoordinator::apply(
        sharedTrackRefs(*this),
        enabledMask,
        activeTrack
    );
    if (result.changed && persist) {
        requestSharedTrackPersist_();
    }
    return result.changed;
}

}  // namespace core::state

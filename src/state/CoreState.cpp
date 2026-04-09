#include "state/CoreState.hpp"

#include <new>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "macro/MacroPersistenceWorkflow.hpp"
#include "macro/MacroWorkflow.hpp"
#include "sequencer/SequencerPersistenceWorkflow.hpp"
#include "sequencer/SequencerSnapshotOps.hpp"
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

core::app::ExtmemUniquePtr<sequencer::SequencerTrackBankState> createSequencerTrackBankState() {
    return core::app::makeExtmemUnique<sequencer::SequencerTrackBankState>();
}

}  // namespace

SequencerDomainState::SequencerDomainState(oc::interface::IStorage& workspaceStorage,
                                           oc::interface::IStorage& patternLibraryStorage,
                                           oc::interface::IStorage& setLibraryStorage)
    : editor{}
    , tracks(createSequencerTrackBankState())
    , persistence(workspaceStorage, patternLibraryStorage, setLibraryStorage)
    , pendingApply(nullptr) {
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
    , sequencer(sequencerDomain_.editor)
    , sequencerTracks(*sequencerDomain_.tracks)
    , sequencerPersistence(sequencerDomain_.persistence)
    , overlays(systemUi_->overlays)
    , activeView(systemUi_->activeView)
    , viewSelector(systemUi_->viewSelector)
    , statusBar(systemUi_->statusBar)
    , midiSync(systemUi_->midiSync)
    , globalSettings(systemUi_->globalSettings)
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
    return sequencerDomain_.pendingApply && sequencerDomain_.pendingApply->valid;
}

void CoreState::queueSequencerApply_(const sequencer::SequencerState& staged, bool merge) {
    CoreStateLifecycle::queuePendingSequencerApply(*this, staged, merge);
}

void CoreState::queueSequencerBankApply_(const sequencer::SequencerTrackBankSnapshot& staged) {
    if (!sequencerDomain_.pendingApply) return;
    sequencerDomain_.pendingApply->bankSnapshot = staged;
    sequencerDomain_.pendingApply->anchorPlayhead = sequencer.playheadStep.get();
    sequencerDomain_.pendingApply->merge = false;
    sequencerDomain_.pendingApply->fullBank = true;
    sequencerDomain_.pendingApply->valid = true;
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

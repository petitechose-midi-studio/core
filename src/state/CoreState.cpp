#include "state/CoreState.hpp"

#include <new>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "macro/MacroPersistenceWorkflow.hpp"
#include "macro/MacroWorkflow.hpp"
#include "sequencer/SequencerPersistenceWorkflow.hpp"
#include "sequencer/SequencerTrackBankOps.hpp"

namespace core::state {

namespace {

constexpr uint16_t kSharedTrackMaskAll =
    static_cast<uint16_t>((1U << sequencer::SequencerTrackBankState::TRACK_COUNT) - 1U);

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

uint16_t sanitizeSharedTrackMask(uint16_t enabledMask) {
    const uint16_t sanitized = static_cast<uint16_t>(enabledMask & kSharedTrackMaskAll);
    return sanitized == 0 ? 0x0001 : sanitized;
}

uint8_t firstEnabledSharedTrack(uint16_t enabledMask) {
    for (uint8_t i = 0; i < sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((enabledMask & static_cast<uint16_t>(1U << i)) != 0) {
            return i;
        }
    }
    return 0;
}

uint8_t sanitizeSharedActiveTrack(uint16_t enabledMask, uint8_t activeTrack) {
    const uint8_t clamped =
        sequencer::SequencerTrackBankState::clampTrackIndex(activeTrack);
    return (enabledMask & static_cast<uint16_t>(1U << clamped)) != 0
        ? clamped
        : firstEnabledSharedTrack(enabledMask);
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
    macroDomain_.workspacePersistPending = false;
    macroDomain_.workspacePersistTimestampMs = 0;
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

void CoreState::requestSharedTrackPersist_() {
    sharedTrackPersistPending_ = true;
    sharedTrackPersistTimestampMs_ = oc::time::millis();
    if (sharedTrackPersistTimestampMs_ == 0) {
        sharedTrackPersistTimestampMs_ = 1;
    }
}

void CoreState::persistSharedTrackState_() {
    if (!sharedTrackPersistPending_) return;

    sharedTrackPersistPending_ = false;
    sharedTrackPersistTimestampMs_ = 0;

    const auto persistStatus = settings.saveSharedTrackStateStatus(
        sharedTrackEnabledMask.get(),
        sharedTrackActive.get()
    );
    if (persistStatus != persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[CoreState] Failed to persist shared track state: {}",
                    persistence::persistenceWriteStatusLabel(persistStatus));
    }
}

void CoreState::clearPendingSequencerApply_() {
    CoreStateLifecycle::clearPendingSequencerApply(*this);
}

bool CoreState::refreshSharedTrackStateFromMacroPages_(bool persist) {
    return setSharedTrackState_(
        pages.currentTrackEnabledMask(),
        pages.currentActiveTrack(),
        persist
    );
}

bool CoreState::refreshSharedTrackStateFromSequencer_(bool persist) {
    return setSharedTrackState_(
        sequencerTracks.currentEnabledMask(),
        sequencerTracks.activeTrackIndex(),
        persist
    );
}

bool CoreState::setSharedTrackState_(uint16_t enabledMask, uint8_t activeTrack, bool persist) {
    const uint16_t sanitizedMask = sanitizeSharedTrackMask(enabledMask);
    const uint8_t sanitizedActive = sanitizeSharedActiveTrack(sanitizedMask, activeTrack);
    const uint16_t previousMask = sharedTrackEnabledMask.get();
    const uint8_t previousActive = sharedTrackActive.get();

    if (previousMask != sanitizedMask) {
        sharedTrackEnabledMask.set(sanitizedMask);
    }

    pages.syncSharedTrackState(sanitizedMask, sanitizedActive);

    if (sequencerTracks.currentEnabledMask() != sanitizedMask) {
        sequencerTracks.enabledMaskSignal().set(sanitizedMask);
    }

    if (sequencerTracks.activeTrackIndex() != sanitizedActive) {
        sequencer::switchActiveTrack(sequencerTracks, sequencer, sanitizedActive);
    }

    if (previousActive != sanitizedActive) {
        sharedTrackActive.set(sanitizedActive);
    }

    const bool changed = previousMask != sharedTrackEnabledMask.get() ||
                         previousActive != sharedTrackActive.get();
    if (!changed) {
        return false;
    }
    if (persist) {
        requestSharedTrackPersist_();
    }

    return true;
}

}  // namespace core::state

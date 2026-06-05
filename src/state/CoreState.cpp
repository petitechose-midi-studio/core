#include "state/CoreState.hpp"

#include <new>
#include <cstdio>
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
#include "midi/MidiUtils.hpp"
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

int32_t sequencerHistoryValueForProperty(
    const sequencer::SequencerHistoryPatternSnapshot& snapshot,
    uint8_t step,
    sequencer::StepProperty property
) {
    if (step >= sequencer::SequencerPatternState::MAX_STEPS) {
        return 0;
    }

    switch (property) {
        case sequencer::StepProperty::NOTE:
            return snapshot.flat.note[step];
        case sequencer::StepProperty::VELOCITY:
            return snapshot.flat.velocity[step];
        case sequencer::StepProperty::GATE:
            return snapshot.flat.gate[step];
        case sequencer::StepProperty::NUDGE:
            return snapshot.flat.nudge[step];
        case sequencer::StepProperty::PROBABILITY:
            return snapshot.flat.probability[step];
        default:
            return 0;
    }
}

sequencer::SequencerHistoryDescriptor makeStepPropertyHistoryDescriptor(
    uint8_t track,
    uint8_t step,
    sequencer::StepProperty property,
    const sequencer::SequencerHistoryPatternSnapshot& before,
    const sequencer::SequencerHistoryPatternSnapshot& after
) {
    return sequencer::SequencerHistoryDescriptor{
        .kind = sequencer::SequencerHistoryActionKind::StepPropertyEdit,
        .trackIndex = track,
        .stepIndex = step,
        .property = property,
        .hasValue = true,
        .beforeValue = sequencerHistoryValueForProperty(before, step, property),
        .afterValue = sequencerHistoryValueForProperty(after, step, property),
    };
}

const char* historyDirectionLabel(sequencer::SequencerHistoryDirection direction) {
    return direction == sequencer::SequencerHistoryDirection::Redo ? "REDO" : "UNDO";
}

const char* historyPropertyLabel(sequencer::StepProperty property) {
    switch (property) {
        case sequencer::StepProperty::NOTE:
            return "Note";
        case sequencer::StepProperty::VELOCITY:
            return "Velocity";
        case sequencer::StepProperty::GATE:
            return "Gate";
        case sequencer::StepProperty::NUDGE:
            return "Nudge";
        case sequencer::StepProperty::PROBABILITY:
            return "Probability";
        default:
            return "Property";
    }
}

const char* historyActionLabel(sequencer::SequencerHistoryActionKind kind) {
    switch (kind) {
        case sequencer::SequencerHistoryActionKind::StepToggle:
            return "Step Toggle";
        case sequencer::SequencerHistoryActionKind::StepPropertyEdit:
            return "Step Property";
        case sequencer::SequencerHistoryActionKind::StepEdit:
            return "Step Edit";
        case sequencer::SequencerHistoryActionKind::QuickControls:
            return "Quick Controls";
        case sequencer::SequencerHistoryActionKind::PageStructure:
            return "Page Structure";
        case sequencer::SequencerHistoryActionKind::TrackStructure:
            return "Track Structure";
        case sequencer::SequencerHistoryActionKind::FullBank:
            return "Sequencer Set";
        case sequencer::SequencerHistoryActionKind::PatternEdit:
        default:
            return "Pattern Edit";
    }
}

void formatHistoryValue(
    char* buffer,
    size_t bufferSize,
    sequencer::StepProperty property,
    int32_t value
) {
    if (!buffer || bufferSize == 0) return;

    if (property == sequencer::StepProperty::NOTE) {
        core::midi::formatNoteName(
            buffer,
            bufferSize,
            static_cast<uint8_t>(value < 0 ? 0 : (value > 127 ? 127 : value))
        );
        return;
    }

    if (property == sequencer::StepProperty::GATE) {
        std::snprintf(buffer, bufferSize, "%ld%%", static_cast<long>(value));
        return;
    }

    if (property == sequencer::StepProperty::NUDGE) {
        std::snprintf(buffer, bufferSize, "%+ld", static_cast<long>(value));
        return;
    }

    std::snprintf(buffer, bufferSize, "%ld", static_cast<long>(value));
}

void formatHistoryStructureValue(
    char* buffer,
    size_t bufferSize,
    sequencer::SequencerHistoryActionKind kind,
    int32_t value
) {
    if (!buffer || bufferSize == 0) return;

    const char* unit = kind == sequencer::SequencerHistoryActionKind::TrackStructure
        ? "track"
        : "page";
    std::snprintf(
        buffer,
        bufferSize,
        "%ld %s%s",
        static_cast<long>(value),
        unit,
        value == 1 ? "" : "s"
    );
}

void showSequencerHistoryFeedback(
    sequencer::SequencerState& sequencerState,
    const sequencer::SequencerHistoryApplyResult& result,
    uint32_t nowMs
) {
    if (!result.applied) return;

    const auto& descriptor = result.descriptor;
    char line1[sequencer::SequencerHistoryFeedbackState::LINE_SIZE]{};
    char line2[sequencer::SequencerHistoryFeedbackState::LINE_SIZE]{};
    char line3[sequencer::SequencerHistoryFeedbackState::LINE_SIZE]{};

    const char* direction = historyDirectionLabel(result.direction);
    if (descriptor.trackIndex != sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        std::snprintf(
            line1,
            sizeof(line1),
            "%s T%02u",
            direction,
            static_cast<unsigned>(descriptor.trackIndex + 1U)
        );
    } else {
        std::snprintf(line1, sizeof(line1), "%s", direction);
    }

    if (descriptor.stepIndex != sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        std::snprintf(
            line2,
            sizeof(line2),
            "Step %02u %s",
            static_cast<unsigned>(descriptor.stepIndex + 1U),
            historyPropertyLabel(descriptor.property)
        );
    } else {
        std::snprintf(line2, sizeof(line2), "%s", historyActionLabel(descriptor.kind));
    }

    if (descriptor.hasValue) {
        const int32_t fromValue = result.direction == sequencer::SequencerHistoryDirection::Undo
            ? descriptor.afterValue
            : descriptor.beforeValue;
        const int32_t toValue = result.direction == sequencer::SequencerHistoryDirection::Undo
            ? descriptor.beforeValue
            : descriptor.afterValue;

        if (descriptor.kind == sequencer::SequencerHistoryActionKind::StepToggle) {
            std::snprintf(
                line3,
                sizeof(line3),
                "%s -> %s",
                fromValue != 0 ? "On" : "Off",
                toValue != 0 ? "On" : "Off"
            );
        } else if (descriptor.kind == sequencer::SequencerHistoryActionKind::PageStructure ||
                   descriptor.kind == sequencer::SequencerHistoryActionKind::TrackStructure) {
            char fromText[14]{};
            char toText[14]{};
            formatHistoryStructureValue(
                fromText,
                sizeof(fromText),
                descriptor.kind,
                fromValue
            );
            formatHistoryStructureValue(
                toText,
                sizeof(toText),
                descriptor.kind,
                toValue
            );
            std::snprintf(line3, sizeof(line3), "%s -> %s", fromText, toText);
        } else {
            char fromText[12]{};
            char toText[12]{};
            formatHistoryValue(fromText, sizeof(fromText), descriptor.property, fromValue);
            formatHistoryValue(toText, sizeof(toText), descriptor.property, toValue);
            std::snprintf(line3, sizeof(line3), "%s -> %s", fromText, toText);
        }
    } else {
        std::snprintf(line3, sizeof(line3), "Applied");
    }

    sequencerState.historyFeedback.show(line1, line2, line3, nowMs);
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
    commitSequencerPatternHistoryCoalescing();
    CoreStateLifecycle::flush(*this);
}

void CoreState::flushAutoPersist() {
    CoreStateLifecycle::flushAutoPersist(*this);
}

void CoreState::resetStandaloneTransientUi() {
    commitSequencerPatternHistoryCoalescing();
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
    sequencer::SequencerHistoryPatternSnapshot after,
    sequencer::SequencerHistoryDescriptor descriptor
) {
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();
    uint8_t targetTrack = activeTrack;
    if (descriptor.trackIndex == sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        descriptor.trackIndex = activeTrack;
    } else {
        targetTrack = sequencer::SequencerTrackBankState::clampTrackIndex(descriptor.trackIndex);
        descriptor.trackIndex = targetTrack;
    }

    if (!sequencerHistory.recordPattern(
            targetTrack,
            std::move(before),
            std::move(after),
            descriptor
        )) {
        return false;
    }

    sequencer::storeActiveTrack(sequencerTracks, sequencer);
    refreshSharedTrackStateFromSequencer();
    persistSequencerWorkspace_();
    return true;
}

bool CoreState::recordSequencerBankHistory(
    sequencer::SequencerHistoryTrackBankSnapshot before,
    sequencer::SequencerHistoryTrackBankSnapshot after,
    sequencer::SequencerHistoryDescriptor descriptor
) {
    if (!sequencerHistory.recordFullBank(
            std::move(before),
            std::move(after),
            descriptor
        )) {
        return false;
    }

    refreshSharedTrackStateFromSequencer();
    persistSequencerWorkspace_();
    return true;
}

bool CoreState::recordSequencerBankHistory(
    sequencer::SequencerHistoryFullBankChangePtr change
) {
    if (!sequencerHistory.recordFullBank(std::move(change))) {
        return false;
    }

    refreshSharedTrackStateFromSequencer();
    persistSequencerWorkspace_();
    return true;
}

bool CoreState::beginOrContinueSequencerPatternHistoryCoalescing(
    uint8_t step,
    sequencer::StepProperty property,
    uint32_t nowMs
) {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    const uint8_t activeTrack = sequencerTracks.activeTrackIndex();

    if (pending.matches(activeTrack, step, property)) {
        pending.lastTouchedMs = nowMs;
        return true;
    }

    if (pending.pending) {
        commitSequencerPatternHistoryCoalescing();
    }

    sequencer::SequencerHistoryPatternSnapshot before;
    if (!sequencer::captureHistorySnapshot(sequencer, before)) {
        pending.clear();
        return false;
    }

    pending.clear();
    pending.pending = true;
    pending.activeTrack = activeTrack;
    pending.step = step;
    pending.property = property;
    pending.lastTouchedMs = nowMs;
    pending.before = std::move(before);
    return true;
}

bool CoreState::commitSequencerPatternHistoryCoalescing() {
    auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) {
        return false;
    }

    const uint8_t targetTrack = pending.activeTrack;
    const uint8_t targetStep = pending.step;
    const auto targetProperty = pending.property;
    sequencer::SequencerHistoryPatternSnapshot before = std::move(pending.before);
    pending.clear();

    sequencer::SequencerHistoryPatternSnapshot after;
    if (!sequencer::captureHistorySnapshot(sequencerTracks, sequencer, targetTrack, after)) {
        return false;
    }

    auto descriptor = makeStepPropertyHistoryDescriptor(
        targetTrack,
        targetStep,
        targetProperty,
        before,
        after
    );

    return recordSequencerPatternHistory(
        std::move(before),
        std::move(after),
        descriptor
    );
}

bool CoreState::updateSequencerPatternHistoryCoalescing(uint32_t nowMs) {
    const auto& pending = sequencerDomain_.coalescedPatternHistory;
    if (!pending.pending) {
        return false;
    }

    if (static_cast<uint32_t>(nowMs - pending.lastTouchedMs) <
        SequencerDomainState::COALESCED_PATTERN_HISTORY_IDLE_MS) {
        return false;
    }

    return commitSequencerPatternHistoryCoalescing();
}

bool CoreState::hasPendingSequencerPatternHistoryCoalescing() const {
    return sequencerDomain_.coalescedPatternHistory.pending;
}

bool CoreState::undoSequencerHistory() {
    commitSequencerPatternHistoryCoalescing();

    const auto result = sequencerHistory.undoWithResult(sequencerTracks, sequencer);
    if (!result.applied) {
        return false;
    }

    showSequencerHistoryFeedback(sequencer, result, oc::time::millis());
    refreshSharedTrackStateFromSequencer();
    persistSequencerWorkspace_();
    return true;
}

bool CoreState::redoSequencerHistory() {
    commitSequencerPatternHistoryCoalescing();

    const auto result = sequencerHistory.redoWithResult(sequencerTracks, sequencer);
    if (!result.applied) {
        return false;
    }

    showSequencerHistoryFeedback(sequencer, result, oc::time::millis());
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
    commitSequencerPatternHistoryCoalescing();
    CoreStateLifecycle::queuePendingSequencerApply(*this, staged, merge);
}

void CoreState::queueSequencerBankApply_(
    const sequencer::SequencerTrackBankState& stagedBank,
    const sequencer::SequencerState& staged
) {
    commitSequencerPatternHistoryCoalescing();
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
    commitSequencerPatternHistoryCoalescing();

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

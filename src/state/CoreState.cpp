#include "state/CoreState.hpp"

#include <new>
#include <cstdio>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#if OC_ENABLE_STATS
#include "diagnostics/MemoryFootprintReporter.hpp"
#endif

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
#include "sequencer/SequencerCcLanePatternOps.hpp"
#include "sequencer/SequencerContentViewOps.hpp"
#include "sequencer/SequencerStructureHistory.hpp"
#include "sequencer/SequencerTrackBankOps.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"

namespace core::state {

namespace {

[[noreturn]] FLASHMEM void failCoreStateAllocation(const char* label) {
    OC_LOG_ERROR("[CoreState] Failed to allocate {}", label);
    while (true) {}
}

FLASHMEM SequencerDomainState::PendingApply* createPendingApply() {
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    void* memory = extmem_malloc(sizeof(SequencerDomainState::PendingApply));
    if (!memory) return nullptr;
#if OC_ENABLE_STATS
    core::diagnostics::trackExtmemAllocation(memory);
#endif
    return new(memory) SequencerDomainState::PendingApply();
#else
    return new SequencerDomainState::PendingApply();
#endif
}

FLASHMEM core::app::ExtmemUniquePtr<UiSystemState> createUiSystemState() {
    auto state = core::app::makeExtmemUnique<UiSystemState>();
    if (!state) failCoreStateAllocation("UI system state");
    return state;
}

FLASHMEM core::app::ExtmemUniquePtr<project::ProjectHistoryCoordinator>
createProjectHistoryCoordinator() {
    auto history = core::app::makeExtmemUnique<project::ProjectHistoryCoordinator>();
    if (!history) failCoreStateAllocation("Project history coordinator");
    return history;
}

FLASHMEM core::app::ExtmemUniquePtr<project::ProjectSettingsHistoryService>
createProjectSettingsHistory() {
    auto history = core::app::makeExtmemUnique<
        project::ProjectSettingsHistoryService
    >();
    if (!history) failCoreStateAllocation("Project settings history");
    return history;
}

FLASHMEM core::app::ExtmemUniquePtr<project::ProjectTrackHistoryService>
createProjectTrackHistory() {
    auto history = core::app::makeExtmemUnique<
        project::ProjectTrackHistoryService
    >();
    if (!history) failCoreStateAllocation("Project Track history");
    return history;
}

FLASHMEM core::app::ExtmemUniquePtr<project::ProjectTrackState>
createProjectTrackState() {
    auto tracks = core::app::makeExtmemUnique<project::ProjectTrackState>();
    if (!tracks) failCoreStateAllocation("Project Track state");
    return tracks;
}

FLASHMEM core::app::ExtmemUniquePtr<sequencer::SequencerState> createSequencerEditorState() {
    auto state = core::app::makeExtmemUnique<sequencer::SequencerState>();
    if (!state) failCoreStateAllocation("sequencer editor state");
    return state;
}

FLASHMEM core::app::ExtmemUniquePtr<sequencer::SequencerTrackBankState> createSequencerTrackBankState() {
    auto state = core::app::makeExtmemUnique<sequencer::SequencerTrackBankState>();
    if (!state) failCoreStateAllocation("sequencer track bank");
    return state;
}

FLASHMEM void discardGlobalRedoBranches(void* context) {
    if (context == nullptr) return;
    auto& state = *static_cast<CoreState*>(context);
    state.macroHistory.discardRedoBranch();
    state.sequencerHistory.discardRedoBranch();
    state.projectTrackHistory.discardRedoBranch();
    state.projectSettingsHistory.discardRedoBranch();
}

constexpr uint32_t nextNonZeroRuntimeRevision(uint32_t current) {
    const uint32_t next = current + 1U;
    return next == 0U ? 1U : next;
}

}  // namespace

FLASHMEM MacroDomainState::MacroDomainState(oc::interface::IStorage& libraryStorage)
    : runtime(core::app::makeExtmemUnique<MacroState>())
    , pages(core::app::makeExtmemUnique<macro::MacroPagesState>())
    , persistence(libraryStorage) {
    if (!runtime) failCoreStateAllocation("macro runtime state");
    if (!pages) failCoreStateAllocation("macro pages state");
}

FLASHMEM MacroDomainState::~MacroDomainState() = default;

FLASHMEM SequencerDomainState::SequencerDomainState(
    oc::interface::IStorage& patternLibraryStorage,
    oc::interface::IStorage& setLibraryStorage
)
    : editor(createSequencerEditorState())
    , tracks(createSequencerTrackBankState())
    , history(core::app::makeExtmemUnique<sequencer::SequencerHistoryService>())
    , persistence(patternLibraryStorage, setLibraryStorage)
    , pendingApply(nullptr) {
    if (!history) failCoreStateAllocation("sequencer history service");
}

FLASHMEM SequencerDomainState::~SequencerDomainState() = default;

FLASHMEM void SequencerDomainState::PendingApplyDeleter::operator()(PendingApply* ptr) const noexcept {
    if (!ptr) return;
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    ptr->~PendingApply();
#if OC_ENABLE_STATS
    core::diagnostics::trackExtmemFree(ptr);
#endif
    extmem_free(ptr);
#else
    delete ptr;
#endif
}

FLASHMEM CoreState::CoreState(oc::interface::IStorage& settingsStorage,
                              oc::interface::IStorage& macroLibraryStorage,
                              oc::interface::IStorage& sequencerPatternLibraryStorage,
                              oc::interface::IStorage& sequencerSetLibraryStorage)
    : macroDomain_(macroLibraryStorage)
    , sequencerDomain_(sequencerPatternLibraryStorage,
                       sequencerSetLibraryStorage)
    , projectTracks_(createProjectTrackState())
    , projectTrackHistory_(createProjectTrackHistory())
    , projectSettingsHistory_(createProjectSettingsHistory())
    , projectHistory_(createProjectHistoryCoordinator())
    , systemUi_(createUiSystemState())
    , settings(settingsStorage)
    , macros(*macroDomain_.runtime)
    , pages(*macroDomain_.pages)
    , macroHistory(macroDomain_.history)
    , macroRuntimeOwnerRevision(macroDomain_.runtimeOwnerRevision)
    , configRevision(macroDomain_.configRevision)
    , macroPersistence(macroDomain_.persistence)
    , sequencer(*sequencerDomain_.editor)
    , sequencerTracks(*sequencerDomain_.tracks)
    , sequencerHistory(*sequencerDomain_.history)
    , sequencerTrackActivations(sequencerDomain_.trackActivations)
    , sequencerRuntimeProjectRevision(sequencerDomain_.runtimeProjectRevision)
    , sequencerPersistence(sequencerDomain_.persistence)
    , project(project_)
    , projectTracks(*projectTracks_)
    , projectTrackHistory(*projectTrackHistory_)
    , projectSettingsHistory(*projectSettingsHistory_)
    , projectHistory(*projectHistory_)
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
    , deviceSettings(systemUi_->deviceSettings)
    , sequencerSettings(systemUi_->sequencerSettings)
    , patternPitchSettings(systemUi_->patternPitchSettings)
    , dataManager(systemUi_->dataManager)
    , macroEdit(systemUi_->macroEdit)
    , macroUi(systemUi_->macroUi)
    , projectNavigation(systemUi_->projectNavigation)
    , projectTrackEditor(systemUi_->projectTrackEditor) {
    projectHistory.setBranchInvalidatedCallback(
        this,
        &discardGlobalRedoBranches
    );
    macroHistory.setProjectHistoryEventSink(&projectHistory.eventSink());
    sequencerHistory.setProjectHistoryEventSink(&projectHistory.eventSink());
    projectTrackHistory.setProjectHistoryEventSink(&projectHistory.eventSink());
    projectSettingsHistory.setProjectHistoryEventSink(
        &projectHistory.eventSink()
    );
    sequencerDomain_.pendingApply.reset(createPendingApply());
    if (!sequencerDomain_.pendingApply) {
        failCoreStateAllocation("sequencer pending apply buffer");
    }
    CoreStateBootstrap::initialize(*this);
}

FLASHMEM CoreState::~CoreState() = default;

void CoreState::update() {
    CoreStateLifecycle::update(*this);
}

FLASHMEM void CoreState::factoryReset() {
    CoreStateLifecycle::factoryReset(*this);
}

FLASHMEM void CoreState::flush() {
    commitSequencerPatternHistoryCoalescing();
    CoreStateLifecycle::flush(*this);
}

FLASHMEM void CoreState::flushProjectMutationCoalescing() {
    CoreStateLifecycle::flushProjectMutationCoalescing(*this);
}

FLASHMEM void CoreState::resetStandaloneTransientUi() {
    commitSequencerPatternHistoryCoalescing();
    CoreStateLifecycle::resetStandaloneTransientUi(*this);
}

FLASHMEM void CoreState::resetMusicalProject() {
    commitSequencerPatternHistoryCoalescing();
    CoreStateLifecycle::resetMusicalProject(*this);
}

FLASHMEM void CoreState::requestMacroRuntimeOwnerActivation() {
    macroRuntimeOwnerRevision.set(nextNonZeroRuntimeRevision(macroRuntimeOwnerRevision.get()));
}

FLASHMEM void CoreState::requestSequencerRuntimeProjectReset() {
    sequencerTrackActivations.reset();
    sequencerRuntimeProjectRevision.set(
        nextNonZeroRuntimeRevision(sequencerRuntimeProjectRevision.get())
    );
}

void CoreState::markMacroValueEdited(uint8_t index) {
    if (index >= MACRO_COUNT) return;
    if (macroDomain_.mutationCoalescer) {
        macroDomain_.mutationCoalescer->markChanged();
        return;
    }
    markProjectMutated();
}

bool CoreState::setMacroValueWithHistory(uint8_t index, float value) {
    if (index >= MACRO_COUNT) return false;
    const macro::MacroAutomationSlotAddress address{
        .track = pages.currentActiveTrack(),
        .page = pages.currentActivePage(),
        .macro = index,
    };
    auto& pending = macroDomain_.coalescedValueHistory;
    if (pending.pending &&
        !macro::macroAutomationAddressEquals(pending.address, address)) {
        flushMacroValueHistoryCoalescing();
    }
    if (!macroHistory.setMacroValueCoalesced(pages, address, value)) {
        return false;
    }
    pending.pending = true;
    pending.address = address;
    pending.lastTouchedMs = oc::time::millis();
    markMacroValueEdited(index);
    return true;
}

bool CoreState::takeMacroManualControlWithHistory(
    uint8_t index,
    float value,
    bool coalesceValue
) {
    if (index >= MACRO_COUNT) return false;
    const macro::MacroAutomationSlotAddress address{
        .track = pages.currentActiveTrack(),
        .page = pages.currentActivePage(),
        .macro = index,
    };
    auto& pending = macroDomain_.coalescedValueHistory;
    if (!coalesceValue ||
        (pending.pending &&
         !macro::macroAutomationAddressEquals(pending.address, address))) {
        flushMacroValueHistoryCoalescing();
    }
    const float beforeBase = pages.pageData(address.track, address.page)
        .values[address.macro];
    if (!macroHistory.setManualOverrideCoalesced(
            pages,
            macroUi.manualOverrides,
            address,
            value,
            coalesceValue
        )) {
        return false;
    }
    if (coalesceValue) {
        pending.pending = true;
        pending.address = address;
        pending.lastTouchedMs = oc::time::millis();
    }
    if (beforeBase != pages.pageData(address.track, address.page)
                          .values[address.macro]) {
        markMacroValueEdited(index);
    }
    return true;
}

bool CoreState::resumeMacroComputedSourcesWithHistory(uint8_t index) {
    if (index >= MACRO_COUNT) return false;
    flushMacroValueHistoryCoalescing();
    const macro::MacroAutomationSlotAddress address{
        .track = pages.currentActiveTrack(),
        .page = pages.currentActivePage(),
        .macro = index,
    };
    return macroHistory.resumeManualOverride(
        pages,
        macroUi.manualOverrides,
        address
    );
}

void CoreState::updateMacroValueHistoryCoalescing(uint32_t nowMs) {
    auto& pending = macroDomain_.coalescedValueHistory;
    if (!pending.pending ||
        (nowMs - pending.lastTouchedMs) <
            MacroDomainState::COALESCED_VALUE_HISTORY_IDLE_MS) {
        return;
    }
    flushMacroValueHistoryCoalescing();
}

FLASHMEM void CoreState::flushMacroValueHistoryCoalescing() {
    macroHistory.endCoalescing();
    macroDomain_.coalescedValueHistory.clear();
}

FLASHMEM void CoreState::markProjectMutated() {
    ++project.metadata.modifiedCounter;
    if (project.metadata.modifiedCounter == 0) {
        project.metadata.modifiedCounter = 1;
    }
    project.metadata.dirty = true;
    requestProjectSessionSave_();
    projectNavigation.notifyContentChanged();
}

FLASHMEM void CoreState::requestProjectSessionSave() {
    requestProjectSessionSave_();
}

FLASHMEM void CoreState::acknowledgeProjectSessionSave(uint32_t savedModifiedCounter) {
    if (project.metadata.modifiedCounter != savedModifiedCounter) {
        requestProjectSessionSave_();
        return;
    }

    projectSessionSavePending_ = false;
    projectSessionSaveTimestampMs_ = 0;
}

bool CoreState::hasPendingProjectSessionSave() const {
    return projectSessionSavePending_;
}

uint32_t CoreState::projectSessionSaveTimestampMs() const {
    return projectSessionSaveTimestampMs_;
}

bool CoreState::hasPendingProjectMutationCoalescing() const {
    const bool macroPending =
        macroDomain_.mutationCoalescer && macroDomain_.mutationCoalescer->hasPendingChanges();
    const bool sequencerPending =
        sequencerDomain_.mutationCoalescer &&
        sequencerDomain_.mutationCoalescer->hasPendingChanges();
    return macroPending || sequencerPending ||
           hasPendingSequencerPatternHistoryCoalescing();
}

bool CoreState::hasPendingProjectTransaction() const {
    return hasPendingProjectMutationCoalescing() ||
           macroHistory.hasPendingModulatorAuditionTransaction(pages) ||
           projectTrackHistory.hasPendingGesture();
}

bool CoreState::isMacroPersistenceReady() const {
    return macroDomain_.persistenceReady;
}

bool CoreState::isSequencerPersistenceReady() const {
    return sequencerDomain_.persistenceReady;
}

FLASHMEM void CoreState::markSequencerProjectMutated() {
    markSequencerProjectMutated_();
}

}  // namespace core::state

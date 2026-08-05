#include "state/CoreState.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>

#include "diagnostics/StorageQualificationProbe.hpp"
#include "macro/MacroWorkflow.hpp"
#include "midi/MidiUtils.hpp"
#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/shared/SharedTrackCoordinator.hpp"

namespace core::state {

namespace {

using StorageQualificationPhase =
    core::diagnostics::storage_qualification::PhaseKind;

FLASHMEM void recordProjectSaveToken(
    StorageQualificationPhase phase,
    const project::ProjectSaveToken& token,
    uint8_t result,
    uint32_t flags
) {
    core::diagnostics::storage_qualification::recordSaveToken(
        phase,
        token.session.bootGeneration,
        token.session.sessionEpoch,
        token.mutationEpoch,
        token.requestId,
        token.modifiedCounter,
        result,
        flags
    );
}

[[noreturn]] FLASHMEM void failCoreStateAllocation(const char* label) {
    OC_LOG_ERROR("[CoreState] Failed to allocate {}", label);
    while (true) {}
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
    auto history = core::app::makeExtmemUnique<project::ProjectSettingsHistoryService>();
    if (!history) failCoreStateAllocation("Project settings history");
    return history;
}

FLASHMEM core::app::ExtmemUniquePtr<project::ProjectTrackHistoryService>
createProjectTrackHistory() {
    auto history = core::app::makeExtmemUnique<project::ProjectTrackHistoryService>();
    if (!history) failCoreStateAllocation("Project Track history");
    return history;
}

FLASHMEM core::app::ExtmemUniquePtr<project::ProjectTrackState> createProjectTrackState() {
    auto tracks = core::app::makeExtmemUnique<project::ProjectTrackState>();
    if (!tracks) failCoreStateAllocation("Project Track state");
    return tracks;
}

FLASHMEM core::app::ExtmemUniquePtr<sequencer::SequencerState> createSequencerEditorState() {
    auto state = core::app::makeExtmemUnique<sequencer::SequencerState>();
    if (!state) failCoreStateAllocation("sequencer editor state");
    return state;
}

FLASHMEM core::app::ExtmemUniquePtr<sequencer::SequencerTrackBankState>
createSequencerTrackBankState() {
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

FLASHMEM MacroDomainState::MacroDomainState()
    : runtime(core::app::makeExtmemUnique<MacroState>()),
      pages(core::app::makeExtmemUnique<macro::MacroPagesState>()) {
    if (!runtime) failCoreStateAllocation("macro runtime state");
    if (!pages) failCoreStateAllocation("macro pages state");
}

FLASHMEM MacroDomainState::~MacroDomainState() = default;

FLASHMEM SequencerDomainState::SequencerDomainState()
    : editor(createSequencerEditorState()), tracks(createSequencerTrackBankState()),
      history(core::app::makeExtmemUnique<sequencer::SequencerHistoryService>()) {
    if (!history) failCoreStateAllocation("sequencer history service");
}

FLASHMEM SequencerDomainState::~SequencerDomainState() = default;

FLASHMEM CoreState::CoreState(oc::interface::IStorage& deviceSettingsStorage)
    : macroDomain_(), sequencerDomain_(), projectTracks_(createProjectTrackState()),
      projectTrackHistory_(createProjectTrackHistory()),
      projectSettingsHistory_(createProjectSettingsHistory()),
      projectHistory_(createProjectHistoryCoordinator()), systemUi_(createUiSystemState()),
      deviceSettingsStore(deviceSettingsStorage), macros(*macroDomain_.runtime),
      pages(*macroDomain_.pages), macroHistory(macroDomain_.history),
      macroRuntimeOwnerRevision(macroDomain_.runtimeOwnerRevision),
      configRevision(macroDomain_.configRevision), sequencer(*sequencerDomain_.editor),
      sequencerTracks(*sequencerDomain_.tracks), sequencerHistory(*sequencerDomain_.history),
      sequencerTrackActivations(sequencerDomain_.trackActivations),
      sequencerRuntimeProjectRevision(sequencerDomain_.runtimeProjectRevision), project(project_),
      projectTracks(*projectTracks_), projectTrackHistory(*projectTrackHistory_),
      projectSettingsHistory(*projectSettingsHistory_), projectHistory(*projectHistory_),
      overlays(systemUi_->overlays), activeView(systemUi_->activeView),
      structureNavigationFocus(systemUi_->structureNavigationFocus),
      sharedTrackActive(systemUi_->sharedTracks.activeIndex),
      sharedTrackEnabledMask(systemUi_->sharedTracks.enabledMask),
      trackNavigation(systemUi_->trackNavigation),
      structureClipboard(systemUi_->structureClipboard), viewSelector(systemUi_->viewSelector),
      statusBar(systemUi_->statusBar), midiSync(systemUi_->midiSync),
      deviceSettings(systemUi_->deviceSettings), sequencerSettings(systemUi_->sequencerSettings),
      patternPitchSettings(systemUi_->patternPitchSettings), macroEdit(systemUi_->macroEdit),
      macroUi(systemUi_->macroUi), projectNavigation(systemUi_->projectNavigation),
      projectTrackEditor(systemUi_->projectTrackEditor) {
    projectHistory.setBranchInvalidatedCallback(this, &discardGlobalRedoBranches);
    macroHistory.setProjectHistoryEventSink(&projectHistory.eventSink());
    sequencerHistory.setProjectHistoryEventSink(&projectHistory.eventSink());
    projectTrackHistory.setProjectHistoryEventSink(&projectHistory.eventSink());
    projectSettingsHistory.setProjectHistoryEventSink(&projectHistory.eventSink());
    CoreStateBootstrap::initialize(*this);
}

FLASHMEM CoreState::~CoreState() = default;

void CoreState::update() { CoreStateLifecycle::update(*this); }

FLASHMEM void CoreState::factoryReset() { CoreStateLifecycle::factoryReset(*this); }

FLASHMEM void CoreState::flush() {
    if (commitSequencerPatternHistoryCoalescingOutcome() ==
        sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return;
    }
    CoreStateLifecycle::flush(*this);
}

FLASHMEM void CoreState::flushProjectMutationCoalescing() {
    CoreStateLifecycle::flushProjectMutationCoalescing(*this);
}

FLASHMEM void CoreState::resetStandaloneTransientUi() {
    if (commitSequencerPatternHistoryCoalescingOutcome() ==
        sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        OC_LOG_ERROR(
            "[CoreState] Invalid Sequencer history discarded during Standalone teardown"
        );
        // The departing context cannot recover this transient owner. Keep the
        // published Pattern authoritative and preserve committed undo entries.
        sequencerDomain_.coalescedPatternHistory.clear();
    }
    CoreStateLifecycle::resetStandaloneTransientUi(*this);
}

FLASHMEM ProjectResetOutcome CoreState::resetMusicalProject() {
    if (sequencer.stepContentDraft.rejectTransitionIfActive(
            sequencer::SequencerStepContentDraftBlockedTransition::RESET
        )) {
        return ProjectResetOutcome::DraftActive;
    }
    if (commitSequencerPatternHistoryCoalescingOutcome() ==
        sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return ProjectResetOutcome::HistoryUnavailable;
    }
    CoreStateLifecycle::resetMusicalProject(*this);
    return ProjectResetOutcome::Completed;
}

FLASHMEM void CoreState::requestMacroRuntimeOwnerActivation() {
    macroRuntimeOwnerRevision.set(nextNonZeroRuntimeRevision(macroRuntimeOwnerRevision.get()));
}

FLASHMEM void CoreState::requestSequencerRuntimeProjectReset() {
    sequencerTrackActivations.reset();
    sequencerRuntimeProjectRevision.set(
        nextNonZeroRuntimeRevision(sequencerRuntimeProjectRevision.get()));
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
    if (pending.pending && !macro::macroAutomationAddressEquals(pending.address, address)) {
        flushMacroValueHistoryCoalescing();
    }
    if (!macroHistory.setMacroValueCoalesced(pages, address, value)) { return false; }
    pending.pending = true;
    pending.address = address;
    pending.lastTouchedMs = oc::time::millis();
    markMacroValueEdited(index);
    return true;
}

bool CoreState::takeMacroManualControlWithHistory(uint8_t index, float value, bool coalesceValue) {
    if (index >= MACRO_COUNT) return false;
    const macro::MacroAutomationSlotAddress address{
        .track = pages.currentActiveTrack(),
        .page = pages.currentActivePage(),
        .macro = index,
    };
    auto& pending = macroDomain_.coalescedValueHistory;
    if (!coalesceValue ||
        (pending.pending && !macro::macroAutomationAddressEquals(pending.address, address))) {
        flushMacroValueHistoryCoalescing();
    }
    const float beforeBase = pages.pageData(address.track, address.page).values[address.macro];
    if (!macroHistory.setManualOverrideCoalesced(pages, macroUi.manualOverrides, address, value,
                                                 coalesceValue)) {
        return false;
    }
    if (coalesceValue) {
        pending.pending = true;
        pending.address = address;
        pending.lastTouchedMs = oc::time::millis();
    }
    if (beforeBase != pages.pageData(address.track, address.page).values[address.macro]) {
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
    return macroHistory.resumeManualOverride(pages, macroUi.manualOverrides, address);
}

void CoreState::updateMacroValueHistoryCoalescing(uint32_t nowMs) {
    auto& pending = macroDomain_.coalescedValueHistory;
    if (!pending.pending ||
        (nowMs - pending.lastTouchedMs) < MacroDomainState::COALESCED_VALUE_HISTORY_IDLE_MS) {
        return;
    }
    flushMacroValueHistoryCoalescing();
}

FLASHMEM void CoreState::flushMacroValueHistoryCoalescing() {
    macroHistory.endCoalescing();
    macroDomain_.coalescedValueHistory.clear();
}

FLASHMEM void CoreState::markProjectDurableMutation_() {
    const bool mutationRollover = projectSessionControl_.mutationEpoch == UINT32_MAX;
    const bool compatibilityRollover = project.metadata.modifiedCounter == UINT32_MAX;
    if (mutationRollover || compatibilityRollover) {
        if (!advanceProjectSessionIdentity_()) {
            project.metadata.dirty = true;
            return;
        }
        if (mutationRollover) {
            projectSessionControl_.mutationEpoch = 0U;
        }
        if (compatibilityRollover) {
            project.metadata.modifiedCounter = 0U;
        }
    }

    ++projectSessionControl_.mutationEpoch;
    ++project.metadata.modifiedCounter;
    project.metadata.dirty = true;
    const auto token = requestProjectSessionSave_();
    recordProjectSaveToken(
        StorageQualificationPhase::Admit,
        token,
        projectSessionControl_.savePending ? 0U : 1U,
        core::diagnostics::storage_qualification::SaveTokenFlagDurableMutation
    );
}

FLASHMEM void CoreState::markProjectMutated() {
    markProjectDurableMutation_();
    projectNavigation.notifyContentChanged();
}

FLASHMEM project::ProjectSaveToken CoreState::requestProjectSessionSave() {
    const auto token = requestProjectSessionSave_();
    recordProjectSaveToken(
        StorageQualificationPhase::Admit,
        token,
        projectSessionControl_.savePending ? 0U : 1U,
        core::diagnostics::storage_qualification::SaveTokenFlagExplicitRequest
    );
    return token;
}

FLASHMEM project::ProjectSaveToken CoreState::projectSessionSaveToken() const {
    return {
        .session = projectSessionControl_.session,
        .mutationEpoch = projectSessionControl_.mutationEpoch,
        .requestId = projectSessionControl_.requestId,
        .modifiedCounter = project.metadata.modifiedCounter,
    };
}

FLASHMEM bool CoreState::projectSessionSaveTokenMatches(
    const project::ProjectSaveToken& token
) const {
    return projectSessionSaveToken() == token;
}

FLASHMEM bool CoreState::acknowledgeProjectSessionSave(
    const project::ProjectSaveToken& savedToken
) {
    if (!projectSessionControl_.savePending ||
        !projectSessionSaveTokenMatches(savedToken)) {
        recordProjectSaveToken(
            StorageQualificationPhase::Complete,
            savedToken,
            1U,
            core::diagnostics::storage_qualification::SaveTokenFlagNone
        );
        return false;
    }

    projectSessionControl_.savePending = false;
    projectSessionControl_.requestTimestampMs = 0U;
    recordProjectSaveToken(
        StorageQualificationPhase::Complete,
        savedToken,
        0U,
        core::diagnostics::storage_qualification::SaveTokenFlagAcknowledged
    );
    return true;
}

FLASHMEM bool CoreState::advanceProjectSessionIdentity_() {
    auto& session = projectSessionControl_.session;
    if (session.sessionEpoch != UINT32_MAX) {
        ++session.sessionEpoch;
    } else if (session.bootGeneration != UINT32_MAX) {
        ++session.bootGeneration;
        session.sessionEpoch = 1U;
    } else {
        projectSessionControl_.trackingEnabled = false;
        projectSessionControl_.savePending = false;
        projectSessionControl_.requestTimestampMs = 0U;
        OC_LOG_ERROR("[CoreState] Project session identity exhausted");
        return false;
    }

    projectSessionControl_.requestId = 0U;
    projectSessionControl_.savePending = false;
    projectSessionControl_.requestTimestampMs = 0U;
    return true;
}

FLASHMEM void CoreState::publishProjectSessionReplacement_() {
    if (advanceProjectSessionIdentity_()) {
        const auto token = requestProjectSessionSave_();
        recordProjectSaveToken(
            StorageQualificationPhase::Admit,
            token,
            projectSessionControl_.savePending ? 0U : 1U,
            core::diagnostics::storage_qualification::SaveTokenFlagSessionReplacement
        );
    }
}

bool CoreState::hasPendingProjectSessionSave() const {
    return projectSessionControl_.savePending;
}

uint32_t CoreState::projectSessionSaveTimestampMs() const {
    return projectSessionControl_.requestTimestampMs;
}

bool CoreState::hasPendingProjectMutationCoalescing() const {
    const bool macroPending =
        macroDomain_.mutationCoalescer && macroDomain_.mutationCoalescer->hasPendingChanges();
    const bool sequencerPending = sequencerDomain_.mutationCoalescer &&
                                  sequencerDomain_.mutationCoalescer->hasPendingChanges();
    return macroPending || sequencerPending || hasPendingSequencerPatternHistoryCoalescing();
}

bool CoreState::hasPendingProjectTransaction() const {
    return hasPendingProjectMutationCoalescing() ||
           macroHistory.hasPendingModulatorAuditionTransaction(pages) ||
           projectTrackHistory.hasPendingGesture();
}

FLASHMEM void CoreState::markSequencerProjectMutated() { markSequencerProjectMutated_(); }

}  // namespace core::state

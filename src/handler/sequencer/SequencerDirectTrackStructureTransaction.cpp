#include "handler/sequencer/SequencerDirectTrackStructureTransaction.hpp"

#include <algorithm>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "handler/sequencer/SequencerStructurePageOps.hpp"
#include "handler/sequencer/SequencerStructureSelectionOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {
namespace {

using Action = SequencerPreparedTrackStructureAction;
using Plan = SequencerPreparedTrackStructurePlan;
using PlanOutcome = SequencerPreparedTrackStructurePlanOutcome;
using Result = SequencerPreparedTrackStructureResult;
using Status = SequencerPreparedTrackStructureStatus;
using TrackBank = core::state::sequencer::SequencerTrackBankState;

struct SelectionIntentToken {
    uint32_t clipboardRevision = 0U;
    uint16_t selectedMask = 0U;
    uint16_t destinationMask = 0U;
    uint16_t overwriteMask = 0U;
    core::state::StructureSelectionScope scope =
        core::state::StructureSelectionScope::PAGE;
    uint8_t cursor = 0U;
    bool active = false;
    bool placing = false;
    bool pasteBlocked = false;

    bool operator==(const SelectionIntentToken& other) const noexcept {
        return scope == other.scope &&
               clipboardRevision == other.clipboardRevision &&
               selectedMask == other.selectedMask &&
               destinationMask == other.destinationMask &&
               overwriteMask == other.overwriteMask &&
               cursor == other.cursor && active == other.active &&
               placing == other.placing &&
               pasteBlocked == other.pasteBlocked;
    }
};

static_assert(
    sizeof(SelectionIntentToken) == 16U,
    "direct selection intent token must remain compact"
);

struct StepSelectionIntentToken {
    oc::note::sequencer::StepBitMask128 selectedMask{};
    uint32_t clipboardRevision = 0U;
    uint8_t cursor = 0U;
    core::state::sequencer::SequencerStepPastePreview pastePreview =
        core::state::sequencer::SequencerStepPastePreview::NONE;
    bool active = false;
    bool placing = false;
    bool pastePreviewActive = false;

    bool operator==(const StepSelectionIntentToken& other) const noexcept {
        return selectedMask == other.selectedMask &&
               clipboardRevision == other.clipboardRevision &&
               cursor == other.cursor &&
               pastePreview == other.pastePreview &&
               active == other.active && placing == other.placing &&
               pastePreviewActive == other.pastePreviewActive;
    }
};

struct ClipboardIntentToken {
    core::state::StructureClipboardKind kind =
        core::state::StructureClipboardKind::NONE;
    uint32_t revision = 0U;
    const void* macroAutomation = nullptr;
    const void* macroModulationAssignment = nullptr;
    const void* sequencerGraph = nullptr;
    const void* sequencerCcLanes = nullptr;
    const void* sequencerTrackSelection = nullptr;
    const void* macroPageSelection = nullptr;

    bool operator==(const ClipboardIntentToken& other) const noexcept {
        return kind == other.kind && revision == other.revision &&
               macroAutomation == other.macroAutomation &&
               macroModulationAssignment ==
                   other.macroModulationAssignment &&
               sequencerGraph == other.sequencerGraph &&
               sequencerCcLanes == other.sequencerCcLanes &&
               sequencerTrackSelection ==
                   other.sequencerTrackSelection &&
               macroPageSelection == other.macroPageSelection;
    }
};

struct TrackPasteIntentToken {
    core::state::StructureClipboardKind clipboardKind =
        core::state::StructureClipboardKind::NONE;
    uint32_t revision = 0U;
    uint32_t clipboardRevision = 0U;
    uint32_t interactionGeneration = 0U;
    uint32_t operationGeneration = 0U;
    uint32_t activationGeneration = 0U;
    bool gestureActive = false;
    bool detailVisible = false;
    bool buttonOwned = false;
    bool commitConsumed = false;

    bool operator==(const TrackPasteIntentToken& other) const noexcept {
        return clipboardKind == other.clipboardKind &&
               revision == other.revision &&
               clipboardRevision == other.clipboardRevision &&
               interactionGeneration == other.interactionGeneration &&
               operationGeneration == other.operationGeneration &&
               activationGeneration == other.activationGeneration &&
               gestureActive == other.gestureActive &&
               detailVisible == other.detailVisible &&
               buttonOwned == other.buttonOwned &&
               commitConsumed == other.commitConsumed;
    }
};

struct IntentToken {
    Action action = Action::SequencerCreate;
    core::state::StructureNavigationFocus focus =
        core::state::StructureNavigationFocus::PAGE;
    uint8_t targetTrack = TrackBank::TRACK_COUNT;
    uint8_t activeTrack = TrackBank::TRACK_COUNT;
    uint8_t previewTrack = TrackBank::TRACK_COUNT;
    uint8_t pagePreview = 0U;
    // Immutable creation payload, deliberately excluded from the live UI
    // intent comparison. 0xFF denotes Instrument; Drum encodes its preset.
    uint8_t createDrumPreset = 0xFFU;
    bool previewAddTrack = false;
    SelectionIntentToken trackSelection{};
    SelectionIntentToken pageSelection{};
    StepSelectionIntentToken stepSelection{};
    ClipboardIntentToken clipboard{};
    TrackPasteIntentToken trackPaste{};
};

struct DirectContext {
    const SequencerDirectTrackStructureStateRefs& state;
    IntentToken token;
};

static_assert(
    sizeof(void*) != 4U || sizeof(DirectContext) <= 160U,
    "direct Track Structure context exceeds its ARM stack contract"
);
static_assert(
    sizeof(void*) != 8U || sizeof(DirectContext) <= 168U,
    "direct Track Structure context exceeds its native stack contract"
);

enum class InitialTopologyOutcome : uint8_t {
    Ready = 0U,
    Invalid,
    Stale,
};

FLASHMEM SelectionIntentToken captureSelectionIntent(
    const core::state::StructureSelectionState& selection
) noexcept {
    return {
        .clipboardRevision = selection.clipboardRevision.get(),
        .selectedMask = selection.selectedMask.get(),
        .destinationMask = selection.destinationMask.get(),
        .overwriteMask = selection.overwriteMask.get(),
        .scope = selection.scope.get(),
        .cursor = selection.cursorIndex.get(),
        .active = selection.active.get(),
        .placing = selection.placing.get(),
        .pasteBlocked = selection.pasteBlocked.get(),
    };
}

FLASHMEM StepSelectionIntentToken captureStepSelectionIntent(
    const core::state::sequencer::SequencerStepSelectionState& selection
) noexcept {
    return {
        .selectedMask = selection.selectedMask.get(),
        .clipboardRevision = selection.clipboardRevision.get(),
        .cursor = selection.cursorStep.get(),
        .pastePreview = selection.pastePreview.get(),
        .active = selection.active.get(),
        .placing = selection.placing.get(),
        .pastePreviewActive = selection.pastePreviewActive.get(),
    };
}

FLASHMEM ClipboardIntentToken captureClipboardIntent(
    const core::state::StructureClipboardState& clipboard
) noexcept {
    return {
        .kind = clipboard.kind.get(),
        .revision = clipboard.revision.get(),
        .macroAutomation = clipboard.macroAutomationSet.get(),
        .macroModulationAssignment =
            clipboard.macroModulationAssignment.get(),
        .sequencerGraph = clipboard.sequencerGraph.get(),
        .sequencerCcLanes = clipboard.sequencerCcLanes.get(),
        .sequencerTrackSelection =
            clipboard.sequencerTrackSelection.get(),
        .macroPageSelection = clipboard.macroPageSelection.get(),
    };
}

FLASHMEM TrackPasteIntentToken captureTrackPasteIntent(
    const core::state::sequencer::SequencerTrackPasteUiState& paste
) noexcept {
    return {
        .clipboardKind = paste.clipboardKind,
        .revision = paste.revision.get(),
        .clipboardRevision = paste.clipboardRevision,
        .interactionGeneration = paste.interactionGeneration,
        .operationGeneration = paste.operationGeneration,
        .activationGeneration = paste.activationGeneration,
        .gestureActive = paste.gestureActive(),
        .detailVisible = paste.detailVisible,
        .buttonOwned = paste.buttonOwned,
        .commitConsumed = paste.commitConsumed,
    };
}

FLASHMEM IntentToken captureIntent(
    const SequencerDirectTrackStructureStateRefs& state,
    Action action,
    uint8_t latchedTargetTrack
) noexcept {
    const auto& trackUi = state.trackNavigation;
    const auto& structureUi = state.sequencer.structureUi;
    IntentToken token{};
    token.action = action;
    token.focus = state.navigationFocus.get();
    token.previewTrack = trackUi.previewTrackIndex.get();
    token.activeTrack = state.sharedTracks.activeTrack();
    token.targetTrack = action == Action::SequencerCreate
        ? token.previewTrack
        : latchedTargetTrack;
    token.pagePreview = structureUi.previewPageIndex.get();
    token.previewAddTrack = trackUi.previewAddSlot.get();
    token.trackSelection = captureSelectionIntent(trackUi.selection);
    token.pageSelection = captureSelectionIntent(structureUi.pageSelection);
    token.stepSelection = captureStepSelectionIntent(structureUi.stepSelection);
    token.clipboard = captureClipboardIntent(state.clipboard);
    token.trackPaste = captureTrackPasteIntent(structureUi.trackPaste);
    return token;
}

FLASHMEM bool sameIntent(
    const IntentToken& lhs,
    const IntentToken& rhs
) noexcept {
    return lhs.action == rhs.action && lhs.focus == rhs.focus &&
           lhs.targetTrack == rhs.targetTrack &&
           lhs.activeTrack == rhs.activeTrack &&
           lhs.previewTrack == rhs.previewTrack &&
           lhs.pagePreview == rhs.pagePreview &&
           lhs.previewAddTrack == rhs.previewAddTrack &&
           lhs.trackSelection == rhs.trackSelection &&
           lhs.pageSelection == rhs.pageSelection &&
           lhs.stepSelection == rhs.stepSelection &&
           lhs.clipboard == rhs.clipboard &&
           lhs.trackPaste == rhs.trackPaste;
}

FLASHMEM bool validIntent(
    const DirectContext& context,
    Action action
) noexcept {
    const auto& token = context.token;
    if (token.action != action ||
        token.focus != core::state::StructureNavigationFocus::TRACK ||
        token.targetTrack >= TrackBank::TRACK_COUNT ||
        token.previewTrack >= TrackBank::TRACK_COUNT ||
        token.pageSelection.active ||
        token.pageSelection.placing || token.stepSelection.active ||
        token.stepSelection.placing || token.trackPaste.gestureActive ||
        token.trackPaste.detailVisible || token.trackPaste.buttonOwned) {
        return false;
    }
    if (action == Action::SequencerCreate) {
        return !token.trackSelection.active &&
               !token.trackSelection.placing && token.previewAddTrack &&
               token.previewTrack == token.targetTrack;
    }
    if (action == Action::SequencerRemoveCurrent) {
        return !token.trackSelection.active &&
               !token.trackSelection.placing && !token.previewAddTrack &&
               token.activeTrack == token.targetTrack &&
               token.previewTrack == token.targetTrack;
    }
    if (action == Action::SequencerRemoveSelection) {
        return token.trackSelection.active &&
               token.trackSelection.scope ==
                   core::state::StructureSelectionScope::TRACK &&
               !token.trackSelection.placing && !token.previewAddTrack &&
               token.activeTrack == token.targetTrack;
    }
    return false;
}

FLASHMEM bool intentStillMatches(
    const DirectContext& context,
    Action action
) noexcept {
    return validIntent(context, action) &&
           sameIntent(
               context.token,
               captureIntent(
                   context.state,
                   action,
                   context.token.targetTrack
               )
           );
}

FLASHMEM InitialTopologyOutcome validateInitialTopology(
    const DirectContext& context,
    Action action
) noexcept {
    const uint16_t enabledMask = context.state.sharedTracks.enabledMask();
    const uint8_t activeTrack = context.state.sharedTracks.activeTrack();
    const uint8_t targetTrack = context.token.targetTrack;
    if (activeTrack >= TrackBank::TRACK_COUNT ||
        targetTrack >= TrackBank::TRACK_COUNT ||
        (enabledMask & core::state::shared::slotBit(activeTrack)) == 0U) {
        return InitialTopologyOutcome::Stale;
    }

    const uint16_t targetBit =
        core::state::shared::slotBit(targetTrack);
    if (action == Action::SequencerCreate) {
        return (enabledMask & targetBit) == 0U
            ? InitialTopologyOutcome::Ready
            : InitialTopologyOutcome::Invalid;
    }
    if (action == Action::SequencerRemoveCurrent) {
        if (targetTrack != activeTrack) {
            return InitialTopologyOutcome::Stale;
        }
        return core::state::shared::countEnabled(
                   enabledMask,
                   TrackBank::TRACK_COUNT
               ) > 1U
            ? InitialTopologyOutcome::Ready
            : InitialTopologyOutcome::Invalid;
    }
    if (action == Action::SequencerRemoveSelection) {
        if (targetTrack != activeTrack) {
            return InitialTopologyOutcome::Stale;
        }
        const uint16_t selectedMask = activeTrackSelectionMask(
            context.token.trackSelection.selectedMask,
            enabledMask
        );
        return deleteSelectedStructureTracks(
                   enabledMask,
                   selectedMask,
                   activeTrack
               ).changed
            ? InitialTopologyOutcome::Ready
            : InitialTopologyOutcome::Invalid;
    }
    return InitialTopologyOutcome::Invalid;
}

FLASHMEM bool fillActiveChangeFocus(
    const DirectContext& context,
    uint8_t incomingLength,
    Plan& plan
) noexcept {
    if (incomingLength == 0U ||
        incomingLength >
            core::state::sequencer::SequencerState::MAX_STEPS) {
        return false;
    }
    plan.beforeFocusedStep = context.state.sequencer.focusedStep.get();
    plan.beforePage = context.state.sequencer.page.get();
    plan.afterFocusedStep = std::min<uint8_t>(
        plan.beforeFocusedStep,
        static_cast<uint8_t>(incomingLength - 1U)
    );
    plan.afterPage = static_cast<uint8_t>(
        plan.afterFocusedStep /
        core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );
    return true;
}

FLASHMEM PlanOutcome buildPlan(
    const void* opaque,
    Action action,
    Plan& out
) noexcept {
    out = {};
    if (opaque == nullptr) return PlanOutcome::Invalid;
    const auto& context = *static_cast<const DirectContext*>(opaque);
    if (!intentStillMatches(context, action) ||
        !context.state.sharedTracks.
            canReconcilePreparedSequencerActiveTrackPresentation()) {
        return PlanOutcome::Stale;
    }

    const uint16_t beforeMask = context.state.sharedTracks.enabledMask();
    const uint8_t beforeActive = context.state.sharedTracks.activeTrack();
    if (beforeActive >= TrackBank::TRACK_COUNT ||
        (beforeMask & core::state::shared::slotBit(beforeActive)) == 0U) {
        return PlanOutcome::Stale;
    }

    Plan plan{};
    plan.action = action;
    plan.beforeEnabledMask = beforeMask;
    plan.beforeActiveTrack = beforeActive;
    plan.targetTrack = context.token.targetTrack;
    const uint16_t oldActiveBit =
        core::state::shared::slotBit(beforeActive);

    if (action == Action::SequencerCreate) {
        const uint16_t targetBit =
            core::state::shared::slotBit(plan.targetTrack);
        if ((beforeMask & targetBit) != 0U) return PlanOutcome::Stale;
        plan.afterEnabledMask = static_cast<uint16_t>(
            beforeMask | targetBit
        );
        plan.afterActiveTrack = plan.targetTrack;
        plan.affectedTrackMask = targetBit;
        plan.capturedTrackMask = static_cast<uint16_t>(
            oldActiveBit | targetBit
        );
        plan.canonicalResetTrackMask = targetBit;
        plan.incomingOwnerPolicy = core::state::sequencer::
            SequencerActiveTrackIncomingOwnerPolicy::Reset;
        if (!fillActiveChangeFocus(
                context,
                core::state::sequencer::SequencerPatternState::DEFAULT_LENGTH,
                plan
            )) {
            return PlanOutcome::Invalid;
        }
    } else if (action == Action::SequencerRemoveCurrent) {
        if (plan.targetTrack != beforeActive) return PlanOutcome::Stale;
        const auto mutation = core::state::shared::removeIndex(
            beforeMask,
            beforeActive,
            TrackBank::TRACK_COUNT
        );
        if (!mutation.changed) return PlanOutcome::Invalid;
        plan.afterEnabledMask = mutation.nextMask;
        plan.afterActiveTrack = mutation.nextActive;
        plan.affectedTrackMask = oldActiveBit;
        plan.capturedTrackMask = static_cast<uint16_t>(
            oldActiveBit |
            core::state::shared::slotBit(mutation.nextActive)
        );
        plan.incomingOwnerPolicy = core::state::sequencer::
            SequencerActiveTrackIncomingOwnerPolicy::Preserve;
        if (!fillActiveChangeFocus(
                context,
                context.state.tracks.track(mutation.nextActive).length.get(),
                plan
            )) {
            return PlanOutcome::Invalid;
        }
    } else if (action == Action::SequencerRemoveSelection) {
        if (plan.targetTrack != beforeActive) return PlanOutcome::Stale;
        const uint16_t selectedMask = activeTrackSelectionMask(
            context.token.trackSelection.selectedMask,
            beforeMask
        );
        const auto mutation = deleteSelectedStructureTracks(
            beforeMask,
            selectedMask,
            beforeActive
        );
        if (!mutation.changed) return PlanOutcome::Invalid;
        plan.targetTrack = TrackBank::TRACK_COUNT;
        plan.afterEnabledMask = mutation.nextMask;
        plan.afterActiveTrack = mutation.nextActive;
        plan.affectedTrackMask = selectedMask;
        plan.capturedTrackMask = static_cast<uint16_t>(
            oldActiveBit |
            core::state::shared::slotBit(mutation.nextActive)
        );
        plan.incomingOwnerPolicy = core::state::sequencer::
            SequencerActiveTrackIncomingOwnerPolicy::Preserve;
        const uint8_t incomingLength = core::state::sequencer::canonicalTrackPattern(
            context.state.tracks,
            context.state.sequencer,
            mutation.nextActive
        ).length.get();
        if (!fillActiveChangeFocus(
                context,
                incomingLength,
                plan
            )) {
            return PlanOutcome::Invalid;
        }
    } else {
        return PlanOutcome::Invalid;
    }

    out = plan;
    return PlanOutcome::Ready;
}

FLASHMEM bool revalidate(
    const void* opaque,
    const Plan& plan,
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change
) noexcept {
    (void)change;
    if (opaque == nullptr) return false;
    const auto& context = *static_cast<const DirectContext*>(opaque);
    return intentStillMatches(context, plan.action) &&
           context.state.sharedTracks.
               canReconcilePreparedSequencerActiveTrackPresentation();
}

FLASHMEM bool prepareSequencerAfter(
    const void* opaque,
    const Plan& plan,
    core::state::sequencer::SequencerHistoryTrackStructureChange& change
) noexcept {
    if (opaque == nullptr) return false;
    const auto& context = *static_cast<const DirectContext*>(opaque);
    if (plan.action != Action::SequencerCreate) return true;
    if (plan.targetTrack >= TrackBank::TRACK_COUNT) return false;

    const uint16_t targetBit = core::state::shared::slotBit(
        plan.targetTrack
    );
    auto& after = change.after;
    after.drumTracks[plan.targetTrack].reset();
    after.drumTrackMask = static_cast<uint16_t>(
        after.drumTrackMask & static_cast<uint16_t>(~targetBit)
    );
    if (context.token.createDrumPreset == 0xFFU) {
        return true;
    }

    auto drum = core::app::makeExtmemUnique<
        core::state::sequencer::DrumTrackState
    >();
    if (!drum) return false;
    drum->reset(static_cast<core::state::sequencer::DrumKitPreset>(
        context.token.createDrumPreset
    ));
    after.drumTracks[plan.targetTrack] = std::move(drum);
    after.drumTrackMask = static_cast<uint16_t>(
        after.drumTrackMask | targetBit
    );
    return true;
}

FLASHMEM void reconcileCommitted(
    void* opaque,
    const Plan& plan,
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change
) noexcept {
    (void)plan;
    (void)change;
    auto& context = *static_cast<DirectContext*>(opaque);
    context.state.sharedTracks.
        reconcilePreparedSequencerActiveTrackPresentation();
}

FLASHMEM void settleSuccessful(void* opaque, const Plan& plan) noexcept {
    auto& context = *static_cast<DirectContext*>(opaque);
    if (plan.action == Action::SequencerRemoveSelection) {
        context.state.trackNavigation.selection.reset(
            core::state::StructureSelectionScope::TRACK,
            plan.afterActiveTrack
        );
        context.state.navigationFocus.set(
            core::state::StructureNavigationFocus::TRACK
        );
    }
    context.state.trackNavigation.previewAddSlot.set(false);
    context.state.trackNavigation.syncPreviewTrack(plan.afterActiveTrack);
    syncSequencerPagePreviewToVisible(
        context.state.sequencer,
        false
    );
}

const SequencerPreparedTrackStructureExecution::Operations
    kDirectOperations{
        .buildPlan = buildPlan,
        .prepareMacroAfter = nullptr,
        .prepareSequencerAfter = prepareSequencerAfter,
        .revalidate = revalidate,
        .reconcileCommitted = reconcileCommitted,
        .settleNoChange = nullptr,
        .settleSuccessful = settleSuccessful,
    };

FLASHMEM Result executeDirect(
    const SequencerDirectTrackStructureStateRefs& state,
    Action action,
    uint8_t latchedTargetTrack,
    core::state::sequencer::SequencerTrackKind createKind =
        core::state::sequencer::SequencerTrackKind::INSTRUMENT,
    core::state::sequencer::DrumKitPreset drumPreset =
        core::state::sequencer::DrumKitPreset::GENERAL_MIDI
) {
    // Draft owns Track transition priority even when another dispatch token or
    // publication capability is also invalid. Mirror the kernel's first gate
    // before adapter-local validation so the required marker is never skipped.
    if (state.sequencer.stepContentDraft.rejectTransitionIfActive(
            core::state::sequencer::
                SequencerStepContentDraftBlockedTransition::TRACK
        )) {
        return {Status::DraftBlocked, {}};
    }
    // Hold provenance is a physical dispatch fact. Validate it once; it is
    // intentionally absent from chronology replans and live revalidation.
    if (state.trackNavigation.hold.action.get() !=
        core::state::StructureHoldAction::NONE) {
        return {Status::Invalid, {}};
    }
    if (!state.sharedTracks.
            canReconcilePreparedSequencerActiveTrackPresentation()) {
        return {Status::PublicationUnavailable, {}};
    }
    auto intent = captureIntent(state, action, latchedTargetTrack);
    if (action == Action::SequencerCreate && createKind ==
            core::state::sequencer::SequencerTrackKind::DRUM) {
        intent.createDrumPreset = static_cast<uint8_t>(drumPreset);
    }
    DirectContext context{state, intent};
    if (!validIntent(context, action)) {
        return {
            action != Action::SequencerCreate
                ? Status::Stale
                : Status::Invalid,
            {},
        };
    }
    switch (validateInitialTopology(context, action)) {
        case InitialTopologyOutcome::Ready:
            break;
        case InitialTopologyOutcome::Stale:
            return {Status::Stale, {}};
        case InitialTopologyOutcome::Invalid:
        default:
            return {Status::Invalid, {}};
    }
    return executeSequencerTrackStructureTransaction(
        SequencerPreparedTrackStructureStateRefs{
            state.tracks,
            state.sequencer,
            &state.macroPages,
            state.activationQueue,
            state.sharedTracks,
            state.history,
        },
        action,
        SequencerPreparedTrackStructureExecution::
            fromStaticOperations<kDirectOperations>(&context)
    );
}

}  // namespace

FLASHMEM Result executeSequencerCreateTrackStructure(
    SequencerDirectTrackStructureStateRefs state,
    core::state::sequencer::SequencerTrackKind kind,
    core::state::sequencer::DrumKitPreset drumPreset
) {
    return executeDirect(
        state,
        Action::SequencerCreate,
        TrackBank::TRACK_COUNT,
        kind,
        drumPreset
    );
}

FLASHMEM Result executeSequencerRemoveCurrentTrackStructure(
    SequencerDirectTrackStructureStateRefs state,
    uint8_t latchedTargetTrack
) {
    return executeDirect(
        state,
        Action::SequencerRemoveCurrent,
        latchedTargetTrack
    );
}

FLASHMEM Result executeSequencerRemoveSelectionTrackStructure(
    SequencerDirectTrackStructureStateRefs state,
    uint8_t latchedActiveTrack
) {
    return executeDirect(
        state,
        Action::SequencerRemoveSelection,
        latchedActiveTrack
    );
}

}  // namespace core::handler

#include "handler/macro/MacroDirectTrackStructureTransaction.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/macro/MacroStructureAutomationOps.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {
namespace {

using Action = SequencerPreparedTrackStructureAction;
using MacroOutcome = SequencerPreparedTrackStructureMacroOutcome;
using Plan = SequencerPreparedTrackStructurePlan;
using PlanOutcome = SequencerPreparedTrackStructurePlanOutcome;
using Result = SequencerPreparedTrackStructureResult;
using Status = SequencerPreparedTrackStructureStatus;
using TrackBank = core::state::sequencer::SequencerTrackBankState;

constexpr uint64_t kFingerprintOffset = 14695981039346656037ULL;
constexpr uint64_t kFingerprintPrime = 1099511628211ULL;

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

    [[nodiscard]] bool operator==(
        const SelectionIntentToken& other
    ) const noexcept {
        return clipboardRevision == other.clipboardRevision &&
               selectedMask == other.selectedMask &&
               destinationMask == other.destinationMask &&
               overwriteMask == other.overwriteMask &&
               scope == other.scope && cursor == other.cursor &&
               active == other.active && placing == other.placing &&
               pasteBlocked == other.pasteBlocked;
    }
};

static_assert(
    sizeof(SelectionIntentToken) == 16U,
    "Macro direct selection token must remain compact"
);

struct IntentToken {
    Action action = Action::MacroDelete;
    core::state::StructureNavigationFocus focus =
        core::state::StructureNavigationFocus::PAGE;
    core::state::StructureClipboardKind clipboardKind =
        core::state::StructureClipboardKind::NONE;
    core::state::StructureHoldAction holdAction =
        core::state::StructureHoldAction::NONE;
    uint32_t clipboardRevision = 0U;
    uint32_t holdAcquisition = 0U;
    uint32_t contextRevision = 0U;
    uint8_t targetTrack = TrackBank::TRACK_COUNT;
    uint8_t activeTrack = TrackBank::TRACK_COUNT;
    uint8_t previewTrack = TrackBank::TRACK_COUNT;
    bool previewAddTrack = false;
    bool contextVisible = false;
    core::state::StructureNavigationFocus contextPreviewFocus =
        core::state::StructureNavigationFocus::PAGE;
    bool canonicalClipboardSource = false;
    const core::state::MacroAutomationClipboard* clipboardAutomation = nullptr;
    SelectionIntentToken trackSelection{};
};

struct DirectContext {
    core::state::CoreState& state;
    Action action = Action::MacroDelete;
    uint8_t targetTrack = TrackBank::TRACK_COUNT;
    const core::state::macro::MacroTrackData* pasteTrack = nullptr;
    const core::state::MacroAutomationClipboard* pasteAutomation = nullptr;
    IntentToken intent{};
    uint64_t pasteTrackFingerprint = 0U;
    uint64_t pasteAutomationFingerprint = 0U;
};

static_assert(
    sizeof(void*) != 4U || sizeof(DirectContext) <= 96U,
    "Macro direct Track context exceeds its ARM stack contract"
);
static_assert(
    sizeof(void*) != 8U || sizeof(DirectContext) <= 120U,
    "Macro direct Track context exceeds its native stack contract"
);

enum class InitialTopologyOutcome : uint8_t {
    Ready = 0U,
    Invalid,
    Stale,
};

FLASHMEM uint64_t fingerprintBytes(
    const void* bytes,
    std::size_t size
) noexcept {
    if (bytes == nullptr) return 0U;
    const auto* cursor = static_cast<const uint8_t*>(bytes);
    uint64_t hash = kFingerprintOffset;
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= cursor[index];
        hash *= kFingerprintPrime;
    }
    return hash;
}

FLASHMEM uint64_t fingerprintTrack(
    const core::state::macro::MacroTrackData* track
) noexcept {
    return fingerprintBytes(track, track == nullptr ? 0U : sizeof(*track));
}

FLASHMEM uint64_t fingerprintAutomation(
    const core::state::MacroAutomationClipboard* automation
) noexcept {
    return fingerprintBytes(
        automation,
        automation == nullptr ? 0U : sizeof(*automation)
    );
}

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

FLASHMEM IntentToken captureIntent(const DirectContext& context) noexcept {
    const auto& state = context.state;
    const bool canonicalClipboardSource =
        context.action == Action::MacroPaste &&
        context.pasteTrack == &state.structureClipboard.macroTrack;
    return {
        .action = context.action,
        .focus = state.structureNavigationFocus.get(),
        .clipboardKind = canonicalClipboardSource
            ? state.structureClipboard.kind.get()
            : core::state::StructureClipboardKind::NONE,
        .holdAction = state.trackNavigation.hold.action.get(),
        .clipboardRevision = canonicalClipboardSource
            ? state.structureClipboard.revision.get()
            : 0U,
        .holdAcquisition = state.trackNavigation.hold.acquisitionId(),
        .contextRevision = state.macroUi.contextSelector.revision.get(),
        .targetTrack = context.targetTrack,
        .activeTrack = state.sharedTrackActive.get(),
        .previewTrack = state.trackNavigation.previewTrackIndex.get(),
        .previewAddTrack = state.trackNavigation.previewAddSlot.get(),
        .contextVisible = state.macroUi.contextSelector.visible,
        .contextPreviewFocus =
            state.macroUi.contextSelector.previewFocus,
        .canonicalClipboardSource = canonicalClipboardSource,
        .clipboardAutomation = canonicalClipboardSource
            ? state.structureClipboard.macroAutomationSet.get()
            : nullptr,
        .trackSelection = captureSelectionIntent(
            state.trackNavigation.selection
        ),
    };
}

FLASHMEM bool sameIntent(
    const IntentToken& lhs,
    const IntentToken& rhs
) noexcept {
    return lhs.action == rhs.action && lhs.focus == rhs.focus &&
           lhs.clipboardKind == rhs.clipboardKind &&
           lhs.holdAction == rhs.holdAction &&
           lhs.clipboardRevision == rhs.clipboardRevision &&
           lhs.holdAcquisition == rhs.holdAcquisition &&
           lhs.contextRevision == rhs.contextRevision &&
           lhs.targetTrack == rhs.targetTrack &&
           lhs.activeTrack == rhs.activeTrack &&
           lhs.previewTrack == rhs.previewTrack &&
           lhs.previewAddTrack == rhs.previewAddTrack &&
           lhs.contextVisible == rhs.contextVisible &&
           lhs.contextPreviewFocus == rhs.contextPreviewFocus &&
           lhs.canonicalClipboardSource ==
               rhs.canonicalClipboardSource &&
           lhs.clipboardAutomation == rhs.clipboardAutomation &&
           lhs.trackSelection == rhs.trackSelection;
}

FLASHMEM bool validIntent(const DirectContext& context) noexcept {
    const auto& token = context.intent;
    if (token.action != context.action ||
        token.targetTrack >= TrackBank::TRACK_COUNT ||
        token.activeTrack >= TrackBank::TRACK_COUNT) {
        return false;
    }
    if (context.action == Action::MacroPaste &&
        context.pasteTrack == nullptr) {
        return false;
    }
    if (token.canonicalClipboardSource &&
        (token.clipboardKind !=
             core::state::StructureClipboardKind::MACRO_TRACK ||
         token.clipboardAutomation != context.pasteAutomation)) {
        return false;
    }

    // Product workflow dispatch is stricter than the reusable service surface.
    // Freeze its exact Track preview when the UI is the caller; programmatic
    // service tests may legitimately execute while another view is focused.
    if (token.focus != core::state::StructureNavigationFocus::TRACK) {
        return true;
    }
    if (token.trackSelection.active || token.trackSelection.placing) {
        return false;
    }
    switch (context.action) {
        case Action::MacroDelete:
        case Action::MacroReset:
            return !token.previewAddTrack &&
                   token.targetTrack == token.activeTrack;
        case Action::MacroPaste:
            return token.targetTrack == (token.previewAddTrack
                ? token.previewTrack
                : token.activeTrack);
        case Action::MacroCreate:
            return token.previewAddTrack &&
                   token.targetTrack == token.previewTrack;
        default:
            return false;
    }
}

FLASHMEM bool pasteSourcesMatch(const DirectContext& context) noexcept {
    if (context.action != Action::MacroPaste) return true;
    if (context.pasteTrack == nullptr ||
        fingerprintTrack(context.pasteTrack) !=
            context.pasteTrackFingerprint ||
        fingerprintAutomation(context.pasteAutomation) !=
            context.pasteAutomationFingerprint ||
        !macro_structure_automation_ops::trackClipboardValid(
            context.pasteAutomation
        )) {
        return false;
    }
    if (!context.intent.canonicalClipboardSource) return true;
    return context.state.structureClipboard.hasMacroTrack() &&
           &context.state.structureClipboard.macroTrack ==
               context.pasteTrack &&
           context.state.structureClipboard.macroAutomationSet.get() ==
               context.pasteAutomation;
}

FLASHMEM bool intentStillMatches(const DirectContext& context) noexcept {
    return validIntent(context) && pasteSourcesMatch(context) &&
           sameIntent(context.intent, captureIntent(context));
}

FLASHMEM InitialTopologyOutcome validateInitialTopology(
    const DirectContext& context
) noexcept {
    const uint16_t enabledMask =
        context.state.sharedTrackEnabledMask.get();
    const uint8_t activeTrack = context.state.sharedTrackActive.get();
    if (activeTrack >= TrackBank::TRACK_COUNT ||
        context.targetTrack >= TrackBank::TRACK_COUNT ||
        (enabledMask & core::state::shared::slotBit(activeTrack)) == 0U) {
        return InitialTopologyOutcome::Stale;
    }
    const uint16_t targetBit =
        core::state::shared::slotBit(context.targetTrack);
    switch (context.action) {
        case Action::MacroDelete:
            if (context.targetTrack != activeTrack) {
                return InitialTopologyOutcome::Stale;
            }
            return core::state::shared::countEnabled(
                       enabledMask,
                       TrackBank::TRACK_COUNT
                   ) > 1U
                ? InitialTopologyOutcome::Ready
                : InitialTopologyOutcome::Invalid;
        case Action::MacroReset:
            return (enabledMask & targetBit) != 0U
                ? InitialTopologyOutcome::Ready
                : InitialTopologyOutcome::Invalid;
        case Action::MacroPaste:
            return InitialTopologyOutcome::Ready;
        case Action::MacroCreate:
            return (enabledMask & targetBit) == 0U
                ? InitialTopologyOutcome::Ready
                : InitialTopologyOutcome::Invalid;
        default:
            return InitialTopologyOutcome::Invalid;
    }
}

FLASHMEM bool fillFocus(
    const DirectContext& context,
    Plan& plan
) noexcept {
    plan.beforeFocusedStep = context.state.sequencer.focusedStep.get();
    plan.beforePage = context.state.sequencer.page.get();
    plan.afterFocusedStep = plan.beforeFocusedStep;
    plan.afterPage = plan.beforePage;
    if (plan.beforeActiveTrack == plan.afterActiveTrack) return true;

    const uint8_t incomingLength =
        context.state.sequencerTracks.track(plan.afterActiveTrack).length.get();
    if (incomingLength == 0U ||
        incomingLength > core::state::sequencer::SequencerState::MAX_STEPS) {
        return false;
    }
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
    if (action != context.action || !intentStillMatches(context)) {
        return PlanOutcome::Stale;
    }

    const uint16_t beforeMask =
        context.state.sharedTrackEnabledMask.get();
    const uint8_t beforeActive = context.state.sharedTrackActive.get();
    if (beforeActive >= TrackBank::TRACK_COUNT ||
        (beforeMask & core::state::shared::slotBit(beforeActive)) == 0U) {
        return PlanOutcome::Stale;
    }

    Plan plan{};
    plan.action = action;
    plan.beforeEnabledMask = beforeMask;
    plan.beforeActiveTrack = beforeActive;
    plan.targetTrack = context.targetTrack;
    plan.macroAffectedTrack = context.targetTrack;
    plan.affectedTrackMask =
        core::state::shared::slotBit(context.targetTrack);
    plan.canonicalResetTrackMask = 0U;
    plan.incomingOwnerPolicy = core::state::sequencer::
        SequencerActiveTrackIncomingOwnerPolicy::Preserve;

    const uint16_t oldActiveBit =
        core::state::shared::slotBit(beforeActive);
    const uint16_t targetBit =
        core::state::shared::slotBit(context.targetTrack);
    switch (action) {
        case Action::MacroDelete: {
            if (context.targetTrack != beforeActive) {
                return PlanOutcome::Stale;
            }
            const auto mutation = core::state::shared::removeIndex(
                beforeMask,
                beforeActive,
                TrackBank::TRACK_COUNT
            );
            if (!mutation.changed) return PlanOutcome::Invalid;
            plan.afterEnabledMask = mutation.nextMask;
            plan.afterActiveTrack = mutation.nextActive;
            plan.capturedTrackMask = static_cast<uint16_t>(
                oldActiveBit |
                core::state::shared::slotBit(mutation.nextActive)
            );
            break;
        }
        case Action::MacroReset:
            if ((beforeMask & targetBit) == 0U) {
                return PlanOutcome::Stale;
            }
            plan.afterEnabledMask = beforeMask;
            plan.afterActiveTrack = beforeActive;
            plan.capturedTrackMask = static_cast<uint16_t>(
                oldActiveBit | targetBit
            );
            break;
        case Action::MacroPaste:
            plan.afterEnabledMask = static_cast<uint16_t>(
                beforeMask | targetBit
            );
            plan.afterActiveTrack = context.targetTrack;
            plan.capturedTrackMask = static_cast<uint16_t>(
                oldActiveBit | targetBit
            );
            break;
        case Action::MacroCreate:
            if ((beforeMask & targetBit) != 0U) {
                return PlanOutcome::Stale;
            }
            plan.afterEnabledMask = static_cast<uint16_t>(
                beforeMask | targetBit
            );
            plan.afterActiveTrack = context.targetTrack;
            plan.capturedTrackMask = static_cast<uint16_t>(
                oldActiveBit | targetBit
            );
            break;
        default:
            return PlanOutcome::Invalid;
    }
    plan.macroCapturedTrackMask = plan.capturedTrackMask;
    if (!fillFocus(context, plan)) return PlanOutcome::Invalid;
    out = plan;
    return PlanOutcome::Ready;
}

FLASHMEM MacroOutcome prepareMacroAfter(
    const void* opaque,
    const Plan& plan,
    std::array<
        core::state::macro::MacroTrackData,
        core::state::macro::TRACK_COUNT>& afterTracks,
    core::state::modulation::ProjectControlDomainState& afterControl
) noexcept {
    if (opaque == nullptr) return MacroOutcome::Invalid;
    const auto& context = *static_cast<const DirectContext*>(opaque);
    if (!intentStillMatches(context) ||
        plan.action != context.action ||
        plan.targetTrack != context.targetTrack) {
        return MacroOutcome::Stale;
    }

    const uint16_t targetBit =
        core::state::shared::slotBit(plan.targetTrack);
    switch (plan.action) {
        case Action::MacroDelete:
            return macro_structure_automation_ops::clearTracksInDomain(
                       afterControl,
                       targetBit
                   )
                ? MacroOutcome::Ready
                : MacroOutcome::Invalid;
        case Action::MacroReset:
        case Action::MacroCreate:
            afterTracks[plan.targetTrack].initDefaults(plan.targetTrack);
            return macro_structure_automation_ops::clearTracksInDomain(
                       afterControl,
                       targetBit
                   )
                ? MacroOutcome::Ready
                : MacroOutcome::Invalid;
        case Action::MacroPaste:
            afterTracks[plan.targetTrack] = *context.pasteTrack;
            return macro_structure_automation_ops::
                    replaceTrackFromClipboardInDomain(
                        afterControl,
                        plan.targetTrack,
                        context.pasteAutomation
                    )
                ? MacroOutcome::Ready
                : MacroOutcome::Invalid;
        default:
            return MacroOutcome::Invalid;
    }
}

FLASHMEM bool revalidate(
    const void* opaque,
    const Plan& plan,
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change
) noexcept {
    if (opaque == nullptr || !change.macroStructure) return false;
    const auto& context = *static_cast<const DirectContext*>(opaque);
    if (!intentStillMatches(context) || plan.action != context.action ||
        plan.targetTrack != context.targetTrack) {
        return false;
    }
    if (plan.action != Action::MacroPaste) return true;
    return std::memcmp(
               &change.macroStructure->afterTracks[plan.targetTrack],
               context.pasteTrack,
               sizeof(*context.pasteTrack)
           ) == 0;
}

FLASHMEM bool syncsActivePresentation(const Plan& plan) noexcept {
    return plan.action != Action::MacroReset ||
           plan.targetTrack == plan.afterActiveTrack;
}

FLASHMEM void clearManualAndMaybeSync(
    DirectContext& context,
    const Plan& plan
) noexcept {
    (void)context.state.macroUi.manualOverrides.clearTrack(plan.targetTrack);
    if (syncsActivePresentation(plan)) {
        core::state::macro::MacroWorkflow::syncActivePagePresentation(
            context.state.macros,
            context.state.pages,
            context.state.macroUi
        );
    }
}

FLASHMEM void reconcileCommitted(
    void* opaque,
    const Plan& plan,
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change
) noexcept {
    (void)change;
    auto& context = *static_cast<DirectContext*>(opaque);
    clearManualAndMaybeSync(context, plan);
    context.state.configRevision.set(
        core::state::macro::nextMacroConfigRevision(
            context.state.configRevision.get()
        )
    );
}

FLASHMEM void settleNoChange(void* opaque, const Plan& plan) noexcept {
    auto& context = *static_cast<DirectContext*>(opaque);
    clearManualAndMaybeSync(context, plan);
}

FLASHMEM void settleSuccessful(void*, const Plan&) noexcept {}

const SequencerPreparedTrackStructureExecution::Operations kDirectOperations{
    .buildPlan = buildPlan,
    .prepareMacroAfter = prepareMacroAfter,
    .revalidate = revalidate,
    .reconcileCommitted = reconcileCommitted,
    .settleNoChange = settleNoChange,
    .settleSuccessful = settleSuccessful,
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
FLASHMEM Result executePrepared(DirectContext& context) {
    auto& state = context.state;
    return executeSequencerTrackStructureTransaction(
        SequencerPreparedTrackStructureStateRefs{
            state.sequencerTracks,
            state.sequencer,
            &state.pages,
            state.sequencerTrackActivations,
            SharedTrackDomainServices::fromCoreState(state),
            SequencerHistoryDomainServices::fromCoreState(state),
        },
        context.action,
        SequencerPreparedTrackStructureExecution::
            fromStaticOperations<kDirectOperations>(&context)
    );
}

FLASHMEM Result executeDirect(
    core::state::CoreState& state,
    Action action,
    uint8_t targetTrack,
    const core::state::macro::MacroTrackData* pasteTrack,
    const core::state::MacroAutomationClipboard* pasteAutomation
) {
    // Draft owns Track transition priority. Mirror the kernel's first gate so
    // adapter-local validation cannot skip its rejection marker.
    if (state.sequencer.stepContentDraft.rejectTransitionIfActive(
            core::state::sequencer::
                SequencerStepContentDraftBlockedTransition::TRACK
        )) {
        return {Status::DraftBlocked, {}};
    }
    switch (action) {
        case Action::MacroDelete:
        case Action::MacroReset:
        case Action::MacroPaste:
        case Action::MacroCreate:
            break;
        default:
            return {Status::Invalid, {}};
    }

    DirectContext context{
        .state = state,
        .action = action,
        .targetTrack = targetTrack,
        .pasteTrack = pasteTrack,
        .pasteAutomation = pasteAutomation,
    };
    context.intent = captureIntent(context);
    context.pasteTrackFingerprint = fingerprintTrack(pasteTrack);
    context.pasteAutomationFingerprint =
        fingerprintAutomation(pasteAutomation);
    if (!validIntent(context) || !pasteSourcesMatch(context)) {
        return {Status::Invalid, {}};
    }
    switch (validateInitialTopology(context)) {
        case InitialTopologyOutcome::Ready:
            break;
        case InitialTopologyOutcome::Stale:
            return {Status::Stale, {}};
        case InitialTopologyOutcome::Invalid:
        default:
            return {Status::Invalid, {}};
    }

    return executePrepared(context);
}

}  // namespace

FLASHMEM Result executeMacroDeleteTrackStructure(
    core::state::CoreState& state
) {
    return executeDirect(
        state,
        Action::MacroDelete,
        state.sharedTrackActive.get(),
        nullptr,
        nullptr
    );
}

FLASHMEM Result executeMacroResetTrackStructure(
    core::state::CoreState& state,
    uint8_t targetTrack
) {
    return executeDirect(
        state,
        Action::MacroReset,
        targetTrack,
        nullptr,
        nullptr
    );
}

FLASHMEM Result executeMacroPasteTrackStructure(
    core::state::CoreState& state,
    uint8_t targetTrack,
    const core::state::macro::MacroTrackData& track,
    const core::state::MacroAutomationClipboard* automation
) {
    return executeDirect(
        state,
        Action::MacroPaste,
        targetTrack,
        &track,
        automation
    );
}

FLASHMEM Result executeMacroCreateTrackStructure(
    core::state::CoreState& state,
    uint8_t targetTrack
) {
    return executeDirect(
        state,
        Action::MacroCreate,
        targetTrack,
        nullptr,
        nullptr
    );
}

}  // namespace core::handler

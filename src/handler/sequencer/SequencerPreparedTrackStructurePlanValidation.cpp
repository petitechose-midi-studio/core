#include "handler/sequencer/SequencerPreparedTrackStructurePlanValidation.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/shared/StructureSlotOps.hpp"

namespace core::handler::prepared_track_structure_detail {
namespace {

using Action = SequencerPreparedTrackStructureAction;
using Plan = SequencerPreparedTrackStructurePlan;
using TrackBank = core::state::sequencer::SequencerTrackBankState;

static_assert(
    core::state::macro::TRACK_COUNT == TrackBank::TRACK_COUNT,
    "prepared Track Structure masks require aligned Sequencer/Macro domains"
);

}  // namespace

FLASHMEM bool actionIsValid(Action action) noexcept {
    switch (action) {
        case Action::SequencerCreate:
        case Action::SequencerRemoveCurrent:
        case Action::SequencerRemoveSelection:
        case Action::MacroDelete:
        case Action::MacroReset:
        case Action::MacroPaste:
        case Action::MacroCreate:
            return true;
        default:
            return false;
    }
}

FLASHMEM bool isMacroAction(Action action) noexcept {
    switch (action) {
        case Action::MacroDelete:
        case Action::MacroReset:
        case Action::MacroPaste:
        case Action::MacroCreate:
            return true;
        default:
            return false;
    }
}

FLASHMEM bool allowsTransientNoChange(Action action) noexcept {
    return action == Action::MacroReset || action == Action::MacroPaste;
}

FLASHMEM uint16_t trackBit(uint8_t track) noexcept {
    return static_cast<uint16_t>(1U << track);
}

FLASHMEM uint8_t trackCount(uint16_t mask) noexcept {
    uint8_t count = 0U;
    while (mask != 0U) {
        count = static_cast<uint8_t>(count + (mask & 1U));
        mask = static_cast<uint16_t>(mask >> 1U);
    }
    return count;
}

FLASHMEM bool samePlan(const Plan& lhs, const Plan& rhs) noexcept {
    return lhs.action == rhs.action &&
           lhs.beforeEnabledMask == rhs.beforeEnabledMask &&
           lhs.afterEnabledMask == rhs.afterEnabledMask &&
           lhs.affectedTrackMask == rhs.affectedTrackMask &&
           lhs.capturedTrackMask == rhs.capturedTrackMask &&
           lhs.canonicalResetTrackMask == rhs.canonicalResetTrackMask &&
           lhs.macroCapturedTrackMask == rhs.macroCapturedTrackMask &&
           lhs.beforeActiveTrack == rhs.beforeActiveTrack &&
           lhs.afterActiveTrack == rhs.afterActiveTrack &&
           lhs.beforeFocusedStep == rhs.beforeFocusedStep &&
           lhs.afterFocusedStep == rhs.afterFocusedStep &&
           lhs.beforePage == rhs.beforePage &&
           lhs.afterPage == rhs.afterPage &&
           lhs.targetTrack == rhs.targetTrack &&
           lhs.macroAffectedTrack == rhs.macroAffectedTrack &&
           lhs.incomingOwnerPolicy == rhs.incomingOwnerPolicy;
}

namespace {

FLASHMEM bool validCommonPlan(
    const Plan& plan,
    Action requestedAction,
    const TrackBank& tracks,
    const core::state::sequencer::SequencerState& sequencer,
    const SharedTrackDomainServices& sharedTracks,
    const core::state::macro::MacroPagesState* macroPages
) noexcept {
    if (!actionIsValid(requestedAction) || plan.action != requestedAction ||
        plan.beforeEnabledMask == 0U ||
        plan.afterEnabledMask == 0U ||
        plan.beforeActiveTrack >= TrackBank::TRACK_COUNT ||
        plan.afterActiveTrack >= TrackBank::TRACK_COUNT ||
        (plan.beforeEnabledMask & trackBit(plan.beforeActiveTrack)) == 0U ||
        (plan.afterEnabledMask & trackBit(plan.afterActiveTrack)) == 0U ||
        plan.affectedTrackMask == 0U ||
        plan.capturedTrackMask == 0U ||
        trackCount(plan.capturedTrackMask) > 2U ||
        (plan.canonicalResetTrackMask &
         static_cast<uint16_t>(~plan.capturedTrackMask)) != 0U ||
        plan.beforeEnabledMask != tracks.currentEnabledMask() ||
        plan.beforeActiveTrack != tracks.activeTrackIndex() ||
        plan.beforeEnabledMask != sharedTracks.enabledMask() ||
        plan.beforeActiveTrack != sharedTracks.activeTrack() ||
        plan.beforeFocusedStep != sequencer.focusedStep.get() ||
        plan.beforePage != sequencer.page.get() ||
        plan.beforeFocusedStep >= sequencer.pattern.length.get() ||
        plan.beforePage >= core::state::sequencer::SequencerState::PAGE_COUNT ||
        (macroPages != nullptr &&
         (macroPages->currentTrackEnabledMask() != plan.beforeEnabledMask ||
          macroPages->currentActiveTrack() != plan.beforeActiveTrack))) {
        return false;
    }

    const bool activeChanges =
        plan.beforeActiveTrack != plan.afterActiveTrack;
    const uint8_t incomingLength =
        (plan.canonicalResetTrackMask & trackBit(plan.afterActiveTrack)) != 0U
            ? core::state::sequencer::SequencerPatternState::DEFAULT_LENGTH
            : (activeChanges
                   ? tracks.track(plan.afterActiveTrack).length.get()
                   : sequencer.pattern.length.get());
    if (incomingLength == 0U ||
        incomingLength > core::state::sequencer::SequencerState::MAX_STEPS) {
        return false;
    }
    const uint8_t expectedFocusedStep = activeChanges
        ? std::min<uint8_t>(
              plan.beforeFocusedStep,
              static_cast<uint8_t>(incomingLength - 1U)
          )
        : plan.beforeFocusedStep;
    const uint8_t expectedPage = activeChanges
        ? static_cast<uint8_t>(
              expectedFocusedStep /
              core::state::sequencer::SequencerState::STEPS_PER_PAGE
          )
        : plan.beforePage;
    return plan.afterFocusedStep == expectedFocusedStep &&
           plan.afterPage == expectedPage;
}

}  // namespace

FLASHMEM bool validActionPlan(
    const Plan& plan,
    Action requestedAction,
    const TrackBank& tracks,
    const core::state::sequencer::SequencerState& sequencer,
    const SharedTrackDomainServices& sharedTracks,
    const core::state::macro::MacroPagesState* macroPages
) noexcept {
    if (!validCommonPlan(
            plan,
            requestedAction,
            tracks,
            sequencer,
            sharedTracks,
            macroPages
        )) {
        return false;
    }

    const uint16_t oldActiveBit = trackBit(plan.beforeActiveTrack);
    const uint16_t newActiveBit = trackBit(plan.afterActiveTrack);
    const uint16_t activePair = static_cast<uint16_t>(
        oldActiveBit | newActiveBit
    );
    const uint8_t invalidMacroTrack = core::state::sequencer::
        SequencerHistoryMacroTrackStructurePayload::INVALID_AFFECTED_TRACK;
    const bool macroAction = isMacroAction(requestedAction);
    if (macroAction) {
        if (plan.macroCapturedTrackMask != plan.capturedTrackMask ||
            plan.macroAffectedTrack != plan.targetTrack) {
            return false;
        }
    } else if (plan.macroCapturedTrackMask != 0U ||
               plan.macroAffectedTrack != invalidMacroTrack) {
        return false;
    }

    const auto preserve = core::state::sequencer::
        SequencerActiveTrackIncomingOwnerPolicy::Preserve;
    const auto reset = core::state::sequencer::
        SequencerActiveTrackIncomingOwnerPolicy::Reset;
    switch (requestedAction) {
        case Action::SequencerCreate: {
            if (plan.targetTrack >= TrackBank::TRACK_COUNT) return false;
            const uint16_t targetBit = trackBit(plan.targetTrack);
            return (plan.beforeEnabledMask & targetBit) == 0U &&
                   plan.afterEnabledMask ==
                       static_cast<uint16_t>(
                           plan.beforeEnabledMask | targetBit
                       ) &&
                   plan.afterActiveTrack == plan.targetTrack &&
                   plan.affectedTrackMask == targetBit &&
                   plan.capturedTrackMask ==
                       static_cast<uint16_t>(oldActiveBit | targetBit) &&
                   plan.canonicalResetTrackMask == targetBit &&
                   plan.incomingOwnerPolicy == reset;
        }
        case Action::SequencerRemoveCurrent:
        case Action::MacroDelete: {
            if (plan.targetTrack != plan.beforeActiveTrack) return false;
            const auto mutation = core::state::shared::removeIndex(
                plan.beforeEnabledMask,
                plan.beforeActiveTrack,
                TrackBank::TRACK_COUNT
            );
            return mutation.changed &&
                   plan.afterEnabledMask == mutation.nextMask &&
                   plan.afterActiveTrack == mutation.nextActive &&
                   plan.affectedTrackMask == oldActiveBit &&
                   plan.capturedTrackMask == activePair &&
                   plan.canonicalResetTrackMask == 0U &&
                   plan.incomingOwnerPolicy == preserve;
        }
        case Action::SequencerRemoveSelection: {
            if (plan.targetTrack != TrackBank::TRACK_COUNT ||
                (plan.affectedTrackMask &
                 static_cast<uint16_t>(~plan.beforeEnabledMask)) != 0U) {
                return false;
            }
            const uint16_t nextMask = static_cast<uint16_t>(
                plan.beforeEnabledMask &
                static_cast<uint16_t>(~plan.affectedTrackMask)
            );
            if (nextMask == 0U) return false;
            const uint8_t nextActive = (nextMask & oldActiveBit) != 0U
                ? plan.beforeActiveTrack
                : core::state::shared::nextEnabledIndex(
                      nextMask,
                      plan.beforeActiveTrack,
                      TrackBank::TRACK_COUNT
                  );
            return plan.afterEnabledMask == nextMask &&
                   plan.afterActiveTrack == nextActive &&
                   plan.capturedTrackMask == activePair &&
                   plan.canonicalResetTrackMask == 0U &&
                   plan.incomingOwnerPolicy == preserve;
        }
        case Action::MacroReset: {
            if (plan.targetTrack >= TrackBank::TRACK_COUNT) return false;
            const uint16_t targetBit = trackBit(plan.targetTrack);
            return (plan.beforeEnabledMask & targetBit) != 0U &&
                   plan.afterEnabledMask == plan.beforeEnabledMask &&
                   plan.afterActiveTrack == plan.beforeActiveTrack &&
                   plan.affectedTrackMask == targetBit &&
                   plan.capturedTrackMask ==
                       static_cast<uint16_t>(oldActiveBit | targetBit) &&
                   plan.canonicalResetTrackMask == 0U &&
                   plan.incomingOwnerPolicy == preserve;
        }
        case Action::MacroPaste: {
            if (plan.targetTrack >= TrackBank::TRACK_COUNT) return false;
            const uint16_t targetBit = trackBit(plan.targetTrack);
            return plan.afterEnabledMask ==
                       static_cast<uint16_t>(
                           plan.beforeEnabledMask | targetBit
                       ) &&
                   plan.afterActiveTrack == plan.targetTrack &&
                   plan.affectedTrackMask == targetBit &&
                   plan.capturedTrackMask ==
                       static_cast<uint16_t>(oldActiveBit | targetBit) &&
                   plan.canonicalResetTrackMask == 0U &&
                   plan.incomingOwnerPolicy == preserve;
        }
        case Action::MacroCreate: {
            if (plan.targetTrack >= TrackBank::TRACK_COUNT) return false;
            const uint16_t targetBit = trackBit(plan.targetTrack);
            return (plan.beforeEnabledMask & targetBit) == 0U &&
                   plan.afterEnabledMask ==
                       static_cast<uint16_t>(
                           plan.beforeEnabledMask | targetBit
                       ) &&
                   plan.afterActiveTrack == plan.targetTrack &&
                   plan.affectedTrackMask == targetBit &&
                   plan.capturedTrackMask ==
                       static_cast<uint16_t>(oldActiveBit | targetBit) &&
                   plan.canonicalResetTrackMask == 0U &&
                   plan.incomingOwnerPolicy == preserve;
        }
        default:
            return false;
    }
}

FLASHMEM bool validOperations(
    const SequencerPreparedTrackStructureExecution::Operations* operations,
    Action action
) noexcept {
    if (operations == nullptr || operations->buildPlan == nullptr ||
        operations->revalidate == nullptr ||
        operations->reconcileCommitted == nullptr ||
        operations->settleSuccessful == nullptr) {
        return false;
    }
    if (isMacroAction(action) && operations->prepareMacroAfter == nullptr) {
        return false;
    }
    return !allowsTransientNoChange(action) ||
           operations->settleNoChange != nullptr;
}

}  // namespace core::handler::prepared_track_structure_detail

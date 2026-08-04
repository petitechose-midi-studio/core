#include "handler/common/SharedTrackDomainServices.hpp"

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"

namespace core::handler {

namespace {

constexpr uint64_t kCheckpointFnvOffset = 14695981039346656037ULL;
constexpr uint64_t kCheckpointFnvPrime = 1099511628211ULL;

[[noreturn]] FLASHMEM void failPreparedTrackPublicationInvariant() {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_trap();
#else
    for (;;) {}
#endif
}

FLASHMEM uint64_t mixCheckpointValue(
    uint64_t hash,
    uint64_t value
) noexcept {
    for (uint8_t byte = 0U; byte < sizeof(value); ++byte) {
        hash ^= static_cast<uint8_t>(value & 0xFFU);
        hash *= kCheckpointFnvPrime;
        value >>= 8U;
    }
    return hash;
}

FLASHMEM uint64_t projectModulatorNavigationFingerprint(
    const core::state::project::ProjectNavigationState& navigation
) noexcept {
    uint64_t hash = kCheckpointFnvOffset;
    const auto mix = [&hash](uint64_t value) {
        hash = mixCheckpointValue(hash, value);
    };
    mix(static_cast<uint8_t>(navigation.activeTab.get()));
    mix(static_cast<uint8_t>(navigation.currentNode.get()));
    mix(navigation.depth.get());
    mix(navigation.focusedRow.get());
    mix(navigation.selectedModulator.value);
    mix(navigation.selectedModulationBinding.value);
    mix(navigation.modulatorReturn.sourceId.value);
    mix(navigation.modulatorReturn.bindingId.value);
    mix(navigation.modulatorReturn.macroAddress.track);
    mix(navigation.modulatorReturn.macroAddress.page);
    mix(navigation.modulatorReturn.macroAddress.macro);
    mix(static_cast<uint8_t>(navigation.modulatorReturn.caller));
    mix(static_cast<uint8_t>(navigation.modulatorReturn.target));
    mix(navigation.modulatorReturn.focusedRow);
    mix(navigation.guardedModulator.value);
    mix(navigation.guardedModulationBinding.value);
    mix(navigation.guardedClipboardModulator.value);
    const auto mixGuard = [&mix](
        const core::state::contextual::GuardedActionState& guard
    ) {
        mix(static_cast<uint8_t>(guard.phase));
        mix(guard.pressedAtMs);
        mix(guard.armedAtMs);
        mix(guard.guardDurationMs);
        mix(guard.progressPermille);
    };
    mixGuard(navigation.modulatorGuard.get());
    mixGuard(navigation.modulatorClipboardGuard.get());
    mix(navigation.modulatorClipboardPasteAvailable ? 1U : 0U);
    mix(navigation.creatingModulatorSource ? 1U : 0U);
    mix(static_cast<uint8_t>(navigation.creatingModulatorKind));
    mix(navigation.destinationPickerTrack);
    mix(navigation.destinationPickerPage);
    mix(static_cast<uint8_t>(navigation.destinationPickerLevel));
    for (const auto node : navigation.pathStack) {
        mix(static_cast<uint8_t>(node));
    }
    for (const uint8_t row : navigation.focusedRowByDepth) mix(row);
    mix(navigation.projectNameShiftActive ? 1U : 0U);
    return hash;
}

FLASHMEM bool setSharedTrackStateFromCoreState(
    void* context,
    uint16_t enabledMask,
    uint8_t activeTrack
) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->setSharedTrackState(enabledMask, activeTrack);
}

FLASHMEM void publishPreparedSequencerStateFromCoreState(
    void* context,
    uint16_t enabledMask,
    uint8_t activeTrack
) noexcept {
    if (context == nullptr) failPreparedTrackPublicationInvariant();

    auto* state = static_cast<core::state::CoreState*>(context);
    if (!state->publishPreparedSequencerTrackState(
            enabledMask,
            activeTrack
        )) {
        failPreparedTrackPublicationInvariant();
    }
}

FLASHMEM void reconcilePreparedTrackPresentationFromCoreState(
    void* context,
    PreparedTrackPresentationKind kind,
    uint16_t capturedTrackMask
) noexcept {
    if (context == nullptr) failPreparedTrackPublicationInvariant();
    auto& state = *static_cast<core::state::CoreState*>(context);
    switch (kind) {
        case PreparedTrackPresentationKind::MacroTrackTransfer:
            state.reconcilePreparedMacroTrackTransfer(capturedTrackMask);
            return;
        case PreparedTrackPresentationKind::SequencerActiveTrack:
            state.reconcilePreparedSequencerActiveTrackPresentation();
            return;
        default:
            failPreparedTrackPublicationInvariant();
    }
}

FLASHMEM bool capturePreparedTrackStructureSettlementCheckpointFromCoreState(
    const void* context,
    PreparedTrackStructureSettlementCheckpoint& out
) noexcept {
    out = {};
    if (context == nullptr) return false;

    const auto& state = *static_cast<const core::state::CoreState*>(context);
    out.projectModulatorNavigationFingerprint =
        projectModulatorNavigationFingerprint(state.projectNavigation);
    out.manualOverrideRevision = state.macroUi.manualOverrides.revision;
    out.manualOverrideRejectedActivationCount =
        state.macroUi.manualOverrides.rejectedActivationCount;
    out.controlAuthoredRevision = state.pages.control.authoredRevision;
    out.configRevision = state.configRevision.get();
    out.automationEditRevision = state.macroUi.automationEditRevision.get();
    out.runtimeProjectionRevision =
        state.macroUi.runtimeProjectionRevision.get();
    out.manualOverrideMask =
        state.macroUi.automationManualOverrideMask.get();
    out.projectNavigationRevision =
        state.projectNavigation.contentRevision.get();
    return true;
}

FLASHMEM bool samePreparedTrackStructureSettlementCheckpoint(
    const PreparedTrackStructureSettlementCheckpoint& lhs,
    const PreparedTrackStructureSettlementCheckpoint& rhs
) {
    return lhs.projectModulatorNavigationFingerprint ==
               rhs.projectModulatorNavigationFingerprint &&
           lhs.manualOverrideRevision == rhs.manualOverrideRevision &&
           lhs.manualOverrideRejectedActivationCount ==
               rhs.manualOverrideRejectedActivationCount &&
           lhs.controlAuthoredRevision == rhs.controlAuthoredRevision &&
           lhs.configRevision == rhs.configRevision &&
           lhs.automationEditRevision == rhs.automationEditRevision &&
           lhs.runtimeProjectionRevision == rhs.runtimeProjectionRevision &&
           lhs.manualOverrideMask == rhs.manualOverrideMask &&
           lhs.projectNavigationRevision == rhs.projectNavigationRevision;
}

}  // namespace

FLASHMEM SharedTrackDomainServices::SharedTrackDomainServices(StateRefs state)
    : SharedTrackDomainServices(state, Operations{}) {}

FLASHMEM SharedTrackDomainServices::SharedTrackDomainServices(StateRefs state, Operations operations)
    : active_track_(&state.activeTrack)
    , enabled_mask_(&state.enabledMask)
    , operations_(operations) {}

FLASHMEM SharedTrackDomainServices SharedTrackDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return SharedTrackDomainServices{
        StateRefs{
            state.sharedTrackActive,
            state.sharedTrackEnabledMask,
        },
        Operations{
            &state,
            setSharedTrackStateFromCoreState,
            publishPreparedSequencerStateFromCoreState,
            reconcilePreparedTrackPresentationFromCoreState,
            capturePreparedTrackStructureSettlementCheckpointFromCoreState,
        },
    };
}

FLASHMEM uint16_t SharedTrackDomainServices::enabledMask() const {
    return enabled_mask_->get();
}

FLASHMEM uint8_t SharedTrackDomainServices::activeTrack() const {
    return active_track_->get();
}

FLASHMEM bool SharedTrackDomainServices::setState(uint16_t enabledMask, uint8_t activeTrack) const {
    return operations_.setSharedTrackState != nullptr &&
           operations_.setSharedTrackState(operations_.context, enabledMask, activeTrack);
}

FLASHMEM bool SharedTrackDomainServices::canPublishPreparedSequencerState() const {
    return operations_.context != nullptr &&
           operations_.publishPreparedSequencerState != nullptr;
}

FLASHMEM void SharedTrackDomainServices::publishPreparedSequencerState(
    uint16_t enabledMask,
    uint8_t activeTrack
) const noexcept {
    operations_.publishPreparedSequencerState(
        operations_.context,
        enabledMask,
        activeTrack
    );
}

FLASHMEM void
SharedTrackDomainServices::reconcilePreparedMacroTrackTransfer(
    uint16_t capturedTrackMask
) const noexcept {
    if (operations_.context == nullptr ||
        operations_.reconcilePreparedTrackPresentation == nullptr) {
        return;
    }
    operations_.reconcilePreparedTrackPresentation(
        operations_.context,
        PreparedTrackPresentationKind::MacroTrackTransfer,
        capturedTrackMask
    );
}

FLASHMEM bool
SharedTrackDomainServices::capturePreparedTrackStructureSettlementCheckpoint(
    PreparedTrackStructureSettlementCheckpoint& out
) const {
    out = {};
    return operations_.capturePreparedTrackStructureSettlementCheckpoint !=
               nullptr &&
           operations_.capturePreparedTrackStructureSettlementCheckpoint(
               operations_.context,
               out
           );
}

FLASHMEM bool
SharedTrackDomainServices::preparedTrackStructureSettlementCheckpointMatches(
    const PreparedTrackStructureSettlementCheckpoint& expected
) const {
    PreparedTrackStructureSettlementCheckpoint actual{};
    return capturePreparedTrackStructureSettlementCheckpoint(actual) &&
           samePreparedTrackStructureSettlementCheckpoint(actual, expected);
}

FLASHMEM bool SharedTrackDomainServices::
canReconcilePreparedSequencerActiveTrackPresentation() const {
    return operations_.context != nullptr &&
           operations_.reconcilePreparedTrackPresentation != nullptr;
}

FLASHMEM void
SharedTrackDomainServices::reconcilePreparedSequencerActiveTrackPresentation()
    const noexcept {
    operations_.reconcilePreparedTrackPresentation(
        operations_.context,
        PreparedTrackPresentationKind::SequencerActiveTrack,
        0U
    );
}

}  // namespace core::handler

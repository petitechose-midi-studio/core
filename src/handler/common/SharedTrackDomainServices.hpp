#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {
struct CoreState;
}

namespace core::handler {

enum class PreparedTrackPresentationKind : uint8_t {
    MacroTrackTransfer = 0U,
    SequencerActiveTrack,
};

/**
 * Exact Core-derived settlement state captured immediately after the common
 * Track Structure chronology boundary. UI callbacks may retain their own
 * pre-call tokens; this checkpoint protects only shared derived state that the
 * final no-fail publication tail is allowed to reconcile.
 */
struct PreparedTrackStructureSettlementCheckpoint {
    uint64_t projectModulatorNavigationFingerprint = 0U;
    uint32_t manualOverrideRevision = 0U;
    uint32_t manualOverrideRejectedActivationCount = 0U;
    uint32_t controlAuthoredRevision = 0U;
    uint32_t configRevision = 0U;
    uint32_t automationEditRevision = 0U;
    uint32_t runtimeProjectionRevision = 0U;
    uint16_t manualOverrideMask = 0U;
    uint8_t projectNavigationRevision = 0U;
};

static_assert(
    sizeof(PreparedTrackStructureSettlementCheckpoint) <= 40U,
    "prepared Track Structure settlement checkpoint must remain scalar"
);

/**
 * Shared-track mutation facade for macro and sequencer handlers.
 *
 * Read access comes from shared track signals. Production writes go through a
 * CoreState operation so macro, sequencer, settings, and persistence stay in sync.
 */
class SharedTrackDomainServices {
public:
    struct StateRefs {
        oc::state::Signal<uint8_t, 8>& activeTrack;
        oc::state::Signal<uint16_t, 16>& enabledMask;
    };

    using SetSharedTrackStateFn = bool (*)(void* context,
                                           uint16_t enabledMask,
                                           uint8_t activeTrack);
    using PublishPreparedSequencerStateFn = void (*)(
        void* context,
        uint16_t enabledMask,
        uint8_t activeTrack
    ) noexcept;
    using ReconcilePreparedTrackPresentationFn = void (*)(
        void* context,
        PreparedTrackPresentationKind kind,
        uint16_t capturedTrackMask
    ) noexcept;
    using CapturePreparedTrackStructureSettlementCheckpointFn = bool (*)(
        const void* context,
        PreparedTrackStructureSettlementCheckpoint& out
    ) noexcept;
    struct Operations {
        void* context = nullptr;
        SetSharedTrackStateFn setSharedTrackState = nullptr;
        PublishPreparedSequencerStateFn publishPreparedSequencerState = nullptr;
        ReconcilePreparedTrackPresentationFn
            reconcilePreparedTrackPresentation = nullptr;
        CapturePreparedTrackStructureSettlementCheckpointFn
            capturePreparedTrackStructureSettlementCheckpoint = nullptr;
    };

    explicit SharedTrackDomainServices(StateRefs state);
    SharedTrackDomainServices(StateRefs state, Operations operations);
    static SharedTrackDomainServices fromCoreState(core::state::CoreState& state);

    uint16_t enabledMask() const;
    uint8_t activeTrack() const;
    bool setState(uint16_t enabledMask, uint8_t activeTrack) const;
    [[nodiscard]] bool canPublishPreparedSequencerState() const;
    void publishPreparedSequencerState(
        uint16_t enabledMask,
        uint8_t activeTrack
    ) const noexcept;
    void reconcilePreparedMacroTrackTransfer(
        uint16_t capturedTrackMask
    ) const;
    [[nodiscard]] bool capturePreparedTrackStructureSettlementCheckpoint(
        PreparedTrackStructureSettlementCheckpoint& out
    ) const;
    [[nodiscard]] bool preparedTrackStructureSettlementCheckpointMatches(
        const PreparedTrackStructureSettlementCheckpoint& expected
    ) const;
    [[nodiscard]] bool
    canReconcilePreparedSequencerActiveTrackPresentation() const;
    void reconcilePreparedSequencerActiveTrackPresentation() const noexcept;

private:
    oc::state::Signal<uint8_t, 8>* active_track_ = nullptr;
    oc::state::Signal<uint16_t, 16>* enabled_mask_ = nullptr;
    Operations operations_{};
};

static_assert(
    sizeof(void*) != 4U ||
        sizeof(SharedTrackDomainServices::Operations) == 20U,
    "shared Track operations exceed their ARM ABI contract"
);
static_assert(
    sizeof(void*) != 8U ||
        sizeof(SharedTrackDomainServices::Operations) == 40U,
    "shared Track operations exceed their native ABI contract"
);
static_assert(
    sizeof(void*) != 4U || sizeof(SharedTrackDomainServices) == 28U,
    "shared Track facade exceeds its ARM ABI contract"
);
static_assert(
    sizeof(void*) != 8U || sizeof(SharedTrackDomainServices) == 56U,
    "shared Track facade exceeds its native ABI contract"
);

}  // namespace core::handler

#include "state/macro/MacroUiState.hpp"

#include <cmath>

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

namespace {

static_assert(TRACK_COUNT <= 16U);
static_assert(PAGE_COUNT <= 16U);
static_assert(MACRO_COUNT <= 16U);

constexpr uint8_t runtimeProjectionContextFor(uint8_t track, uint8_t page) {
    if (track >= TRACK_COUNT || page >= PAGE_COUNT) {
        return MacroUiState::INVALID_RUNTIME_PROJECTION_CONTEXT;
    }
    return static_cast<uint8_t>((track << 4U) | page);
}

FLASHMEM MacroUiState::RuntimeValueProjection makeRuntimeProjection(
    const MacroResolvedValue& value,
    float modulationDepth
) {
    return {
        .base = value.base,
        .modulation = value.modulation,
        .resolved = value.resolved,
        .modulationDepth = macroAutomationClamp01(modulationDepth),
        .valid = true,
        .modulationActive = value.modulationActive,
        .clippedLow = value.base + value.modulation < 0.0f,
        .clippedHigh = value.base + value.modulation > 1.0f,
    };
}

FLASHMEM bool sameRuntimeProjection(
    const MacroUiState::RuntimeValueProjection& lhs,
    const MacroUiState::RuntimeValueProjection& rhs
) {
    constexpr float EPSILON = 0.0005f;
    return lhs.valid == rhs.valid &&
        std::abs(lhs.base - rhs.base) < EPSILON &&
        std::abs(lhs.modulation - rhs.modulation) < EPSILON &&
        std::abs(lhs.resolved - rhs.resolved) < EPSILON &&
        std::abs(lhs.modulationDepth - rhs.modulationDepth) < EPSILON &&
        lhs.modulationActive == rhs.modulationActive &&
        lhs.clippedLow == rhs.clippedLow &&
        lhs.clippedHigh == rhs.clippedHigh;
}

}  // namespace

FLASHMEM MacroUiState::MacroUiState() = default;
FLASHMEM MacroUiState::~MacroUiState() = default;

FLASHMEM void MacroUiState::resetInteraction() {
    if (automationTake.phase == MacroAutomationTakePhase::RECORDING) {
        for (uint8_t macro = 0U; macro < MACRO_COUNT; ++macro) {
            const uint16_t bit = static_cast<uint16_t>(1U << macro);
            if ((automationTake.manualRestoreMask & bit) == 0U) continue;
            (void)manualOverrides.activate(
                MacroAutomationSlotAddress{
                    .track = automationTake.track,
                    .page = automationTake.page,
                    .macro = macro,
                },
                automationTake.previousManualValues[macro]
            );
        }
    }
    clutchActive.set(false);
    performanceOverlayMode.set(MacroPerformanceOverlayMode::NONE);
    activeProperty.set(MacroPerformanceProperty::VALUE);
    automationManualOverrideMask.set(0);
    focusedMacroSlot.set(0);
    previewAddPageSlot.set(false);
    previewPageIndex.set(0);
    pageHold.clear();
    pageSelection.reset(core::state::StructureSelectionScope::PAGE);
    selectionDeleteGuard.set({});
    selectionDeleteFeedback.set({});
    automationTake.reset();
    automationTakeHistory.reset();
    automationTakeDomain.reset();
    automationRecordingStatus.set(MacroAutomationRecordingStatus::IDLE);
    clearRuntimeProjections();
}

FLASHMEM void MacroUiState::resetProjectRuntime() {
    manualOverrides.clearProjectRuntime();
    automationManualOverrideMask.set(0);
}

FLASHMEM void MacroUiState::reset() {
    resetInteraction();
    resetProjectRuntime();
}

FLASHMEM void MacroUiState::refreshManualOverrideMask(uint8_t track, uint8_t page) {
    uint16_t mask = 0;
    if (track < TRACK_COUNT && page < PAGE_COUNT) {
        for (uint8_t macro = 0; macro < MACRO_COUNT; ++macro) {
            if (manualOverrides.activeFor(MacroAutomationSlotAddress{
                    .track = track,
                    .page = page,
                    .macro = macro,
                })) {
                mask = static_cast<uint16_t>(mask | static_cast<uint16_t>(1U << macro));
            }
        }
    }
    automationManualOverrideMask.set(mask);
}

FLASHMEM void MacroUiState::setRuntimeProjection(
    uint8_t track,
    uint8_t page,
    uint8_t macro,
    const MacroResolvedValue& value,
    float modulationDepth
) {
    const uint8_t nextContext = runtimeProjectionContextFor(track, page);
    if (macro >= runtimeProjections.size() ||
        nextContext == INVALID_RUNTIME_PROJECTION_CONTEXT) {
        return;
    }
    const bool contextChanged = runtimeProjectionContext != nextContext;
    if (contextChanged) {
        for (auto& projection : runtimeProjections) projection = {};
        runtimeProjectionContext = nextContext;
    }
    auto& target = runtimeProjections[macro];
    const auto next = makeRuntimeProjection(value, modulationDepth);
    if (!contextChanged && sameRuntimeProjection(target, next)) return;
    target = next;
    runtimeProjectionRevision.set(nextMacroRuntimeProjectionRevision(
        runtimeProjectionRevision.get(),
        contextChanged ? kMacroRuntimeProjectionDirtyAll : macro
    ));
}

FLASHMEM MacroUiState::RuntimeProjectionFrameTransaction
MacroUiState::beginRuntimeProjectionFrame() {
    RuntimeProjectionFrameTransaction transaction{};
    transaction.previousContext = runtimeProjectionContext;
    for (uint8_t macro = 0; macro < runtimeProjections.size(); ++macro) {
        if (runtimeProjections[macro].valid) {
            transaction.previousValidMask = static_cast<uint16_t>(
                transaction.previousValidMask |
                static_cast<uint16_t>(1U << macro)
            );
        }
    }
    transaction.active = true;
    // The context is unpublished until commit. Deferred Signal callbacks can
    // therefore never consume a partially staged frame.
    runtimeProjectionContext = INVALID_RUNTIME_PROJECTION_CONTEXT;
    return transaction;
}

FLASHMEM void MacroUiState::stageRuntimeProjection(
    RuntimeProjectionFrameTransaction& transaction,
    uint8_t macro,
    const MacroResolvedValue& value,
    float modulationDepth
) {
    if (!transaction.active || macro >= runtimeProjections.size()) return;
    const uint16_t bit = static_cast<uint16_t>(1U << macro);
    const auto next = makeRuntimeProjection(value, modulationDepth);
    if (!sameRuntimeProjection(runtimeProjections[macro], next)) {
        transaction.changedMask = static_cast<uint16_t>(
            transaction.changedMask | bit
        );
    }
    runtimeProjections[macro] = next;
    transaction.stagedValidMask = static_cast<uint16_t>(
        transaction.stagedValidMask | bit
    );
}

FLASHMEM void MacroUiState::commitRuntimeProjectionFrame(
    RuntimeProjectionFrameTransaction& transaction,
    uint8_t track,
    uint8_t page
) {
    const uint8_t nextContext = runtimeProjectionContextFor(track, page);
    if (!transaction.active ||
        nextContext == INVALID_RUNTIME_PROJECTION_CONTEXT) {
        cancelRuntimeProjectionFrame(transaction);
        return;
    }

    for (uint8_t macro = 0; macro < runtimeProjections.size(); ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((transaction.stagedValidMask & bit) != 0U) continue;
        if (runtimeProjections[macro].valid) {
            runtimeProjections[macro] = {};
            transaction.changedMask = static_cast<uint16_t>(
                transaction.changedMask | bit
            );
        }
    }

    const bool contextChanged =
        transaction.previousContext != nextContext ||
        transaction.previousValidMask != transaction.stagedValidMask;
    runtimeProjectionContext = nextContext;
    transaction.active = false;

    if (contextChanged) {
        runtimeProjectionRevision.set(nextMacroRuntimeProjectionRevision(
            runtimeProjectionRevision.get()
        ));
        return;
    }
    for (uint8_t macro = 0; macro < runtimeProjections.size(); ++macro) {
        const uint16_t bit = static_cast<uint16_t>(1U << macro);
        if ((transaction.changedMask & bit) == 0U) continue;
        runtimeProjectionRevision.set(nextMacroRuntimeProjectionRevision(
            runtimeProjectionRevision.get(),
            macro
        ));
    }
}

FLASHMEM void MacroUiState::cancelRuntimeProjectionFrame(
    RuntimeProjectionFrameTransaction& transaction
) {
    if (!transaction.active) return;
    const bool hadPublishedOrStagedProjection =
        transaction.previousContext != INVALID_RUNTIME_PROJECTION_CONTEXT ||
        transaction.previousValidMask != 0U ||
        transaction.stagedValidMask != 0U;
    for (auto& projection : runtimeProjections) projection = {};
    runtimeProjectionContext = INVALID_RUNTIME_PROJECTION_CONTEXT;
    transaction.active = false;
    if (hadPublishedOrStagedProjection) {
        runtimeProjectionRevision.set(nextMacroRuntimeProjectionRevision(
            runtimeProjectionRevision.get()
        ));
    }
}

FLASHMEM bool MacroUiState::runtimeProjectionValidFor(
    uint8_t track,
    uint8_t page,
    uint8_t macro
) const {
    return macro < runtimeProjections.size() &&
        runtimeProjectionContext == runtimeProjectionContextFor(track, page) &&
        runtimeProjections[macro].valid;
}

FLASHMEM void MacroUiState::clearRuntimeProjections() {
    bool hadProjection =
        runtimeProjectionContext != INVALID_RUNTIME_PROJECTION_CONTEXT;
    for (auto& projection : runtimeProjections) {
        hadProjection = hadProjection || projection.valid;
        projection = {};
    }
    runtimeProjectionContext = INVALID_RUNTIME_PROJECTION_CONTEXT;
    if (hadProjection) {
        runtimeProjectionRevision.set(nextMacroRuntimeProjectionRevision(
            runtimeProjectionRevision.get()
        ));
    }
}

}  // namespace core::state::macro

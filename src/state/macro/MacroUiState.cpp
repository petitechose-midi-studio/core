#include "state/macro/MacroUiState.hpp"

#include <cmath>

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

FLASHMEM MacroUiState::MacroUiState() = default;
FLASHMEM MacroUiState::~MacroUiState() = default;

FLASHMEM void MacroUiState::resetInteraction() {
    // Beginning a recording temporarily removes Manual so the live gesture can
    // own the value. A context teardown is a cancellation, not Resume Auto.
    if (automationRecording.active && automationRecording.restoreManualOnFailure) {
        (void)manualOverrides.activate(
            automationRecording.address,
            automationRecording.previousManualValue
        );
    }
    clutchActive.set(false);
    activeProperty.set(MacroPerformanceProperty::VALUE);
    automationManualOverrideMask.set(0);
    focusedMacroSlot.set(0);
    previewAddPageSlot.set(false);
    previewPageIndex.set(0);
    pageHold.clear();
    pageSelection.reset(core::state::StructureSelectionScope::PAGE);
    selectionDeleteGuard.set({});
    selectionDeleteFeedback.set({});
    automationRecording.reset();
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
    uint8_t macro,
    const MacroResolvedValue& value,
    float modulationDepth
) {
    if (macro >= runtimeProjections.size()) return;
    auto& target = runtimeProjections[macro];
    const float depth = macroAutomationClamp01(modulationDepth);
    const bool clippedLow = value.base + value.modulation < 0.0f;
    const bool clippedHigh = value.base + value.modulation > 1.0f;
    constexpr float EPSILON = 0.0005f;
    const bool changed = !target.valid ||
        std::abs(target.base - value.base) >= EPSILON ||
        std::abs(target.modulation - value.modulation) >= EPSILON ||
        std::abs(target.resolved - value.resolved) >= EPSILON ||
        std::abs(target.modulationDepth - depth) >= EPSILON ||
        target.modulationActive != value.modulationActive ||
        target.clippedLow != clippedLow ||
        target.clippedHigh != clippedHigh;
    if (!changed) return;
    target = RuntimeValueProjection{
        .base = value.base,
        .modulation = value.modulation,
        .resolved = value.resolved,
        .modulationDepth = depth,
        .valid = true,
        .modulationActive = value.modulationActive,
        .clippedLow = clippedLow,
        .clippedHigh = clippedHigh,
    };
    runtimeProjectionRevision.set(nextMacroRuntimeProjectionRevision(
        runtimeProjectionRevision.get(),
        macro
    ));
}

FLASHMEM void MacroUiState::clearRuntimeProjections() {
    bool hadProjection = false;
    for (auto& projection : runtimeProjections) {
        hadProjection = hadProjection || projection.valid;
        projection = {};
    }
    if (hadProjection) {
        runtimeProjectionRevision.set(nextMacroRuntimeProjectionRevision(
            runtimeProjectionRevision.get()
        ));
    }
}

}  // namespace core::state::macro

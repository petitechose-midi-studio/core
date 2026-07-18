#pragma once

#include <cstdint>

#include "state/macro/MacroAutomationDomain.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"

namespace core::ui {

enum class MacroEditorPreviewFocus : uint8_t {
    DESTINATION = 0,
    AUTOMATION,
    FOCUSED_MODULATOR,
    ALL_MODULATION,
};

/** One high-precision presentation sample derived from musical authority. */
struct MacroEditorPreviewSample {
    uint16_t automationQ16 = 0U;
    uint16_t baseQ16 = 0U;
    int16_t modulationQ15 = 0;
    uint16_t outQ16 = 0U;
    bool clippedLow = false;
    bool clippedHigh = false;
    bool discontinuityBefore = false;
};

struct MacroEditorLiveValue {
    float base = 0.0f;
    float modulation = 0.0f;
    float out = 0.0f;
    uint32_t timestampMs = 0U;
    bool valid = false;
    bool clippedLow = false;
    bool clippedHigh = false;
};

/**
 * Small semantic context for the Macro editor.
 *
 * The retained CurvePreviewWidget owns the only screen-width PSRAM work
 * surface. This model references durable musical authority and evaluates the
 * exact pixel positions requested by that surface; it deliberately retains no
 * second 64/320-point cache and is safe to copy on native test paths.
 */
struct MacroEditorPreviewModel {
    enum class Backend : uint8_t { NONE = 0, LEGACY_SLOT, PROJECT_CONTROL };

    const core::state::macro::MacroAutomationSlotState* legacySlot = nullptr;
    const core::state::macro::MacroAutomationPointPool* legacyPool = nullptr;
    const core::state::modulation::ProjectControlState* control = nullptr;
    core::state::macro::MacroAutomationSlotAddress address{};
    core::state::modulation::ModulationBindingId focusedBindingId{};
    uint16_t focusedBindingIndex = UINT16_MAX;
    uint16_t focusedSourceIndex = UINT16_MAX;
    uint16_t focusedRuntimeSourceIndex = UINT16_MAX;
    MacroEditorLiveValue live{};
    float staticBase = 0.0f;
    uint16_t automationDurationTicks =
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
    uint16_t modulationDurationTicks =
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
    uint16_t timelineDurationTicks =
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
    Backend backend = Backend::NONE;
    bool automationStored = false;
    bool modulationStored = false;
    bool automationPlayback = false;
    bool modulationPlayback = false;
    bool automationDrivingBase = false;
    bool manualOverride = false;
};

static_assert(
    sizeof(MacroEditorPreviewModel) <= 96U,
    "Macro preview context must remain a small PSRAM presentation descriptor"
);

MacroEditorPreviewModel buildMacroEditorPreviewModel(
    float staticBase,
    const core::state::macro::MacroAutomationSlotState* slot,
    const core::state::macro::MacroAutomationPointPool& pool,
    bool manualOverride
);

void buildMacroEditorPreviewModel(
    float staticBase,
    const core::state::macro::MacroAutomationSlotState* slot,
    const core::state::macro::MacroAutomationPointPool& pool,
    bool manualOverride,
    MacroEditorPreviewModel& model
);

void buildMacroEditorPreviewModel(
    float staticBase,
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    bool manualOverride,
    MacroEditorPreviewModel& model
);

void buildMacroEditorPreviewModel(
    float staticBase,
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    bool manualOverride,
    core::state::modulation::ModulationBindingId focusedBindingId,
    const MacroEditorLiveValue& live,
    MacroEditorPreviewModel& model
);

[[nodiscard]] bool sampleMacroEditorPreview(
    const MacroEditorPreviewModel& model,
    MacroEditorPreviewFocus focus,
    uint16_t positionQ16,
    uint16_t previousPositionQ16,
    bool hasPrevious,
    MacroEditorPreviewSample& out
);

}  // namespace core::ui

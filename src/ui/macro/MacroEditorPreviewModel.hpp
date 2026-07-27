#pragma once

#include <cstdint>

#include "state/macro/MacroAutomationDomain.hpp"
#include "state/macro/MacroAutomationTake.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"

namespace core::state::modulation {
struct ProjectRecordedShapeCaptureState;
}

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
    enum class Backend : uint8_t { NONE = 0, PROJECT_CONTROL };

    const core::state::modulation::ProjectControlState* control = nullptr;
    const core::state::macro::MacroAutomationTakeState* activeTake = nullptr;
    const core::state::modulation::ProjectRecordedShapeCaptureState*
        recordedShapeCapture = nullptr;
    core::state::macro::MacroAutomationSlotAddress address{};
    core::state::modulation::ModulationBindingId focusedBindingId{};
    core::state::modulation::ProjectCurveId automationCurveId{};
    core::state::modulation::ModulationDestination destination{};
    uint32_t authoredRevision = 0U;
    uint32_t planCompiledRevision = 0U;
    uint32_t planContextHash = 0U;
    uint16_t focusedBindingIndex = UINT16_MAX;
    uint16_t focusedSourceIndex = UINT16_MAX;
    uint16_t focusedRuntimeBindingIndex = UINT16_MAX;
    uint16_t runtimeDestinationIndex = UINT16_MAX;
    uint16_t automationCurveRecordIndex = UINT16_MAX;
    uint16_t destinationScaleQ15 =
        core::state::modulation::
            PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    float staticBase = 0.0f;
    uint16_t automationDurationTicks =
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
    uint16_t modulationDurationTicks =
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
    uint16_t timelineDurationTicks =
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
    float timelineTempoBpm = 120.0f;
    Backend backend = Backend::NONE;
    bool automationStored = false;
    bool modulationStored = false;
    bool automationPlayback = false;
    bool modulationPlayback = false;
    bool automationDrivingBase = false;
    bool manualOverride = false;
    bool timelineHasActiveSource = false;
    uint8_t activeTakeMacro = UINT8_MAX;
};

static_assert(
    sizeof(MacroEditorPreviewModel) <= 96U,
    "Macro preview context must remain a small PSRAM presentation descriptor"
);

void buildMacroEditorPreviewModel(
    float staticBase,
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    bool manualOverride,
    MacroEditorPreviewModel& model,
    float timelineTempoBpm = 120.0f
);

void buildMacroEditorPreviewModel(
    float staticBase,
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    bool manualOverride,
    core::state::modulation::ModulationBindingId focusedBindingId,
    MacroEditorPreviewModel& model,
    float timelineTempoBpm = 120.0f
);

/** Adds the in-progress circular Automation authority without copying it. */
void attachMacroAutomationTakePreview(
    const core::state::macro::MacroAutomationTakeState& take,
    uint8_t macro,
    MacroEditorPreviewModel& model
);

/**
 * Adds one live Project Recorded Shape gesture without copying its PSRAM grid.
 *
 * CREATE_ASSIGNED is projected as a provisional edge for this Macro;
 * REPLACE_EXISTING substitutes the captured source wherever it already feeds
 * this Macro. Detached Project captures are intentionally not attached.
 */
void attachProjectRecordedShapeCapturePreview(
    const core::state::modulation::ProjectRecordedShapeCaptureState& capture,
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

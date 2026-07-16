#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "state/macro/MacroAutomationDomain.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"

namespace core::ui {

// Four samples per 20 px segment keep the 304 px graph visually faithful
// without allocating raw curve data in the widget.
constexpr size_t MACRO_EDITOR_PREVIEW_SAMPLE_COUNT = 64;
constexpr size_t MACRO_EDITOR_MODULATION_MARKER_COUNT = 16;

/** Fixed-size semantic preview for the Macro editor.
 *
 * `automation` always describes stored absolute content. `base` describes the
 * absolute value that is currently allowed to drive playback (manual/static or
 * Automation). `out` is the audible clamp(Base + Modulation).
 */
struct MacroEditorPreviewModel {
    std::array<uint8_t, MACRO_EDITOR_PREVIEW_SAMPLE_COUNT> automation{};
    std::array<uint8_t, MACRO_EDITOR_PREVIEW_SAMPLE_COUNT> base{};
    std::array<int16_t, MACRO_EDITOR_PREVIEW_SAMPLE_COUNT> modulation{};
    std::array<uint8_t, MACRO_EDITOR_PREVIEW_SAMPLE_COUNT> out{};
    bool automationStored = false;
    bool modulationStored = false;
    bool automationPlayback = false;
    bool modulationPlayback = false;
    bool automationDrivingBase = false;
    bool manualOverride = false;
    bool clippedLow = false;
    bool clippedHigh = false;
    uint16_t timelineDurationTicks =
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
};

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

}  // namespace core::ui

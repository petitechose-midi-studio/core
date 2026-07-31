#pragma once

#include <cstddef>
#include <cstdint>

#include "context/standalone/MacroOverlayPresenterFormatters.hpp"
#include "state/macro/MacroSourceDetailPolicy.hpp"

namespace core::context::standalone::macro_overlay_presenter::internal {

const char* recordedShapeCaptureLabel(
    const core::state::modulation::ProjectRecordedShapeCaptureState& capture
);
int bindingDepthPercent(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulationBindingState& binding
);
void formatBeatDuration(
    char* out,
    std::size_t outSize,
    uint16_t durationTicks,
    const char* suffix
);
void formatModulationAssignmentSummary(
    char* out,
    std::size_t outSize,
    const char* name,
    int depth,
    uint16_t position,
    uint16_t count
);
uint32_t mixRevision(uint32_t seed, uint32_t value);
uint16_t sourceUsageCount(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId
);
bool sourceAssignedTo(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulationDestination& destination
);
const char* lfoRateCompact(uint8_t index);
void provideModulatorPickerRow(
    void* context,
    int index,
    ms::ui::KeyValueRowBuffer& out
);
void provideModulationAssignmentRow(
    void* context,
    int rowIndex,
    ms::ui::KeyValueRowBuffer& out
);
bool sampleMacroModulationSparkline(
    const ms::ui::KeyValueSparkline& descriptor,
    uint16_t positionQ16,
    uint16_t previousPositionQ16,
    bool hasPrevious,
    ms::ui::KeyValueSparklineSample& out
);
uint32_t macroModulationGeometryRevision(
    const core::ui::MacroEditorPreviewModel& preview
);
ms::ui::KeyValueSparkline buildModulationSparkline(
    const core::ui::MacroEditorPreviewModel& preview
);
void formatAutomationState(
    char* out,
    std::size_t outSize,
    const core::state::modulation::ProjectControlMacroDestinationView* slot,
    bool manual
);
void formatModulationState(
    char* out,
    std::size_t outSize,
    const core::state::modulation::ProjectControlMacroDestinationView* slot
);
core::state::macro::MacroSourceDetailContext sourceDetailContext(
    const core::state::modulation::ProjectControlMacroDestinationView* slot,
    bool manual
);
const char* modulationOriginLabel(
    core::state::modulation::ProjectCurveOrigin origin
);
const char* conversionPolicyLabel(
    core::state::modulation::ProjectAutomationConversionPolicy policy
);
const char* winnerClassLabel(
    core::state::shared::MidiCcCandidateClass candidateClass
);
bool formatConflict(
    char* out,
    std::size_t outSize,
    const Source& source,
    uint8_t macroIndex,
    bool computed,
    bool manual
);
core::state::macro::MacroAutomationSlotAddress currentAddress(
    const Source& source
);
core::ui::ContextActionStripSlotProps scopeLabel(const char* label);
void projectGuardedAction(
    core::ui::ContextActionStripSlotProps& slot,
    const Source& source,
    core::state::MacroContextButton button
);

}  // namespace core::context::standalone::macro_overlay_presenter::internal

#pragma once

#include <cstdint>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>

#include "state/modulation/ProjectControlState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

namespace core::ui::project::modulators {

using SourceDetailItem = core::state::project::modulators::SourceDetailItem;
using SourceDetailLayout = core::state::project::modulators::SourceDetailLayout;

inline SourceDetailLayout sourceDetailLayout(
    core::state::modulation::ModulatorKind kind
) {
    return core::state::project::modulators::sourceDetailLayout(kind);
}

inline SourceDetailLayout sourceOptionsLayout(
    core::state::modulation::ModulatorKind kind
) {
    return core::state::project::modulators::sourceOptionsLayout(kind);
}

[[nodiscard]] const core::state::modulation::ModulatorSourceState*
sourceAtRegistryIndex(
    const core::state::modulation::ProjectControlState& control,
    uint16_t index
);

inline uint16_t sourceDestinationCount(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId
) {
    return core::state::project::modulators::sourceDestinationCount(
        graph,
        sourceId
    );
}

void populateRegistryRow(
    const core::state::modulation::ProjectControlState& control,
    int index,
    ms::ui::KeyValueRowBuffer& out
);

void populateSourceDetailRow(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulatorSourceState& source,
    int index,
    ms::ui::KeyValueRowBuffer& out
);

void populateSourceOptionsRow(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulatorSourceState& source,
    int index,
    ms::ui::KeyValueRowBuffer& out
);

void populateReachRow(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ModulatorId sourceId,
    int index,
    ms::ui::KeyValueRowBuffer& out
);

void populateDestinationRow(
    const core::state::macro::MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    int index,
    ms::ui::KeyValueRowBuffer& out
);

void populateDestinationPickerRow(
    const core::state::macro::MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    uint8_t track,
    uint8_t page,
    bool creatingSource,
    int index,
    ms::ui::KeyValueRowBuffer& out
);

[[nodiscard]] uint32_t registryRevision(
    const core::state::modulation::ProjectControlState& control,
    uint8_t telemetryRevision,
    uint8_t focusedRow
);

}  // namespace core::ui::project::modulators

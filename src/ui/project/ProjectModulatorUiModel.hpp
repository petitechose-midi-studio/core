#pragma once

#include <cstdint>

#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>

#include "state/modulation/ProjectControlState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

namespace core::ui::project::modulators {

using core::state::project::modulators::SourceDetailItem;
using core::state::project::modulators::SourceDetailLayout;
using core::state::project::modulators::sourceAuditionLayout;
using core::state::project::modulators::sourceAuditionOptionsLayout;
using core::state::project::modulators::sourceDetailLayout;
using core::state::project::modulators::sourceOptionsLayout;

[[nodiscard]] const core::state::modulation::ModulatorSourceState*
sourceAtRegistryIndex(
    const core::state::modulation::ProjectControlState& control,
    uint16_t index
);

using core::state::project::modulators::sourceDestinationCount;

void populateRegistryRow(
    const core::state::modulation::ProjectControlState& control,
    int index,
    ms::ui::KeyValueRowBuffer& out
);

void populateSourceKindRow(int index, ms::ui::KeyValueRowBuffer& out);

void populateTriggerRow(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ModulatorId sourceId,
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

void populateDestinationRow(
    const core::state::macro::MacroPagesState& pages,
    core::state::modulation::ModulatorId sourceId,
    int index,
    ms::ui::KeyValueRowBuffer& out
);

void populateDestinationPickerRow(
    const core::state::macro::MacroPagesState& pages,
    const core::state::project::ProjectNavigationState& navigation,
    core::state::modulation::ModulatorId sourceId,
    int index,
    ms::ui::KeyValueRowBuffer& out
);

[[nodiscard]] uint32_t registryRevision(
    const core::state::modulation::ProjectControlState& control,
    uint8_t focusedRow
);

}  // namespace core::ui::project::modulators

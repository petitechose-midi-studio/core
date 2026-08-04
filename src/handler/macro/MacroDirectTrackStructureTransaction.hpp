#pragma once

#include <cstdint>

#include "handler/sequencer/SequencerPreparedTrackStructureTransaction.hpp"

namespace core::state {
struct CoreState;
struct MacroAutomationClipboard;

namespace macro {
struct MacroTrackData;
}
}  // namespace core::state

namespace core::handler {

/** Cold product adapter for direct Macro Track structure actions. */
[[nodiscard]] SequencerPreparedTrackStructureResult
executeMacroDeleteTrackStructure(core::state::CoreState& state);

[[nodiscard]] SequencerPreparedTrackStructureResult
executeMacroResetTrackStructure(
    core::state::CoreState& state,
    uint8_t targetTrack
);

[[nodiscard]] SequencerPreparedTrackStructureResult
executeMacroPasteTrackStructure(
    core::state::CoreState& state,
    uint8_t targetTrack,
    const core::state::macro::MacroTrackData& track,
    const core::state::MacroAutomationClipboard* automation
);

[[nodiscard]] SequencerPreparedTrackStructureResult
executeMacroCreateTrackStructure(
    core::state::CoreState& state,
    uint8_t targetTrack
);

}  // namespace core::handler

#pragma once

#include "handler/sequencer/SequencerPreparedTrackStructureTransaction.hpp"

namespace core::handler::prepared_track_structure_detail {

[[nodiscard]] bool actionIsValid(
    SequencerPreparedTrackStructureAction action
) noexcept;
[[nodiscard]] bool isMacroAction(
    SequencerPreparedTrackStructureAction action
) noexcept;
[[nodiscard]] bool allowsTransientNoChange(
    SequencerPreparedTrackStructureAction action
) noexcept;
[[nodiscard]] uint16_t trackBit(uint8_t track) noexcept;
[[nodiscard]] uint8_t trackCount(uint16_t mask) noexcept;
[[nodiscard]] bool samePlan(
    const SequencerPreparedTrackStructurePlan& lhs,
    const SequencerPreparedTrackStructurePlan& rhs
) noexcept;
[[nodiscard]] bool validActionPlan(
    const SequencerPreparedTrackStructurePlan& plan,
    SequencerPreparedTrackStructureAction requestedAction,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::sequencer::SequencerState& sequencer,
    const SharedTrackDomainServices& sharedTracks,
    const core::state::macro::MacroPagesState* macroPages
) noexcept;
[[nodiscard]] bool validOperations(
    const SequencerPreparedTrackStructureExecution::Operations* operations,
    SequencerPreparedTrackStructureAction action
) noexcept;

}  // namespace core::handler::prepared_track_structure_detail

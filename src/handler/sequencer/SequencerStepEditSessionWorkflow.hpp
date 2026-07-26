#pragma once

#include <cstdint>

#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/common/ButtonReleaseLatch.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler::sequencer::step_edit_session_workflow {

using StepEditHistorySnapshot = core::state::sequencer::SequencerHistoryPatternSnapshot;

bool openForMacroInPage(core::state::sequencer::SequencerState& sequencer,
                        SequencerHistoryDomainServices& history,
                        ButtonReleaseLatch<8>& openReleaseLatch,
                        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                        StepEditHistorySnapshot& historySnapshot,
                        bool& historySnapshotValid,
                        uint8_t indexInPage);
bool commitHistory(core::state::sequencer::SequencerState& sequencer,
                   SequencerHistoryDomainServices& history,
                   StepEditHistorySnapshot& historySnapshot,
                   bool& historySnapshotValid);
bool retargetRootStep(core::state::sequencer::SequencerState& sequencer,
                      SequencerHistoryDomainServices& history,
                      StepEditHistorySnapshot& historySnapshot,
                      bool& historySnapshotValid,
                      int direction);
bool backToParentContent(core::state::sequencer::SequencerState& sequencer,
                         SequencerHistoryDomainServices& history,
                         StepEditHistorySnapshot& historySnapshot,
                         bool& historySnapshotValid);
void close(core::state::sequencer::SequencerState& sequencer,
           SequencerHistoryDomainServices& history,
           ButtonReleaseLatch<8>& openReleaseLatch,
           ButtonReleaseLatch<2>& contextReleaseLatch,
           oc::context::OverlayManager<core::ui::OverlayType>& overlays,
           StepEditHistorySnapshot& historySnapshot,
           bool& historySnapshotValid);
bool editedStepInRange(const core::state::sequencer::SequencerState& sequencer,
                       uint8_t& step);
bool shouldCloseFromMacro(ButtonReleaseLatch<8>& openReleaseLatch,
                          const core::state::sequencer::SequencerState& sequencer,
                          uint8_t indexInPage);

}  // namespace core::handler::sequencer::step_edit_session_workflow

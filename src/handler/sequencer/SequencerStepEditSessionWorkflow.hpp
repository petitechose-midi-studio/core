#pragma once

#include <cstdint>

#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/common/ButtonReleaseLatch.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler::sequencer::step_edit_session_workflow {

bool openForMacroInPage(core::state::sequencer::SequencerState& sequencer,
                        SequencerHistoryDomainServices& history,
                        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
                        uint8_t indexInPage);
bool openForStep(core::state::sequencer::SequencerState& sequencer,
                 SequencerHistoryDomainServices& history,
                 oc::context::OverlayManager<core::ui::OverlayType>& overlays, uint8_t step);
bool commitHistory(SequencerHistoryDomainServices& history);
bool retargetRootStep(core::state::sequencer::SequencerState& sequencer,
                      SequencerHistoryDomainServices& history, int direction);
bool backToParentContent(core::state::sequencer::SequencerState& sequencer,
                         SequencerHistoryDomainServices& history);
bool close(core::state::sequencer::SequencerState& sequencer,
           SequencerHistoryDomainServices& history, ButtonReleaseLatch<2>& contextReleaseLatch,
           oc::context::OverlayManager<core::ui::OverlayType>& overlays);
bool editedStepInRange(const core::state::sequencer::SequencerState& sequencer, uint8_t& step);
bool shouldCloseFromMacro(const core::state::sequencer::SequencerState& sequencer,
                          uint8_t indexInPage);

}  // namespace core::handler::sequencer::step_edit_session_workflow

#pragma once

#include <oc/note/sequencer/StepSequencerChord.hpp>

namespace core::state::sequencer {

/**
 * One serialized cold-path DP workspace shared by context changes and Chord
 * Preset inspection. Neither path may run concurrently on the controller.
 */
oc::note::sequencer::StepSequencerChordProjectionWorkspace&
sharedSequencerChordProjectionWorkspace();

}  // namespace core::state::sequencer

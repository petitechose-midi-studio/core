#include "state/sequencer/SequencerChordProjectionWorkspace.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {
namespace {

EXTMEM oc::note::sequencer::StepSequencerChordProjectionWorkspace
    workspace{};

}  // namespace

FLASHMEM oc::note::sequencer::StepSequencerChordProjectionWorkspace&
sharedSequencerChordProjectionWorkspace() {
    return workspace;
}

}  // namespace core::state::sequencer

#pragma once

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerStepContentDraftSession.hpp"

namespace core::ui::sequencer {

FLASHMEM const char* standaloneStepContentDraftTransitionLabel(
    core::state::sequencer::SequencerStepContentDraftBlockedTransition transition
);

FLASHMEM const char* propertyOverlayStepContentDraftTransitionLabel(
    core::state::sequencer::SequencerStepContentDraftBlockedTransition transition
);

}  // namespace core::ui::sequencer

#pragma once

#include "ui/sequencer/SequencerTrackPastePreflightViewModel.hpp"

namespace core::ui::sequencer {

struct SequencerViewModelSource;

/** Builds the canonical live Track transfer plan/action for the focused target. */
SequencerTrackPasteProjection projectSequencerTrackPaste(
    const SequencerViewModelSource& source,
    bool selectionActive
);

/** Adapts the real Sequencer state to the LVGL-free preflight formatter. */
SequencerTrackPastePreflightViewModel projectSequencerTrackPastePreflight(
    const SequencerViewModelSource& source
);

}  // namespace core::ui::sequencer

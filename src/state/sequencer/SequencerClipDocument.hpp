#pragma once

#include "DrumPatternState.hpp"
#include "SequencerClipState.hpp"
#include "SequencerPatternState.hpp"

namespace core::state::sequencer {

enum class SequencerTrackKind : uint8_t { INSTRUMENT = 0, DRUM };

/** One musical owner, independent of the selected or playing Clip.
 * Moving its unique owner never moves its data or invalidates payload addresses.
 */
struct SequencerClipDocument {
    SequencerPatternState pattern;
    SequencerClipState clip{};
    SequencerTrackKind trackKind = SequencerTrackKind::INSTRUMENT;
    core::app::ExtmemUniquePtr<DrumTrackState> drum;
};

using SequencerClipDocumentPtr = core::app::ExtmemUniquePtr<SequencerClipDocument>;

} // namespace core::state::sequencer

#pragma once

#include <cstdint>

#include "state/sequencer/SequencerChordContextProjection.hpp"
#include "state/sequencer/SequencerUiState.hpp"

namespace core::handler {

/**
 * Reports only lossy or musically adapted context projections.
 *
 * Exact formula re-encodings remain silent. The existing Sequencer history
 * toast owns the presentation so the feedback neither adds an overlay nor
 * captures Transport bindings.
 */
bool showChordProjectionFeedback(
    core::state::sequencer::SequencerHistoryFeedbackState& feedback,
    const core::state::sequencer::SequencerChordContextProjectionStats& stats,
    uint32_t nowMs
);

}  // namespace core::handler

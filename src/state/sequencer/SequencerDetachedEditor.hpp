#pragma once

#include "SequencerState.hpp"

namespace core::state::sequencer {

namespace detail {
struct DetachedPatternStorage {
    SequencerPatternState ownedPattern;
    SequencerClipState ownedClip;
};
}

// A temporary command candidate owns its musical data independently of the
// live bank. Storage is constructed first and outlives the borrowing editor.
struct SequencerDetachedEditor final : private detail::DetachedPatternStorage,
                                       public SequencerState {
    SequencerDetachedEditor() : SequencerState(ownedPattern, ownedClip) {}
};

} // namespace core::state::sequencer

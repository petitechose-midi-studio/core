#pragma once

#include <oc/state/Signal.hpp>
#include <config/PlatformCompat.hpp>
#include <cstdlib>

#include "SequencerPatternState.hpp"

namespace core::state::sequencer {

// Stable, filtered invalidations for retained UI. No musical value or payload
// is mirrored here. Detach with bind() before destroying the observed Pattern.
class SequencerPatternObservation {
public:
    // The largest retained-UI fan-out is length: five concurrent watchers.
    using Revision = oc::state::Signal<uint32_t, 5>;
    Revision length, enabledMask, stepDataRevision, graphRevision;
    Revision ccLaneRevision, patternVariationRevision, patternScaleRevision;
    Revision patternTimingRevision;

    oc::state::Signal<uint32_t, 1> authoredRevision;

    ~SequencerPatternObservation();
    void bind(SequencerPatternState& pattern);

private:
    void publish(SequencerPatternChange change);
    SequencerPatternState* pattern_ = nullptr;
    friend struct SequencerPatternState;
};

} // namespace core::state::sequencer

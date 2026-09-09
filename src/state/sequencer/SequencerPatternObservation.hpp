#pragma once

#include <oc/state/FixedSubscriptionList.hpp>
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

    void bind(SequencerPatternState& pattern);

private:
    template <typename Source>
    OC_ALWAYS_INLINE void watch(Source& source, Revision& revision) {
        if (!subscriptions_.tryAdd(source.subscribe([&revision](const auto&) {
                revision.set(revision.get() + 1U);
            }))) {
            // Partial observation would leave retained controls stale.
            std::abort();
        }
    }

    oc::state::FixedSubscriptionList<11> subscriptions_;
};

} // namespace core::state::sequencer

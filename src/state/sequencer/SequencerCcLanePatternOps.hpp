#pragma once

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerCcLaneDomain.hpp"

namespace core::state::sequencer {

struct SequencerPatternState;

using SequencerCcLaneBankPtr = core::app::ExtmemUniquePtr<SequencerCcLaneBank>;

const SequencerCcLaneBank* sequencerCcLaneView(
    const SequencerPatternState& pattern
);

/** Lazily materializes a canonical empty bank. Never publishes on OOM. */
SequencerCcLaneBank* ensureSequencerCcLaneBank(SequencerPatternState& pattern);

/** Clone a canonical bank into a detached EXTMEM owner, atomically on success. */
bool cloneSequencerCcLaneBank(
    SequencerCcLaneBankPtr& out,
    const SequencerCcLaneBank* source
);

/** Copy into already-reserved storage; empty source releases the destination. */
bool captureSequencerCcLaneBankUsingReservedStorage(
    const SequencerCcLaneBank* source,
    SequencerCcLaneBankPtr& destination
);

/** Installs detached ownership and synchronizes the Pattern revision signal. */
void installSequencerCcLaneBank(
    SequencerPatternState& pattern,
    SequencerCcLaneBankPtr bank
);

bool copySequencerCcLaneBank(
    SequencerPatternState& target,
    const SequencerPatternState& source
);

bool sameOptionalSequencerCcLaneBank(
    const SequencerCcLaneBank* lhs,
    const SequencerCcLaneBank* rhs
);

}  // namespace core::state::sequencer

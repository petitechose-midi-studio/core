#include "state/sequencer/SequencerCcLanePatternOps.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerPatternState.hpp"

namespace core::state::sequencer {

FLASHMEM const SequencerCcLaneBank* sequencerCcLaneView(
    const SequencerPatternState& pattern
) {
    return pattern.ccLanes.get();
}

FLASHMEM SequencerCcLaneBank* ensureSequencerCcLaneBank(
    SequencerPatternState& pattern
) {
    if (pattern.ccLanes) return pattern.ccLanes.get();
    auto bank = core::app::makeExtmemUnique<SequencerCcLaneBank>();
    if (!bank) return nullptr;
    pattern.ccLanes = std::move(bank);
    pattern.bumpCcLaneRevision();
    return pattern.ccLanes.get();
}

FLASHMEM bool cloneSequencerCcLaneBank(
    SequencerCcLaneBankPtr& out,
    const SequencerCcLaneBank* source
) {
    if (source == nullptr || sequencerCcLaneCount(*source) == 0) {
        out.reset();
        return true;
    }
    SequencerCcLaneBank canonical{};
    if (!decodeCanonicalSequencerCcLaneBank(*source, canonical)) return false;
    auto copy = core::app::makeExtmemUnique<SequencerCcLaneBank>(canonical);
    if (!copy) return false;
    out = std::move(copy);
    return true;
}

FLASHMEM bool captureSequencerCcLaneBankUsingReservedStorage(
    const SequencerCcLaneBank* source,
    SequencerCcLaneBankPtr& destination
) {
    if (source == nullptr || sequencerCcLaneCount(*source) == 0) {
        destination.reset();
        return true;
    }
    SequencerCcLaneBank canonical{};
    if (!decodeCanonicalSequencerCcLaneBank(*source, canonical)) return false;
    if (!destination) {
        destination = core::app::makeExtmemUnique<SequencerCcLaneBank>();
        if (!destination) return false;
    }
    *destination = canonical;
    return true;
}

FLASHMEM void installSequencerCcLaneBank(
    SequencerPatternState& pattern,
    SequencerCcLaneBankPtr bank
) {
    if (bank && sequencerCcLaneCount(*bank) == 0) bank.reset();
    pattern.ccLanes = std::move(bank);
    pattern.bumpCcLaneRevision();
}

FLASHMEM bool copySequencerCcLaneBank(
    SequencerPatternState& target,
    const SequencerPatternState& source
) {
    SequencerCcLaneBankPtr copy;
    if (!cloneSequencerCcLaneBank(copy, source.ccLanes.get())) return false;
    installSequencerCcLaneBank(target, std::move(copy));
    return true;
}

FLASHMEM bool sameOptionalSequencerCcLaneBank(
    const SequencerCcLaneBank* lhs,
    const SequencerCcLaneBank* rhs
) {
    const bool lhsEmpty = lhs == nullptr || sequencerCcLaneCount(*lhs) == 0;
    const bool rhsEmpty = rhs == nullptr || sequencerCcLaneCount(*rhs) == 0;
    if (lhsEmpty || rhsEmpty) return lhsEmpty == rhsEmpty;
    return sameSequencerCcLaneBankMusicalData(*lhs, *rhs);
}

}  // namespace core::state::sequencer

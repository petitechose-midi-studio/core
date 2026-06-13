#include "state/sequencer/SequencerStructureHistory.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state::sequencer {

namespace {

constexpr uint16_t kTrackMaskAll =
    static_cast<uint16_t>((1U << SequencerTrackBankState::TRACK_COUNT) - 1U);

FLASHMEM uint16_t activeTrackBit(const SequencerTrackBankState& bank) {
    return sequencerHistoryTrackBit(bank.activeTrackIndex());
}

}  // namespace

FLASHMEM SequencerHistoryTrackStructureSnapshot::SequencerHistoryTrackStructureSnapshot() = default;
FLASHMEM SequencerHistoryTrackStructureSnapshot::~SequencerHistoryTrackStructureSnapshot() = default;
FLASHMEM SequencerHistoryTrackStructureSnapshot::SequencerHistoryTrackStructureSnapshot(
    SequencerHistoryTrackStructureSnapshot&&
) noexcept = default;
FLASHMEM SequencerHistoryTrackStructureSnapshot& SequencerHistoryTrackStructureSnapshot::operator=(
    SequencerHistoryTrackStructureSnapshot&&
) noexcept = default;

FLASHMEM SequencerHistoryTrackStructureChange::SequencerHistoryTrackStructureChange() = default;
FLASHMEM SequencerHistoryTrackStructureChange::~SequencerHistoryTrackStructureChange() = default;
FLASHMEM SequencerHistoryTrackStructureChange::SequencerHistoryTrackStructureChange(
    SequencerHistoryTrackStructureChange&&
) noexcept = default;
FLASHMEM SequencerHistoryTrackStructureChange& SequencerHistoryTrackStructureChange::operator=(
    SequencerHistoryTrackStructureChange&&
) noexcept = default;

FLASHMEM uint16_t sequencerHistoryTrackBit(uint8_t trackIndex) {
    const uint8_t clamped = SequencerTrackBankState::clampTrackIndex(trackIndex);
    return static_cast<uint16_t>(1U << clamped);
}

FLASHMEM uint16_t sequencerHistorySanitizeTrackMask(uint16_t trackMask) {
    const uint16_t sanitized = static_cast<uint16_t>(trackMask & kTrackMaskAll);
    return sanitized == 0 ? 0x0001 : sanitized;
}

FLASHMEM uint8_t sequencerHistoryEnabledTrackCount(uint16_t enabledMask) {
    uint8_t count = 0;
    const uint16_t sanitized = static_cast<uint16_t>(enabledMask & kTrackMaskAll);
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((sanitized & sequencerHistoryTrackBit(i)) != 0) {
            ++count;
        }
    }
    return count == 0 ? 1 : count;
}

FLASHMEM bool captureHistoryStructureSnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out
) {
    out = SequencerHistoryTrackStructureSnapshot{};
    out.enabledMask = bank.currentEnabledMask();
    out.activeTrack = bank.activeTrackIndex();
    out.focusedStep = active.focusedStep.get();
    out.page = active.page.get();
    out.capturedTrackMask = sequencerHistorySanitizeTrackMask(
        static_cast<uint16_t>(trackMask | activeTrackBit(bank))
    );

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((out.capturedTrackMask & sequencerHistoryTrackBit(i)) == 0) {
            continue;
        }

        if (!captureHistorySnapshot(bank, active, i, out.tracks[i])) {
            return false;
        }
    }

    return true;
}

FLASHMEM bool applyHistoryStructureSnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerHistoryTrackStructureSnapshot& snapshot
) {
    const uint16_t capturedMask = sequencerHistorySanitizeTrackMask(
        static_cast<uint16_t>(snapshot.capturedTrackMask | sequencerHistoryTrackBit(snapshot.activeTrack))
    );

    storeActiveTrack(bank, active);
    bank.syncSharedTrackState(snapshot.enabledMask, snapshot.activeTrack);

    bool restoredActiveTrack = false;
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((capturedMask & sequencerHistoryTrackBit(i)) == 0) {
            continue;
        }

        if (!applyHistorySnapshotToTrack(bank, active, i, snapshot.tracks[i])) {
            return false;
        }

        restoredActiveTrack = restoredActiveTrack || i == bank.activeTrackIndex();
    }

    if (!restoredActiveTrack) {
        return false;
    }

    active.focusedStep.set(snapshot.focusedStep);
    active.page.set(snapshot.page);
    return true;
}

FLASHMEM bool sameMusicalHistoryStructureSnapshot(
    const SequencerHistoryTrackStructureSnapshot& lhs,
    const SequencerHistoryTrackStructureSnapshot& rhs
) {
    if (lhs.enabledMask != rhs.enabledMask || lhs.activeTrack != rhs.activeTrack) {
        return false;
    }

    const uint16_t capturedMask = sequencerHistorySanitizeTrackMask(
        static_cast<uint16_t>(lhs.capturedTrackMask | rhs.capturedTrackMask)
    );

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((capturedMask & sequencerHistoryTrackBit(i)) == 0) {
            continue;
        }

        if (!sameMusicalHistorySnapshot(lhs.tracks[i], rhs.tracks[i])) {
            return false;
        }
    }

    return true;
}

}  // namespace core::state::sequencer

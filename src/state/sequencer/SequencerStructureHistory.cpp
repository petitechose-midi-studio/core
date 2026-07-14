#include "state/sequencer/SequencerStructureHistory.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state::sequencer {

namespace {

constexpr uint16_t kTrackMaskAll =
    static_cast<uint16_t>((1U << SequencerTrackBankState::TRACK_COUNT) - 1U);

FLASHMEM uint16_t activeTrackBit(const SequencerTrackBankState& bank) {
    return sequencerHistoryTrackBit(bank.activeTrackIndex());
}

FLASHMEM bool captureStructureSnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out,
    bool reuseGraphStorage
) {
    if (!reuseGraphStorage) {
        out = SequencerHistoryTrackStructureSnapshot{};
    }
    out.enabledMask = bank.currentEnabledMask();
    out.mutedMask = bank.currentMutedMask();
    out.activeTrack = bank.activeTrackIndex();
    out.focusedStep = active.focusedStep.get();
    out.page = active.page.get();
    out.capturedTrackMask = sequencerHistorySanitizeTrackMask(
        static_cast<uint16_t>(trackMask | activeTrackBit(bank))
    );

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((out.capturedTrackMask & sequencerHistoryTrackBit(i)) == 0) continue;
        const bool captured = reuseGraphStorage
            ? captureHistorySnapshotUsingReservedGraph(bank, active, i, out.tracks[i])
            : captureHistorySnapshot(bank, active, i, out.tracks[i]);
        if (!captured) return false;
    }
    return true;
}

FLASHMEM bool cloneSnapshotGraph(
    const SequencerHistoryPatternSnapshot& snapshot,
    SequencerHistoryGraphPtr& out
) {
    out.reset();
    if (!snapshot.graph || !snapshot.graph->enabled) return true;
    out = core::app::makeExtmemUnique<oc::note::sequencer::StepSequencerGraph>(
        *snapshot.graph
    );
    return static_cast<bool>(out);
}

FLASHMEM void installSnapshotGraph(
    SequencerPatternState& target,
    SequencerHistoryGraphPtr graph,
    uint32_t revision
) {
    target.graph = std::move(graph);
    target.graphRevision.set(revision);
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
    return captureStructureSnapshot(bank, active, trackMask, out, false);
}

FLASHMEM bool captureHistoryStructureSnapshotUsingReservedGraphs(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out
) {
    return captureStructureSnapshot(bank, active, trackMask, out, true);
}

FLASHMEM bool applyHistoryStructureSnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerHistoryTrackStructureSnapshot& snapshot,
    uint16_t preserveDestinationBindingsMask
) {
    const uint16_t capturedMask = sequencerHistorySanitizeTrackMask(
        static_cast<uint16_t>(snapshot.capturedTrackMask | sequencerHistoryTrackBit(snapshot.activeTrack))
    );
    const uint16_t preservedBindingsMask = static_cast<uint16_t>(
        preserveDestinationBindingsMask & capturedMask & kTrackMaskAll
    );
    const uint8_t targetActive = SequencerTrackBankState::clampTrackIndex(
        snapshot.activeTrack
    );
    if ((capturedMask & sequencerHistoryTrackBit(bank.activeTrackIndex())) == 0) {
        return false;
    }

    // The active editor is authoritative for its Track. Its bank slot can be
    // stale until a Track switch, so capture destination bindings from the
    // editor for that one Track and from the bank for every other Track.
    std::array<uint8_t, SequencerTrackBankState::TRACK_COUNT> preservedMidiChannels{};
    const uint8_t currentActive = bank.activeTrackIndex();
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((preservedBindingsMask & sequencerHistoryTrackBit(i)) == 0) continue;
        preservedMidiChannels[i] = i == currentActive
            ? active.pattern.midiChannel.get()
            : bank.track(i).midiChannel.get();
    }

    std::array<SequencerHistoryGraphPtr, SequencerTrackBankState::TRACK_COUNT> bankGraphs{};
    SequencerHistoryGraphPtr editorGraph;
    std::array<SequencerHistoryCcLanePtr, SequencerTrackBankState::TRACK_COUNT>
        bankCcLanes{};
    SequencerHistoryCcLanePtr editorCcLanes;
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((capturedMask & sequencerHistoryTrackBit(i)) == 0) {
            continue;
        }
        if (!cloneSnapshotGraph(snapshot.tracks[i], bankGraphs[i])) return false;
        if (snapshot.tracks[i].ccLanesCaptured &&
            !cloneSequencerCcLaneBank(
                bankCcLanes[i],
                snapshot.tracks[i].ccLanes.get()
            )) {
            return false;
        }
    }
    if (!cloneSnapshotGraph(snapshot.tracks[targetActive], editorGraph)) {
        return false;
    }
    if (snapshot.tracks[targetActive].ccLanesCaptured &&
        !cloneSequencerCcLaneBank(
            editorCcLanes,
            snapshot.tracks[targetActive].ccLanes.get()
        )) {
        return false;
    }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((capturedMask & sequencerHistoryTrackBit(i)) == 0) continue;
        applySnapshot(bank.track(i), snapshot.tracks[i].flat);
        installSnapshotGraph(
            bank.track(i),
            std::move(bankGraphs[i]),
            snapshot.tracks[i].flat.graphRevision
        );
        if (snapshot.tracks[i].ccLanesCaptured) {
            installSequencerCcLaneBank(
                bank.track(i),
                std::move(bankCcLanes[i])
            );
        }
    }

    applySnapshotToEditor(active, snapshot.tracks[targetActive].flat);
    installSnapshotGraph(
        active.pattern,
        std::move(editorGraph),
        snapshot.tracks[targetActive].flat.graphRevision
    );
    if (snapshot.tracks[targetActive].ccLanesCaptured) {
        installSequencerCcLaneBank(active.pattern, std::move(editorCcLanes));
    }
    bank.syncSharedTrackState(snapshot.enabledMask, targetActive);
    bank.setMutedMask(snapshot.mutedMask);
    active.focusedStep.set(snapshot.focusedStep);
    active.page.set(snapshot.page);

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((preservedBindingsMask & sequencerHistoryTrackBit(i)) == 0) continue;
        bank.track(i).midiChannel.set(preservedMidiChannels[i]);
    }
    if ((preservedBindingsMask & sequencerHistoryTrackBit(targetActive)) != 0) {
        active.pattern.midiChannel.set(preservedMidiChannels[targetActive]);
    }
    return true;
}

FLASHMEM bool sameMusicalHistoryStructureSnapshot(
    const SequencerHistoryTrackStructureSnapshot& lhs,
    const SequencerHistoryTrackStructureSnapshot& rhs
) {
    if (lhs.enabledMask != rhs.enabledMask ||
        lhs.mutedMask != rhs.mutedMask ||
        lhs.activeTrack != rhs.activeTrack) {
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

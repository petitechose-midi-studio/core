#include "state/sequencer/SequencerStructureHistory.hpp"

#include <utility>
#include <cstring>

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

FLASHMEM bool reserveHistoryStructureSnapshotStorage(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out
) {
    const uint16_t capturedMask = sequencerHistorySanitizeTrackMask(
        static_cast<uint16_t>(trackMask | activeTrackBit(bank))
    );
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((capturedMask & sequencerHistoryTrackBit(i)) == 0U) {
            out.tracks[i].reset();
            continue;
        }
        if (!reserveHistorySnapshotStorage(bank, active, i, out.tracks[i])) {
            return false;
        }
    }
    out.capturedTrackMask = capturedMask;
    return true;
}

FLASHMEM bool captureHistoryStructureSnapshotUsingReservedStorage(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out
) {
    const uint16_t capturedMask = sequencerHistorySanitizeTrackMask(
        static_cast<uint16_t>(trackMask | activeTrackBit(bank))
    );
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((capturedMask & sequencerHistoryTrackBit(i)) == 0U) {
            out.tracks[i].reset();
            continue;
        }
        if (!captureHistorySnapshotUsingReservedStorage(
                bank,
                active,
                i,
                out.tracks[i]
            )) {
            return false;
        }
    }
    out.enabledMask = bank.currentEnabledMask();
    out.activeTrack = bank.activeTrackIndex();
    out.focusedStep = active.focusedStep.get();
    out.page = active.page.get();
    out.capturedTrackMask = capturedMask;
    return true;
}

FLASHMEM bool captureHistoryStructureSnapshotUsingReservedGraphs(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out
) {
    return captureStructureSnapshot(bank, active, trackMask, out, true);
}

FLASHMEM SequencerHistoryTrackStructureChangePtr prepareHistoryStructureChangeBefore(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryDescriptor descriptor
) {
    auto change = core::app::makeExtmemUnique<
        SequencerHistoryTrackStructureChange
    >();
    if (!change || !captureHistoryStructureSnapshot(
            bank,
            active,
            trackMask,
            change->before
        )) {
        return nullptr;
    }
    change->descriptor = descriptor;
    return change;
}

FLASHMEM bool reservePreparedHistoryStructureAfter(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerHistoryTrackStructureChange& change
) {
    const uint16_t frozenMask = change.before.capturedTrackMask;
    if ((frozenMask & activeTrackBit(bank)) == 0U) return false;
    if (!reserveHistoryStructureSnapshotStorage(
        bank,
        active,
        frozenMask,
        change.after
    )) {
        return false;
    }
    return change.after.capturedTrackMask == frozenMask;
}

FLASHMEM bool capturePreparedHistoryStructureAfterUsingReservedStorage(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerHistoryTrackStructureChange& change
) {
    const uint16_t frozenMask = change.before.capturedTrackMask;
    if (change.after.capturedTrackMask != frozenMask ||
        (frozenMask & activeTrackBit(bank)) == 0U) {
        return false;
    }
    if (!captureHistoryStructureSnapshotUsingReservedStorage(
        bank,
        active,
        frozenMask,
        change.after
    )) {
        return false;
    }
    return change.after.capturedTrackMask == frozenMask;
}

FLASHMEM bool applyHistoryStructureSnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerHistoryTrackStructureSnapshot& snapshot
) {
    const uint16_t capturedMask = sequencerHistorySanitizeTrackMask(
        static_cast<uint16_t>(snapshot.capturedTrackMask | sequencerHistoryTrackBit(snapshot.activeTrack))
    );
    const uint8_t targetActive = SequencerTrackBankState::clampTrackIndex(
        snapshot.activeTrack
    );
    if ((capturedMask & sequencerHistoryTrackBit(bank.activeTrackIndex())) == 0) {
        return false;
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
    active.focusedStep.set(snapshot.focusedStep);
    active.page.set(snapshot.page);

    return true;
}

FLASHMEM bool sameMusicalHistoryStructureSnapshot(
    const SequencerHistoryTrackStructureSnapshot& lhs,
    const SequencerHistoryTrackStructureSnapshot& rhs
) {
    if (lhs.enabledMask != rhs.enabledMask ||
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

FLASHMEM bool captureMacroTrackStructureHistoryBefore(
    const core::state::macro::MacroPagesState& pages,
    uint16_t trackMask,
    SequencerHistoryTrackStructureChange& change
) {
    const uint16_t sanitized = sequencerHistorySanitizeTrackMask(trackMask);
    auto payload = core::app::makeExtmemUnique<
        SequencerHistoryMacroTrackStructurePayload
    >();
    if (!payload) return false;
    payload->beforeControl = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >(pages.control.authored);
    payload->afterControl = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >();
    if (!payload->beforeControl || !payload->afterControl) return false;
    payload->capturedTrackMask = sanitized;
    for (uint8_t track = 0U; track < macro::TRACK_COUNT; ++track) {
        if ((sanitized & sequencerHistoryTrackBit(track)) == 0U) continue;
        payload->beforeTracks[track] = pages.tracks[track];
    }
    change.macroStructure = std::move(payload);
    return true;
}

FLASHMEM bool captureMacroTrackStructureHistoryAfter(
    const core::state::macro::MacroPagesState& pages,
    SequencerHistoryTrackStructureChange& change
) {
    auto* payload = change.macroStructure.get();
    if (payload == nullptr || payload->capturedTrackMask == 0U ||
        !payload->beforeControl || !payload->afterControl) {
        return false;
    }
    for (uint8_t track = 0U; track < macro::TRACK_COUNT; ++track) {
        if ((payload->capturedTrackMask & sequencerHistoryTrackBit(track)) == 0U) {
            continue;
        }
        payload->afterTracks[track] = pages.tracks[track];
    }
    *payload->afterControl = pages.control.authored;
    if (std::memcmp(
            payload->beforeControl.get(),
            payload->afterControl.get(),
            sizeof(core::state::modulation::ProjectControlDomainState)
        ) == 0) {
        payload->afterControl.reset();
    }
    payload->afterCaptured = true;
    return true;
}

FLASHMEM bool macroTrackStructureHistoryChanged(
    const SequencerHistoryTrackStructureChange& change
) {
    const auto* payload = change.macroStructure.get();
    if (payload == nullptr || !payload->beforeControl ||
        !payload->afterCaptured) return false;
    if (payload->afterControl != nullptr) return true;
    for (uint8_t track = 0U; track < macro::TRACK_COUNT; ++track) {
        if ((payload->capturedTrackMask & sequencerHistoryTrackBit(track)) == 0U) {
            continue;
        }
        if (std::memcmp(
                &payload->beforeTracks[track],
                &payload->afterTracks[track],
                sizeof(core::state::macro::MacroTrackData)
            ) != 0) {
            return true;
        }
    }
    return false;
}

FLASHMEM bool liveMacroTrackStructureMatches(
    const core::state::macro::MacroPagesState& pages,
    const SequencerHistoryMacroTrackStructurePayload& payload,
    bool after
) {
    if (!payload.afterCaptured) return false;
    const auto* control = after && payload.afterControl
        ? payload.afterControl.get()
        : payload.beforeControl.get();
    if (control == nullptr || std::memcmp(
            &pages.control.authored,
            control,
            sizeof(core::state::modulation::ProjectControlDomainState)
        ) != 0) {
        return false;
    }
    const auto& tracks = after ? payload.afterTracks : payload.beforeTracks;
    for (uint8_t track = 0U; track < macro::TRACK_COUNT; ++track) {
        if ((payload.capturedTrackMask & sequencerHistoryTrackBit(track)) == 0U) {
            continue;
        }
        if (std::memcmp(
                &pages.tracks[track],
                &tracks[track],
                sizeof(core::state::macro::MacroTrackData)
            ) != 0) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool applyMacroTrackStructureHistory(
    core::state::macro::MacroPagesState& pages,
    const SequencerHistoryMacroTrackStructurePayload& payload,
    bool after
) {
    const bool expectedAfter = !after;
    if (!liveMacroTrackStructureMatches(pages, payload, expectedAfter)) {
        return false;
    }
    const auto* control = after && payload.afterControl
        ? payload.afterControl.get()
        : payload.beforeControl.get();
    if (control == nullptr) return false;
    pages.control.authored = *control;
    pages.control.markAuthoredMutation();
    const auto& tracks = after ? payload.afterTracks : payload.beforeTracks;
    for (uint8_t track = 0U; track < macro::TRACK_COUNT; ++track) {
        if ((payload.capturedTrackMask & sequencerHistoryTrackBit(track)) == 0U) {
            continue;
        }
        pages.tracks[track] = tracks[track];
    }
    pages.syncActiveTrackCache();
    pages.updateActiveConfigs();
    return liveMacroTrackStructureMatches(pages, payload, after);
}

}  // namespace core::state::sequencer

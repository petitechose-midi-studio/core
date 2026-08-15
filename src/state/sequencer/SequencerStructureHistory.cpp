#include "state/sequencer/SequencerStructureHistory.hpp"

#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::state::sequencer {

namespace {

[[noreturn]] FLASHMEM void failStructureHistoryInvariant() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_trap();
#else
    for (;;) {}
#endif
}

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
        out.reset();
    }
    out.enabledMask = bank.currentEnabledMask();
    out.activeTrack = bank.activeTrackIndex();
    out.focusedStep = active.focusedStep.get();
    out.page = active.page.get();
    out.capturedTrackMask = sequencerHistorySanitizeTrackMask(
        static_cast<uint16_t>(trackMask | activeTrackBit(bank))
    );
    out.drumTrackMask = static_cast<uint16_t>(
        bank.drumTrackMask() & out.capturedTrackMask
    );

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        const uint16_t bit = sequencerHistoryTrackBit(i);
        if ((out.capturedTrackMask & bit) == 0) {
            out.drumTracks[i].reset();
            continue;
        }
        const bool captured = reuseGraphStorage
            ? captureHistorySnapshotUsingReservedGraph(bank, active, i, out.tracks[i])
            : captureHistorySnapshot(bank, active, i, out.tracks[i]);
        if (!captured) return false;
        if ((out.drumTrackMask & bit) != 0U) {
            if (reuseGraphStorage && out.drumTracks[i]) {
                *out.drumTracks[i] = bank.drumTrack(i);
            } else {
                out.drumTracks[i] = core::app::makeExtmemUnique<DrumTrackState>(
                    bank.drumTrack(i)
                );
                if (!out.drumTracks[i]) return false;
            }
        } else {
            out.drumTracks[i].reset();
        }
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

FLASHMEM bool validStructureSnapshotSource(
    const SequencerHistoryTrackStructureSnapshot& snapshot
) {
    if (snapshot.activeTrack >= SequencerTrackBankState::TRACK_COUNT ||
        snapshot.capturedTrackMask !=
            sequencerHistorySanitizeTrackMask(snapshot.capturedTrackMask) ||
        snapshot.enabledMask !=
            sequencerHistorySanitizeTrackMask(snapshot.enabledMask) ||
        (snapshot.capturedTrackMask &
            sequencerHistoryTrackBit(snapshot.activeTrack)) == 0U ||
        (snapshot.enabledMask &
            sequencerHistoryTrackBit(snapshot.activeTrack)) == 0U ||
        (snapshot.drumTrackMask &
            static_cast<uint16_t>(~snapshot.capturedTrackMask)) != 0U) {
        return false;
    }
    for (uint8_t track = 0U;
         track < SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        if ((snapshot.capturedTrackMask & sequencerHistoryTrackBit(track)) == 0U) {
            continue;
        }
        const bool drum = (snapshot.drumTrackMask &
            sequencerHistoryTrackBit(track)) != 0U;
        if (drum != static_cast<bool>(snapshot.drumTracks[track])) {
            return false;
        }
        const auto& source = snapshot.tracks[track];
        if (!source.ccLanesCaptured ||
            (source.graph && !source.graph->enabled) ||
            (source.ccLanes && !validSequencerCcLaneBank(*source.ccLanes))) {
            return false;
        }
    }
    return true;
}

FLASHMEM void resetHistoryPatternSnapshotFromBefore(
    const SequencerHistoryPatternSnapshot& before,
    uint8_t focusedStep,
    SequencerHistoryPatternSnapshot& out
) {
    out.reset();
    auto& flat = out.flat;
    flat.length = SequencerPatternState::DEFAULT_LENGTH;
    flat.playStart = 0U;
    flat.loopStart = 0U;
    flat.loopEnd = SequencerPatternState::DEFAULT_LENGTH;
    flat.stepsPerBeat = SequencerPatternState::DEFAULT_STEPS_PER_BEAT;
    flat.enabledMask = {};
    flat.stepDataRevision = before.flat.stepDataRevision + 1U;
    flat.patternVariationRevision = before.flat.patternVariationRevision + 1U;
    flat.patternScaleRevision = before.flat.patternScaleRevision + 1U;
    flat.patternTimingRevision = before.flat.patternTimingRevision + 1U;
    flat.graphRevision = before.flat.graphRevision + 1U;
    flat.swingOffsetPercent = 0;
    flat.patternNudgePercent = 0;
    flat.effectiveSwingPercent = 0U;
    flat.variationRanges = {};
    flat.scalePolicy = SequencerPatternScalePolicy::INHERIT_PROJECT;
    flat.scaleOverride = {};
    flat.pitchEditMode = SequencerPitchEditMode::FOLLOW_SCALE;
    flat.effectiveScaleSettings = {};
    flat.note.fill(SequencerPatternState::DEFAULT_NOTE);
    flat.velocity.fill(SequencerPatternState::DEFAULT_VELOCITY);
    flat.gate.fill(SequencerPatternState::DEFAULT_GATE_PERCENT);
    flat.nudge.fill(0);
    flat.probability.fill(SequencerPatternState::DEFAULT_PROBABILITY);
    out.ccLaneRevision = before.ccLaneRevision + 1U;
    out.focusedStep = focusedStep;
    out.ccLanesCaptured = true;
}

FLASHMEM bool cloneHistoryPatternSnapshotFromBefore(
    const SequencerHistoryPatternSnapshot& before,
    uint8_t focusedStep,
    SequencerHistoryPatternSnapshot& out
) {
    out.reset();
    out.flat = before.flat;
    out.ccLaneRevision = before.ccLaneRevision;
    out.focusedStep = focusedStep;
    out.ccLanesCaptured = true;
    if (!cloneSnapshotGraph(before, out.graph)) return false;
    return cloneSequencerCcLaneBank(out.ccLanes, before.ccLanes.get());
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
FLASHMEM void SequencerHistoryTrackStructureSnapshot::reset() {
    enabledMask = 0x0001U;
    activeTrack = 0U;
    focusedStep = 0U;
    page = 0U;
    capturedTrackMask = 0x0001U;
    drumTrackMask = 0U;
    for (auto& track : tracks) track.reset();
    for (auto& drumTrack : drumTracks) drumTrack.reset();
}

FLASHMEM SequencerHistoryTrackStructureChange::SequencerHistoryTrackStructureChange() = default;
FLASHMEM SequencerHistoryTrackStructureChange::~SequencerHistoryTrackStructureChange() = default;
FLASHMEM SequencerHistoryTrackStructureChange::SequencerHistoryTrackStructureChange(
    SequencerHistoryTrackStructureChange&&
) noexcept = default;
FLASHMEM SequencerHistoryTrackStructureChange& SequencerHistoryTrackStructureChange::operator=(
    SequencerHistoryTrackStructureChange&&
) noexcept = default;

FLASHMEM SequencerPreparedStructureHistoryReplay::
    SequencerPreparedStructureHistoryReplay() = default;
FLASHMEM SequencerPreparedStructureHistoryReplay::
    ~SequencerPreparedStructureHistoryReplay() = default;
FLASHMEM SequencerPreparedStructureHistoryReplay::
    SequencerPreparedStructureHistoryReplay(
        SequencerPreparedStructureHistoryReplay&&) noexcept = default;
FLASHMEM SequencerPreparedStructureHistoryReplay&
SequencerPreparedStructureHistoryReplay::operator=(
    SequencerPreparedStructureHistoryReplay&&) noexcept = default;

FLASHMEM void SequencerPreparedStructureHistoryReplay::reset() {
    direction = SequencerHistoryDirection::Undo;
    entryIdentity = 0U;
    entry = nullptr;
    targetSnapshot = nullptr;
    macroStructure = nullptr;
    activation = {};
    capturedTrackMask = 0U;
    targetActiveTrack = SequencerTrackBankState::TRACK_COUNT;
    ready = false;
    for (auto& graph : bankGraphs) graph.reset();
    for (auto& ccLanes : bankCcLanes) ccLanes.reset();
    editorGraph.reset();
    editorCcLanes.reset();
}

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
        const uint16_t bit = sequencerHistoryTrackBit(i);
        if ((capturedMask & bit) == 0U) {
            out.tracks[i].reset();
            out.drumTracks[i].reset();
            continue;
        }
        if (!reserveHistorySnapshotStorage(bank, active, i, out.tracks[i])) {
            return false;
        }
        if (bank.isDrumTrack(i)) {
            out.drumTracks[i] = core::app::makeExtmemUnique<DrumTrackState>();
            if (!out.drumTracks[i]) return false;
        } else {
            out.drumTracks[i].reset();
        }
    }
    out.capturedTrackMask = capturedMask;
    out.drumTrackMask = static_cast<uint16_t>(
        bank.drumTrackMask() & capturedMask
    );
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
        const uint16_t bit = sequencerHistoryTrackBit(i);
        if ((capturedMask & bit) == 0U) {
            out.tracks[i].reset();
            out.drumTracks[i].reset();
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
        if (bank.isDrumTrack(i)) {
            if (!out.drumTracks[i]) return false;
            *out.drumTracks[i] = bank.drumTrack(i);
        } else {
            out.drumTracks[i].reset();
        }
    }
    out.enabledMask = bank.currentEnabledMask();
    out.activeTrack = bank.activeTrackIndex();
    out.focusedStep = active.focusedStep.get();
    out.page = active.page.get();
    out.capturedTrackMask = capturedMask;
    out.drumTrackMask = static_cast<uint16_t>(
        bank.drumTrackMask() & capturedMask
    );
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

FLASHMEM bool buildHistoryStructureSnapshotAfterFromBefore(
    SequencerHistoryTrackStructureChange& change,
    uint16_t enabledMask,
    uint8_t activeTrack,
    uint8_t focusedStep,
    uint8_t page,
    uint16_t canonicalResetTrackMask
) {
    const auto& before = change.before;
    const uint16_t frozenMask = before.capturedTrackMask;
    if (!validStructureSnapshotSource(before) ||
        activeTrack >= SequencerTrackBankState::TRACK_COUNT ||
        enabledMask != sequencerHistorySanitizeTrackMask(enabledMask) ||
        (enabledMask & sequencerHistoryTrackBit(activeTrack)) == 0U ||
        (frozenMask & sequencerHistoryTrackBit(activeTrack)) == 0U ||
        (canonicalResetTrackMask & static_cast<uint16_t>(~frozenMask)) != 0U ||
        focusedStep >= SequencerState::MAX_STEPS ||
        page >= SequencerState::PAGE_COUNT) {
        return false;
    }

    const bool activeReset =
        (canonicalResetTrackMask & sequencerHistoryTrackBit(activeTrack)) != 0U;
    const uint8_t activeLength = activeReset
        ? SequencerPatternState::DEFAULT_LENGTH
        : before.tracks[activeTrack].flat.length;
    if (activeLength == 0U || activeLength > SequencerState::MAX_STEPS ||
        focusedStep >= activeLength) {
        return false;
    }

    auto& after = change.after;
    after.reset();
    after.enabledMask = enabledMask;
    after.activeTrack = activeTrack;
    after.focusedStep = focusedStep;
    after.page = page;
    after.capturedTrackMask = frozenMask;
    after.drumTrackMask = static_cast<uint16_t>(
        before.drumTrackMask & static_cast<uint16_t>(~canonicalResetTrackMask)
    );

    for (uint8_t track = 0U;
         track < SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const uint16_t bit = sequencerHistoryTrackBit(track);
        if ((frozenMask & bit) == 0U) continue;
        if ((canonicalResetTrackMask & bit) != 0U) {
            resetHistoryPatternSnapshotFromBefore(
                before.tracks[track],
                focusedStep,
                after.tracks[track]
            );
            after.drumTracks[track].reset();
            continue;
        }
        // Frozen order: After Graph then CC, ascending Track.
        if (!cloneHistoryPatternSnapshotFromBefore(
                before.tracks[track],
                focusedStep,
                after.tracks[track]
            )) {
            return false;
        }
        if ((before.drumTrackMask & bit) != 0U) {
            if (!before.drumTracks[track]) return false;
            after.drumTracks[track] = core::app::makeExtmemUnique<
                DrumTrackState
            >(*before.drumTracks[track]);
            if (!after.drumTracks[track]) return false;
        }
    }
    return true;
}

FLASHMEM bool liveHistoryStructureSnapshotMatches(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    const SequencerHistoryTrackStructureSnapshot& snapshot
) {
    if (!validStructureSnapshotSource(snapshot) ||
        bank.currentEnabledMask() != snapshot.enabledMask ||
        bank.activeTrackIndex() != snapshot.activeTrack ||
        active.focusedStep.get() != snapshot.focusedStep ||
        active.page.get() != snapshot.page) {
        return false;
    }

    for (uint8_t track = 0U;
         track < SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        if ((snapshot.capturedTrackMask & sequencerHistoryTrackBit(track)) == 0U) {
            continue;
        }
        const auto& live = canonicalTrackPattern(bank, active, track);
        if (!liveHistoryPatternSnapshotMatches(live, snapshot.tracks[track])) {
            return false;
        }
        const bool expectedDrum = (snapshot.drumTrackMask &
            sequencerHistoryTrackBit(track)) != 0U;
        if (bank.isDrumTrack(track) != expectedDrum) return false;
        if (expectedDrum && (!snapshot.drumTracks[track] ||
            std::memcmp(
                &bank.drumTrack(track),
                snapshot.drumTracks[track].get(),
                sizeof(DrumTrackState)
            ) != 0)) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool prepareHistoryStructureReplayOwners(
    const SequencerHistoryTrackStructureSnapshot& snapshot,
    uint8_t liveActiveTrack,
    SequencerPreparedStructureHistoryReplay& out
) {
    out.reset();
    if (!validStructureSnapshotSource(snapshot)) {
        return false;
    }
    const uint16_t capturedMask = snapshot.capturedTrackMask;
    const uint8_t targetActive = snapshot.activeTrack;
    if (liveActiveTrack >= SequencerTrackBankState::TRACK_COUNT ||
        (capturedMask & sequencerHistoryTrackBit(liveActiveTrack)) == 0U) {
        return false;
    }

    out.targetSnapshot = &snapshot;
    out.capturedTrackMask = capturedMask;
    out.targetActiveTrack = targetActive;
    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((capturedMask & sequencerHistoryTrackBit(i)) == 0U) continue;
        if (!cloneSnapshotGraph(snapshot.tracks[i], out.bankGraphs[i]) ||
            !cloneSequencerCcLaneBank(
                out.bankCcLanes[i], snapshot.tracks[i].ccLanes.get())) {
            out.reset();
            return false;
        }
    }
    if (!cloneSnapshotGraph(snapshot.tracks[targetActive], out.editorGraph) ||
        !cloneSequencerCcLaneBank(
            out.editorCcLanes, snapshot.tracks[targetActive].ccLanes.get())) {
        out.reset();
        return false;
    }
    out.ready = true;
    return true;
}

FLASHMEM void commitPreparedHistoryStructureReplayState(
    SequencerTrackBankState& bank,
    SequencerState& active,
    SequencerPreparedStructureHistoryReplay& replay
) noexcept {
    const auto* snapshot = replay.targetSnapshot;
    if (!replay.ready || snapshot == nullptr ||
        !validStructureSnapshotSource(*snapshot) ||
        replay.capturedTrackMask != snapshot->capturedTrackMask ||
        replay.targetActiveTrack != snapshot->activeTrack ||
        (replay.capturedTrackMask &
            sequencerHistoryTrackBit(bank.activeTrackIndex())) == 0U) {
        failStructureHistoryInvariant();
    }

    for (uint8_t i = 0; i < SequencerTrackBankState::TRACK_COUNT; ++i) {
        if ((replay.capturedTrackMask & sequencerHistoryTrackBit(i)) == 0U) continue;
        installTrackContentSnapshotWithOwnedPayload(
            bank.track(i),
            snapshot->tracks[i].flat,
            std::move(replay.bankGraphs[i]),
            std::move(replay.bankCcLanes[i])
        );
    }

    const uint8_t targetActive = replay.targetActiveTrack;
    installTrackContentSnapshotToEditorWithOwnedPayload(
        active,
        snapshot->tracks[targetActive].flat,
        std::move(replay.editorGraph),
        std::move(replay.editorCcLanes)
    );
    commitHistoryStructureDrumSnapshot(bank, *snapshot);
    bank.syncSharedTrackState(snapshot->enabledMask, targetActive);
    active.focusedStep.set(snapshot->focusedStep);
    active.page.set(snapshot->page);
    replay.ready = false;
}

FLASHMEM void commitHistoryStructureDrumSnapshot(
    SequencerTrackBankState& bank,
    const SequencerHistoryTrackStructureSnapshot& snapshot
) noexcept {
    if (!validStructureSnapshotSource(snapshot)) {
        failStructureHistoryInvariant();
    }
    for (uint8_t track = 0U;
         track < SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const uint16_t bit = sequencerHistoryTrackBit(track);
        if ((snapshot.capturedTrackMask & bit) == 0U) continue;
        if ((snapshot.drumTrackMask & bit) != 0U) {
            if (!snapshot.drumTracks[track]) failStructureHistoryInvariant();
            bank.restoreDrumTrack(
                track,
                SequencerTrackKind::DRUM,
                *snapshot.drumTracks[track]
            );
        } else {
            (void)bank.setTrackKind(
                track,
                SequencerTrackKind::INSTRUMENT,
                false
            );
        }
    }
}

FLASHMEM bool sameMusicalHistoryStructureSnapshot(
    const SequencerHistoryTrackStructureSnapshot& lhs,
    const SequencerHistoryTrackStructureSnapshot& rhs
) {
    if (lhs.enabledMask != rhs.enabledMask ||
        lhs.activeTrack != rhs.activeTrack ||
        lhs.drumTrackMask != rhs.drumTrackMask) {
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
        const bool lhsDrum = (lhs.drumTrackMask &
            sequencerHistoryTrackBit(i)) != 0U;
        const bool rhsDrum = (rhs.drumTrackMask &
            sequencerHistoryTrackBit(i)) != 0U;
        if (lhsDrum != rhsDrum) return false;
        if (lhsDrum && (!lhs.drumTracks[i] || !rhs.drumTracks[i] ||
            std::memcmp(
                lhs.drumTracks[i].get(),
                rhs.drumTracks[i].get(),
                sizeof(DrumTrackState)
            ) != 0)) {
            return false;
        }
    }

    return true;
}

FLASHMEM bool captureMacroTrackStructureHistoryBefore(
    const core::state::macro::MacroPagesState& pages,
    uint16_t trackMask,
    SequencerHistoryTrackStructureChange& change,
    uint8_t affectedTrackIndex
) {
    const uint16_t sanitized = sequencerHistorySanitizeTrackMask(trackMask);
    const uint8_t invalidAffected =
        SequencerHistoryMacroTrackStructurePayload::INVALID_AFFECTED_TRACK;
    if (affectedTrackIndex != invalidAffected &&
        (affectedTrackIndex >= macro::TRACK_COUNT ||
         (sanitized & sequencerHistoryTrackBit(affectedTrackIndex)) == 0U)) {
        return false;
    }
    auto payload = core::app::makeExtmemUnique<
        SequencerHistoryMacroTrackStructurePayload
    >();
    if (!payload) return false;
    payload->beforeControl = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >(pages.control.authored);
    if (!payload->beforeControl) return false;
    payload->afterControl = core::app::makeExtmemUnique<
        core::state::modulation::ProjectControlDomainState
    >();
    if (!payload->afterControl) return false;
    payload->capturedTrackMask = sanitized;
    payload->affectedTrackIndex = affectedTrackIndex;
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

FLASHMEM bool validateMacroTrackStructureHistoryReplay(
    const core::state::macro::MacroPagesState& pages,
    const SequencerHistoryMacroTrackStructurePayload& payload,
    bool after
) {
    if (!payload.afterCaptured || payload.capturedTrackMask == 0U ||
        payload.capturedTrackMask !=
            sequencerHistorySanitizeTrackMask(payload.capturedTrackMask) ||
        (payload.affectedTrackIndex !=
             SequencerHistoryMacroTrackStructurePayload::INVALID_AFFECTED_TRACK &&
         (payload.affectedTrackIndex >= macro::TRACK_COUNT ||
          (payload.capturedTrackMask &
              sequencerHistoryTrackBit(payload.affectedTrackIndex)) == 0U))) {
        return false;
    }
    const auto* control = after && payload.afterControl
        ? payload.afterControl.get()
        : payload.beforeControl.get();
    return control != nullptr &&
        liveMacroTrackStructureMatches(pages, payload, !after);
}

FLASHMEM void commitMacroTrackStructureHistoryReplay(
    core::state::macro::MacroPagesState& pages,
    const SequencerHistoryMacroTrackStructurePayload& payload,
    bool after
) {
    if (!payload.afterCaptured || payload.capturedTrackMask == 0U ||
        payload.capturedTrackMask !=
            sequencerHistorySanitizeTrackMask(payload.capturedTrackMask)) {
        failStructureHistoryInvariant();
    }
    const auto* control = after && payload.afterControl
        ? payload.afterControl.get()
        : payload.beforeControl.get();
    if (control == nullptr) failStructureHistoryInvariant();
    if (std::memcmp(
            &pages.control.authored,
            control,
            sizeof(core::state::modulation::ProjectControlDomainState)
        ) != 0) {
        pages.control.authored = *control;
        pages.control.markAuthoredMutation();
    }
    const auto& tracks = after ? payload.afterTracks : payload.beforeTracks;
    for (uint8_t track = 0U; track < macro::TRACK_COUNT; ++track) {
        if ((payload.capturedTrackMask & sequencerHistoryTrackBit(track)) == 0U) {
            continue;
        }
        pages.tracks[track] = tracks[track];
    }
}

FLASHMEM void commitAdmittedMacroTrackStructureHistoryAfter(
    core::state::macro::MacroPagesState& pages,
    const SequencerHistoryMacroTrackStructurePayload& payload
) noexcept {
    if (!payload.afterCaptured || payload.capturedTrackMask == 0U ||
        payload.capturedTrackMask !=
            sequencerHistorySanitizeTrackMask(payload.capturedTrackMask) ||
        payload.beforeControl == nullptr) {
        failStructureHistoryInvariant();
    }
    if (payload.afterControl) {
        pages.control.authored = *payload.afterControl;
        pages.control.markAuthoredMutation();
    }
    for (uint8_t track = 0U; track < macro::TRACK_COUNT; ++track) {
        if ((payload.capturedTrackMask & sequencerHistoryTrackBit(track)) !=
            0U) {
            pages.tracks[track] = payload.afterTracks[track];
        }
    }
}

}  // namespace core::state::sequencer

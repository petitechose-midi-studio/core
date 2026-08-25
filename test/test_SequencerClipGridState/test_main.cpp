#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <iostream>

#include "app/ExtmemAllocator.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerClipGridState.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "../support/CoreStorages.hpp"

#if !defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
#error "This test requires native EXTMEM failure injection"
#endif

namespace {

namespace seq = core::state::sequencer;

void seed(
    seq::SequencerPatternState& pattern,
    seq::SequencerClipState& clip,
    uint8_t note
) {
    pattern.reset();
    pattern.setStepNoteAt(0U, note);
    pattern.enabledMask.set(oc::note::sequencer::StepBitMask128::prefixMask(1U));
    clip.reset();
}

seq::SequencerClipDocumentPtr capture(
    const seq::SequencerPatternState& pattern,
    const seq::SequencerClipState& clip
) {
    seq::SequencerClipDocumentPtr document;
    assert(seq::captureSequencerClipDocument(
        pattern,
        clip,
        seq::SequencerTrackKind::INSTRUMENT,
        nullptr,
        document
    ));
    return document;
}

void test_sparse_grid_capacity_and_snapshot_are_exact() {
    seq::SequencerClipGridState grid;
    seq::SequencerPatternState pattern;
    seq::SequencerClipState clip;
    seed(pattern, clip, 61U);

    assert(grid.residentSlot(0U) == 0U);
    assert(grid.isOccupied({0U, 0U}));
    assert(grid.occupiedCount() == 1U);

    for (uint8_t index = 0U;
         index < seq::SequencerClipGridState::MAX_INACTIVE_DOCUMENTS;
         ++index) {
        const uint8_t track = static_cast<uint8_t>(index / 7U);
        const uint8_t slot = static_cast<uint8_t>(1U + index % 7U);
        if (track > 0U && grid.residentSlot(track) == seq::SequencerClipGridState::INVALID_SLOT) {
            grid.synchronizeEnabledTracks(static_cast<uint16_t>((1U << (track + 1U)) - 1U));
        }
        auto document = capture(pattern, clip);
        assert(grid.installInactiveDocument({track, slot}, std::move(document)));
    }
    assert(grid.inactiveDocumentCount() ==
           seq::SequencerClipGridState::MAX_INACTIVE_DOCUMENTS);

    auto rejected = capture(pattern, clip);
    assert(!grid.installInactiveDocument({3U, 1U}, std::move(rejected)));
    assert(rejected != nullptr);

    seq::SequencerClipGridSnapshot snapshot;
    assert(seq::captureSequencerClipGridSnapshot(grid, snapshot));
    seq::SequencerClipGridState restored;
    assert(seq::applySequencerClipGridSnapshot(
        restored, snapshot, grid.enabledTrackMask()));
    assert(restored.inactiveDocumentCount() == grid.inactiveDocumentCount());
    assert(restored.occupiedCount() == grid.occupiedCount());
    for (uint8_t track = 0U; track < seq::SequencerClipGridState::TRACK_COUNT; ++track) {
        assert(restored.residentSlot(track) == grid.residentSlot(track));
        for (uint8_t slot = 0U; slot < seq::SequencerClipGridState::SLOT_COUNT; ++slot) {
            const seq::SequencerClipAddress address{track, slot};
            assert(restored.isOccupied(address) == grid.isOccupied(address));
            const auto* lhs = grid.inactiveDocument(address);
            const auto* rhs = restored.inactiveDocument(address);
            assert((lhs == nullptr) == (rhs == nullptr));
            if (lhs != nullptr) assert(seq::sameSequencerClipDocument(*lhs, *rhs));
        }
    }
    std::cout << "[PASS] sparse capacity and snapshot are exact\n";
}

void test_capture_and_snapshot_fail_without_mutating_destination() {
    seq::SequencerPatternState pattern;
    seq::SequencerClipState clip;
    seed(pattern, clip, 62U);
    assert(seq::ensureGraphRoot(pattern));

    seq::SequencerClipDocumentPtr destination = capture(pattern, clip);
    const auto* identity = destination.get();
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        assert(!seq::captureSequencerClipDocument(
            pattern,
            clip,
            seq::SequencerTrackKind::INSTRUMENT,
            nullptr,
            destination
        ));
    }
    assert(destination.get() == identity);

    seq::SequencerClipGridState source;
    auto extra = capture(pattern, clip);
    assert(source.installInactiveDocument({0U, 1U}, std::move(extra)));
    seq::SequencerClipGridSnapshot output;
    assert(seq::captureSequencerClipGridSnapshot(source, output));
    const auto* outputIdentity = output.documents[
        seq::SequencerClipGridState::cellIndex({0U, 1U})
    ].get();
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(!seq::captureSequencerClipGridSnapshot(source, output));
    }
    assert(output.documents[
        seq::SequencerClipGridState::cellIndex({0U, 1U})
    ].get() == outputIdentity);
    std::cout << "[PASS] allocation failures preserve destinations\n";
}

void test_grid_rejects_malformed_documents() {
    seq::SequencerClipGridState grid;
    seq::SequencerPatternState pattern;
    seq::SequencerClipState clip;
    seed(pattern, clip, 63U);
    auto malformed = capture(pattern, clip);
    malformed->trackKind = seq::SequencerTrackKind::DRUM;
    assert(!grid.installInactiveDocument({0U, 1U}, std::move(malformed)));
    assert(malformed != nullptr);
    assert(!grid.isOccupied({0U, 1U}));
    std::cout << "[PASS] malformed Clip documents are rejected\n";
}

void test_grid_enforces_the_aggregate_psram_budget() {
    seq::SequencerClipGridState grid;
    grid.synchronizeEnabledTracks(0xFFFFU);
    seq::SequencerPatternState pattern;
    seq::SequencerClipState clip;
    seed(pattern, clip, 63U);
    assert(seq::ensureGraphRoot(pattern));

    bool rejected = false;
    for (uint8_t track = 0U; track < seq::SequencerClipGridState::TRACK_COUNT;
         ++track) {
        seq::DrumTrackState drum;
        auto document = capture(pattern, clip);
        document->trackKind = seq::SequencerTrackKind::DRUM;
        document->drum = core::app::makeExtmemUniqueCopy(drum);
        assert(document->drum != nullptr);
        const uint8_t slot = 1U;
        if (!grid.installInactiveDocument({track, slot}, std::move(document))) {
            assert(document != nullptr);
            rejected = true;
            break;
        }
    }
    assert(rejected);
    assert(grid.inactiveDocumentCount() <
           seq::SequencerClipGridState::MAX_INACTIVE_DOCUMENTS);
    assert(grid.inactiveRetainedBytes() <=
           seq::SequencerClipGridState::MAX_INACTIVE_RETAINED_BYTES);
    std::cout << "[PASS] aggregate Clip PSRAM budget is enforced\n";
}

void test_launcher_metadata_survives_snapshot_move_and_history() {
    seq::SequencerClipGridState grid;
    seq::SequencerPatternState pattern;
    seq::SequencerClipState clip;
    seed(pattern, clip, 66U);
    auto document = capture(pattern, clip);
    assert(grid.installInactiveDocument({0U, 1U}, std::move(document)));

    const seq::SequencerLauncherBehavior clipBehavior{
        .length = 2U,
        .follow = seq::sequencerLauncherFollowTarget(3U),
        .quantization = seq::SequencerLauncherFollowQuantization::BEAT,
    };
    const seq::SequencerLauncherBehavior sceneBehavior{
        .length = 4U,
        .follow = seq::sequencerLauncherFollowTarget(2U),
        .quantization = seq::SequencerLauncherFollowQuantization::BAR,
    };
    assert(grid.setClipBehavior({0U, 1U}, clipBehavior));
    assert(grid.setStop({0U, 3U}));
    assert(grid.setSceneBehavior(1U, sceneBehavior));
    assert(grid.sceneUsed(1U));
    assert(!grid.sceneUsed(2U));
    assert(grid.sceneUsed(3U));
    assert(grid.lastNavigableScene() == 4U);

    seq::SequencerClipGridSnapshot snapshot;
    assert(seq::captureSequencerClipGridSnapshot(grid, snapshot));
    seq::SequencerClipGridState restored;
    assert(seq::applySequencerClipGridSnapshot(
        restored, snapshot, grid.enabledTrackMask()));
    assert(restored.clipBehavior({0U, 1U}) == clipBehavior);
    assert(restored.isStop({0U, 3U}));
    assert(restored.sceneBehavior(1U) == sceneBehavior);
    assert(restored.sceneUsed(1U));
    assert(!restored.sceneUsed(2U));
    assert(restored.sceneUsed(3U));

    auto move = seq::prepareSequencerClipMoveChange(
        restored, {0U, 1U}, {0U, 2U});
    assert(move);
    assert(seq::applySequencerClipStructureChange(restored, *move, true));
    assert(restored.clipBehavior({0U, 1U}) ==
           seq::SequencerLauncherBehavior{});
    assert(restored.clipBehavior({0U, 2U}) == clipBehavior);
    assert(seq::applySequencerClipStructureChange(restored, *move, false));
    assert(restored.clipBehavior({0U, 1U}) == clipBehavior);

    auto remove = seq::prepareSequencerClipDeleteChange(restored, {0U, 1U});
    assert(remove);
    assert(seq::applySequencerClipStructureChange(restored, *remove, true));
    assert(!restored.isOccupied({0U, 1U}));
    assert(seq::applySequencerClipStructureChange(restored, *remove, false));
    assert(restored.isOccupied({0U, 1U}));
    assert(restored.clipBehavior({0U, 1U}) == clipBehavior);
    assert(restored.isStop({0U, 3U}));
    assert(restored.sceneBehavior(1U) == sceneBehavior);
    assert(!restored.setClipBehavior({0U, 1U}, {
        .length = 1U,
        .follow = static_cast<seq::SequencerLauncherFollowChoice>(42U),
        .quantization = seq::SequencerLauncherFollowQuantization::BEAT,
    }));
    std::cout << "[PASS] launcher metadata survives snapshots and history\n";
}

void test_follow_choice_order_matches_the_editor_grammar() {
    using Choice = seq::SequencerLauncherFollowChoice;
    assert(seq::stepSequencerLauncherFollowChoice(Choice::NONE, 1) ==
           Choice::NEXT);
    assert(seq::stepSequencerLauncherFollowChoice(Choice::NEXT, 1) ==
           Choice::FIRST);
    assert(seq::stepSequencerLauncherFollowChoice(Choice::FIRST, 1) ==
           Choice::RANDOM_OTHER);
    assert(seq::stepSequencerLauncherFollowChoice(Choice::RANDOM_OTHER, 1) ==
           Choice::RANDOM_ANY);
    assert(seq::stepSequencerLauncherFollowChoice(Choice::RANDOM_ANY, 1) ==
           Choice::TARGET_1);
    assert(seq::stepSequencerLauncherFollowChoice(Choice::TARGET_8, 1) ==
           Choice::TARGET_8);
    assert(seq::stepSequencerLauncherFollowChoice(Choice::NONE, -1) ==
           Choice::NONE);
}

void test_resident_switch_preserves_both_clip_documents() {
    seq::SequencerTrackBankState bank;
    seq::SequencerState active;
    seq::SequencerClipGridState grid;
    assert(seq::initializeTrackBankFromActive(bank, active));

    seed(active.pattern, active.clip, 64U);
    assert(seq::ensureGraphRoot(active.pattern));
    auto secondPattern = core::app::makeExtmemUnique<seq::SequencerPatternState>();
    assert(secondPattern);
    seq::SequencerClipState secondClip;
    seed(*secondPattern, secondClip, 72U);
    auto second = capture(*secondPattern, secondClip);
    assert(grid.installInactiveDocument({0U, 1U}, std::move(second)));
    const uint32_t retainedBeforeSwitch = grid.inactiveRetainedBytes();

#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(seq::switchResidentSequencerClip(grid, bank, active, {0U, 1U}));
        assert(core::app::testing::extmemAllocationAttempt == 0U);
    }
#else
    assert(seq::switchResidentSequencerClip(grid, bank, active, {0U, 1U}));
#endif
    assert(grid.residentSlot(0U) == 1U);
    assert(active.pattern.note[0] == 72U);
    assert(grid.inactiveDocument({0U, 0U}) != nullptr);
    assert(grid.inactiveDocument({0U, 0U})->pattern.note[0] == 64U);
    assert(grid.inactiveRetainedBytes() > retainedBeforeSwitch);

    assert(seq::switchResidentSequencerClip(grid, bank, active, {0U, 0U}));
    assert(grid.residentSlot(0U) == 0U);
    assert(active.pattern.note[0] == 64U);
    assert(grid.inactiveDocument({0U, 1U})->pattern.note[0] == 72U);
    assert(grid.inactiveRetainedBytes() == retainedBeforeSwitch);
    std::cout << "[PASS] resident switching preserves both Clips without allocation\n";
}

void test_history_targets_the_authored_clip_after_resident_switch() {
    seq::SequencerTrackBankState bank;
    seq::SequencerState active;
    seq::SequencerClipGridState grid;
    seq::SequencerHistoryService history;
    assert(seq::initializeTrackBankFromActive(bank, active));

    seed(active.pattern, active.clip, 64U);
    auto secondPattern = core::app::makeExtmemUnique<seq::SequencerPatternState>();
    assert(secondPattern);
    seq::SequencerClipState secondClip;
    seed(*secondPattern, secondClip, 72U);
    auto second = capture(*secondPattern, secondClip);
    assert(grid.installInactiveDocument({0U, 1U}, std::move(second)));

    auto change = core::app::makeExtmemUnique<seq::SequencerHistoryPatternChange>();
    assert(change);
    change->trackIndex = 0U;
    change->storage = seq::SequencerHistoryPatternStorage::FlatOnly;
    change->descriptor = {
        .kind = seq::SequencerHistoryActionKind::StepPropertyEdit,
        .trackIndex = 0U,
        .stepIndex = 0U,
        .clipIndex = 0U,
        .property = seq::StepProperty::NOTE,
    };
    seq::captureFlatHistorySnapshot(active, change->before);
    active.pattern.setStepNoteAt(0U, 65U);
    seq::captureFlatHistorySnapshot(active, change->after);
    assert(history.canRecordPattern(*change));
    history.recordPreparedPattern(std::move(change));

    assert(seq::switchResidentSequencerClip(grid, bank, active, {0U, 1U}));
    assert(active.pattern.note[0] == 72U);

    const uint32_t generationBeforeUndo = grid.generation({0U, 0U});
    const auto undone = history.undoWithResult(bank, active, grid);
    assert(undone.applied && undone.descriptor.clipIndex == 0U);
    assert(active.pattern.note[0] == 72U);
    assert(grid.inactiveDocument({0U, 0U})->pattern.note[0] == 64U);
    assert(grid.generation({0U, 0U}) != generationBeforeUndo);

    const auto redone = history.redoWithResult(bank, active, grid);
    assert(redone.applied && redone.descriptor.clipIndex == 0U);
    assert(active.pattern.note[0] == 72U);
    assert(grid.inactiveDocument({0U, 0U})->pattern.note[0] == 65U);
    std::cout << "[PASS] history follows exact Clip identity\n";
}

void test_clip_structure_history_transfers_ownership_without_project_copies() {
    seq::SequencerClipGridState grid;
    seq::SequencerTrackBankState bank;
    seq::SequencerState active;
    seq::SequencerHistoryService history;
    assert(seq::initializeTrackBankFromActive(bank, active));

    seq::SequencerPatternState pattern;
    seq::SequencerClipState clip;
    seed(pattern, clip, 67U);
    auto create = seq::prepareSequencerClipInstallChange(
        seq::SequencerClipStructureAction::CREATE,
        {0U, 1U},
        capture(pattern, clip));
    assert(create && history.canRecordClipStructure(*create));
    assert(seq::applySequencerClipStructureChange(grid, *create, true));
    history.commitAdmittedClipStructure(std::move(create));
    assert(grid.inactiveDocument({0U, 1U})->pattern.note[0] == 67U);

    assert(history.undoWithResult(bank, active, grid).applied);
    assert(!grid.isOccupied({0U, 1U}));
    assert(history.redoWithResult(bank, active, grid).applied);
    assert(grid.isOccupied({0U, 1U}));

    auto move = seq::prepareSequencerClipMoveChange(
        grid, {0U, 1U}, {0U, 2U});
    assert(move && history.canRecordClipStructure(*move));
    assert(seq::applySequencerClipStructureChange(grid, *move, true));
    history.commitAdmittedClipStructure(std::move(move));
    assert(!grid.isOccupied({0U, 1U}) && grid.isOccupied({0U, 2U}));
    assert(history.undoWithResult(bank, active, grid).applied);
    assert(grid.isOccupied({0U, 1U}) && !grid.isOccupied({0U, 2U}));
    assert(history.redoWithResult(bank, active, grid).applied);

    const auto* source = grid.inactiveDocument({0U, 2U});
    assert(source != nullptr);
    seq::SequencerClipDocumentPtr duplicateDocument;
    assert(seq::cloneSequencerClipDocument(*source, duplicateDocument));
    auto duplicate = seq::prepareSequencerClipInstallChange(
        seq::SequencerClipStructureAction::DUPLICATE_CLIP,
        {0U, 3U},
        std::move(duplicateDocument));
    assert(duplicate && history.canRecordClipStructure(*duplicate));
    assert(seq::applySequencerClipStructureChange(grid, *duplicate, true));
    history.commitAdmittedClipStructure(std::move(duplicate));
    assert(grid.inactiveDocument({0U, 3U})->pattern.note[0] == 67U);

    auto remove = seq::prepareSequencerClipDeleteChange(grid, {0U, 3U});
    assert(remove && history.canRecordClipStructure(*remove));
    assert(seq::applySequencerClipStructureChange(grid, *remove, true));
    history.commitAdmittedClipStructure(std::move(remove));
    assert(!grid.isOccupied({0U, 3U}));
    assert(history.undoWithResult(bank, active, grid).applied);
    assert(grid.isOccupied({0U, 3U}));
    assert(history.redoWithResult(bank, active, grid).applied);
    assert(!grid.isOccupied({0U, 3U}));
    std::cout << "[PASS] Clip structure history transfers one document owner\n";
}

void test_cross_track_transfer_is_kind_safe_and_preserves_residency() {
    seq::SequencerClipGridState grid;
    seq::SequencerTrackBankState bank;
    seq::SequencerState active;
    assert(seq::initializeTrackBankFromActive(bank, active));
    bank.syncSharedTrackState(0x0007U, 0U);
    grid.synchronizeEnabledTracks(0x0007U);

    seq::SequencerPatternState pattern;
    seq::SequencerClipState clip;
    seed(pattern, clip, 69U);
    auto document = capture(pattern, clip);
    assert(grid.installInactiveDocument({0U, 1U}, std::move(document)));

    assert(seq::compatibleSequencerClipTrackMask(grid, bank, {0U, 1U}) ==
           0x0007U);
    assert(seq::canTransferSequencerClip(
        grid,
        bank,
        {0U, 1U},
        {1U, 1U},
        seq::SequencerClipStructureAction::MOVE
    ));
    assert(!seq::canTransferSequencerClip(
        grid,
        bank,
        {0U, 0U},
        {1U, 1U},
        seq::SequencerClipStructureAction::MOVE
    ));
    assert(seq::canTransferSequencerClip(
        grid,
        bank,
        {0U, 0U},
        {1U, 1U},
        seq::SequencerClipStructureAction::DUPLICATE_CLIP
    ));

    assert(bank.setTrackKind(
        2U,
        seq::SequencerTrackKind::DRUM,
        true,
        seq::DrumKitPreset::GENERAL_MIDI
    ));
    assert(seq::compatibleSequencerClipTrackMask(grid, bank, {0U, 1U}) ==
           0x0003U);
    assert(!seq::canTransferSequencerClip(
        grid,
        bank,
        {0U, 1U},
        {2U, 1U},
        seq::SequencerClipStructureAction::DUPLICATE_CLIP
    ));

    seq::SequencerClipAddress destination{};
    assert(seq::firstSequencerClipTransferDestination(
        grid,
        bank,
        {0U, 1U},
        seq::SequencerClipStructureAction::MOVE,
        destination
    ));
    assert(destination.track == 0U && destination.slot == 2U);

    auto move = seq::prepareSequencerClipMoveChange(
        grid, {0U, 1U}, {1U, 1U});
    assert(move);
    assert(seq::applySequencerClipStructureChange(grid, *move, true));
    assert(!grid.isOccupied({0U, 1U}));
    assert(grid.inactiveDocument({1U, 1U})->pattern.note[0] == 69U);
    assert(seq::applySequencerClipStructureChange(grid, *move, false));
    assert(grid.inactiveDocument({0U, 1U})->pattern.note[0] == 69U);

    std::cout
        << "[PASS] cross-Track Clip transfer preserves kind and residency\n";
}

void test_core_clip_api_keeps_structure_and_history_coherent() {
    test_support::CoreStorages storages;
    core::state::CoreState state(storages.settings);

    seed(state.sequencer.pattern, state.sequencer.clip, 60U);
    assert(state.duplicateSequencerClip({0U, 0U}, {0U, 1U}));
    assert(state.sequencerClips.isOccupied({0U, 1U}));
    assert(state.undoSequencerHistory());
    assert(!state.sequencerClips.isOccupied({0U, 1U}));
    assert(state.redoSequencerHistory());

    assert(state.switchSequencerClipForEditing({0U, 1U}));
    assert(state.sequencerClips.residentSlot(0U) == 1U);
    assert(state.deleteSequencerClip({0U, 1U}));
    assert(state.sequencerClips.residentSlot(0U) ==
           seq::SequencerClipGridState::INVALID_SLOT);
    assert(state.sequencerTracks.isTrackEnabled(0U));
    assert(state.undoSequencerHistory());
    assert(state.sequencerClips.residentSlot(0U) == 1U);
    assert(state.redoSequencerHistory());
    assert(state.undoSequencerHistory());
    // Editing residency and musical activation are deliberately independent:
    // Clip 0 still feeds the runtime until Clip 1's launch is published.
    assert(!state.deleteSequencerClip({0U, 0U}));
    assert(state.requestSequencerClipLaunch(
        {0U, 1U}, seq::SequencerClipLaunchQuantization::IMMEDIATE));
    const auto publication =
        state.sequencerClipLaunches.captureRuntimePublication(
            state.sequencerClips, false);
    assert((publication.queuedMask & 0x1U) != 0U);
    state.sequencerClipLaunches.applyRuntimePublication(publication, 0U, 1U);
    assert(state.sequencerClipLaunches.markAppliedFromRealtime(
        0U, publication.generations[0U]));
    (void)state.sequencerClipLaunches.publishRealtimeTelemetry();
    assert(state.deleteSequencerClip({0U, 0U}));
    assert(!state.sequencerClips.isOccupied({0U, 0U}));
    assert(state.undoSequencerHistory());
    assert(state.sequencerClips.isOccupied({0U, 0U}));

    assert(state.deleteSequencerClip({0U, 0U}));
    assert(state.deleteSequencerClip({0U, 1U}));
    assert(state.sequencerTracks.isTrackEnabled(0U));
    assert(state.sequencerClips.residentSlot(0U) ==
           seq::SequencerClipGridState::INVALID_SLOT);
    assert(state.sequencerClips.occupiedCount() == 0U);
    assert(state.sequencerClipLaunches.stopped(0U));
    assert(state.undoSequencerHistory());
    assert(state.sequencerClips.isResident({0U, 1U}));
    assert(state.redoSequencerHistory());
    assert(state.sequencerClips.occupiedCount() == 0U);

    assert(state.createSequencerClip({0U, 3U}));
    assert(state.sequencerClips.residentSlot(0U) ==
           seq::SequencerClipGridState::INVALID_SLOT);
    assert(state.switchSequencerClipForEditing({0U, 3U}));
    assert(state.sequencerClips.isResident({0U, 3U}));

    assert(state.setSharedTrackState(0x0003U, 0U));
    assert(state.duplicateSequencerClip({0U, 3U}, {1U, 1U}));
    assert(state.sequencerClips.isOccupied({1U, 1U}));
    assert(state.moveSequencerClip({1U, 1U}, {0U, 2U}));
    assert(!state.sequencerClips.isOccupied({1U, 1U}));
    assert(state.sequencerClips.isOccupied({0U, 2U}));
    assert(state.undoSequencerHistory());
    assert(state.sequencerClips.isOccupied({1U, 1U}));
    assert(!state.sequencerClips.isOccupied({0U, 2U}));
    assert(state.redoSequencerHistory());
    assert(!state.moveSequencerClip({0U, 3U}, {1U, 2U}));

    assert(state.sequencerTracks.setTrackKind(
        1U,
        seq::SequencerTrackKind::DRUM,
        true,
        seq::DrumKitPreset::GENERAL_MIDI
    ));
    assert(!state.duplicateSequencerClip({0U, 3U}, {1U, 2U}));
    std::cout << "[PASS] Core Clip API preserves structural history\n";
}

}  // namespace

int main() {
    test_sparse_grid_capacity_and_snapshot_are_exact();
    test_capture_and_snapshot_fail_without_mutating_destination();
    test_grid_rejects_malformed_documents();
    test_grid_enforces_the_aggregate_psram_budget();
    test_launcher_metadata_survives_snapshot_move_and_history();
    test_follow_choice_order_matches_the_editor_grammar();
    test_resident_switch_preserves_both_clip_documents();
    test_history_targets_the_authored_clip_after_resident_switch();
    test_clip_structure_history_transfers_ownership_without_project_copies();
    test_cross_track_transfer_is_kind_safe_and_preserves_residency();
    test_core_clip_api_keeps_structure_and_history_coherent();
    std::cout << "All SequencerClipGridState tests passed.\n";
    return 0;
}

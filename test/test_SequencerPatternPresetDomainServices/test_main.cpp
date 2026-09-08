#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>

#include "../../src/handler/sequencer/SequencerPatternPresetDomainServices.hpp"
#include "../../src/handler/sequencer/SequencerPatternPresetLibraryAdapter.hpp"
#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/sequencer/SequencerClipRegionOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"
#include "../support/SequencerHistoryTransactionAssertions.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"

namespace {

namespace seq = core::state::sequencer;
using core::handler::SequencerPatternPresetActivation;
using core::handler::SequencerPatternPresetDomainServices;
using core::handler::SequencerPatternPresetDomainStatus;
using core::handler::SequencerPatternPresetPreviewSession;
using core::persistence::ProductDirectoryCatalog;
using core::persistence::ProductFileService;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
        "midi-studio-core-pattern-preset-domain-services-test";
}

void resetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(testRoot(), error);
}

struct Harness {
    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::impl::HostFileSystem filesystem;
    ProductFileService files;
    ProductDirectoryCatalog catalog;
    SequencerPatternPresetDomainServices presets;
    uint32_t nowMs = 0U;

    Harness()
        : state(storages.settings)
        , filesystem(testRoot().string().c_str())
        , files(filesystem)
        , catalog(files)
        , presets(SequencerPatternPresetDomainServices::fromCoreState(
              state,
              files,
              catalog
          )) {
        resetTestRoot();
        assert(filesystem.init());
        assert(files.init());
    }

    ~Harness() { resetTestRoot(); }

    void advanceCatalog() {
        ++nowMs;
        assert(files.persistenceJobs().beginTurn(nowMs));
        catalog.advance(nowMs, false);
    }
};

core::handler::SequencerPatternPresetListResult listSettled(
    Harness& harness,
    SequencerPatternPresetDomainServices::Entry* entries,
    seq::SequencerPatternPresetSourceFilter filter,
    seq::SequencerTrackKind trackKind,
    const char* anchorExclusive = nullptr,
    core::persistence::PatternPresetFilePageDirection direction =
        core::persistence::PatternPresetFilePageDirection::FORWARD,
    const seq::SequencerPatternPresetLocation& location = {}
) {
    auto listed = harness.presets.listPresetsPage(
        entries,
        15U,
        anchorExclusive,
        direction,
        filter,
        trackKind,
        location
    );
    for (uint8_t attempt = 0U;
         listed.status == SequencerPatternPresetDomainStatus::QUEUED &&
         attempt < 32U;
         ++attempt) {
        harness.advanceCatalog();
        listed = harness.presets.listPresetsPage(
            entries,
            15U,
            anchorExclusive,
            direction,
            filter,
            trackKind,
            location
        );
    }
    return listed;
}

void setInstrumentPattern(
    core::state::CoreState& state,
    uint8_t note,
    bool withMicro
) {
    auto& pattern = state.sequencer.pattern;
    pattern.reset();
    pattern.setContentLength(8U);
    pattern.setEnabled(2U, true);
    assert(state.sequencer.setStepDataAt(2U, note, 101U, 145U, -4, 83U));
    if (withMicro) {
        const auto sequence = seq::createMicroSequence(
            pattern,
            seq::rootStepNodeId(2U),
            3U
        );
        assert(sequence.ok);
    }
    state.markSequencerProjectMutated();
}

void testInstrumentPreviewAllocationFailuresAndNoFailCancel() {
    namespace tx = test_support::sequencer_transaction;
    bool reachedSuccess = false;
    for (std::size_t ordinal = 1U; ordinal <= 64U; ++ordinal) {
        Harness h;
        auto author = [&](uint8_t note) {
            setInstrumentPattern(h.state, note, true);
            auto* lanes = seq::ensureSequencerCcLaneBank(h.state.sequencer.pattern);
            assert(lanes != nullptr);
            seq::SequencerCcLaneDraft lane{};
            lane.destination.controller = 21U;
            assert(seq::createSequencerCcLane(*lanes, 0U, lane).changed());
            assert(seq::setSequencerCcLaneEvent(*lanes, 0U, 2U, note).changed());
            h.state.sequencer.pattern.bumpCcLaneRevision();
        };
        author(67U);
        assert(h.presets.savePreset(
            "pattern-preset-0001", h.presets.captureTarget(), false).ok());
        author(48U);
        test_support::drainNotifications();
        h.state.flushProjectMutationCoalescing();
        test_support::drainNotifications();
        h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
        // Preview must tolerate an unrelated noncanonical bank slot.
        h.state.sequencerTracks.track(0U).note[2U] = 99U;
        const auto target = h.presets.captureTarget();
        const auto inspected = h.presets.inspectPreset("pattern-preset-0001", target);
        assert(inspected.status == SequencerPatternPresetDomainStatus::OK);
        seq::SequencerHistoryPatternSnapshot before;
        tx::captureMusicalSnapshot(h.state, before);
        const auto invariant = tx::captureStateInvariant(h.state);
        SequencerPatternPresetPreviewSession preview;
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            const auto result = h.presets.previewPreset(
                "pattern-preset-0001", target,
                inspected.descriptor.previewKey, preview);
            if (!result.ok()) {
                assert(!preview.active());
                tx::assertFailureConsumed(ordinal);
                tx::assertStateInvariant(h.state, invariant);
            } else {
                reachedSuccess = true;
                assert(preview.active());
                tx::assertMaxPlusOneStillArmed(ordinal - 1U);
                assert(h.state.sequencer.pattern.note[2U] == 67U);
                assert(h.state.sequencer.pattern.ccLanes->lanes[0U].values[2U] == 67U);
                std::cout << "[MEASURE] melodic Graph+CC preview allocations="
                          << ordinal - 1U << '\n';
            }
        }
        if (reachedSuccess) {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(h.presets.cancelPresetPreview(preview).ok());
            tx::assertMaxPlusOneStillArmed(0U);
        }
        tx::assertMusicalSnapshot(h.state, before);
        assert(h.state.sequencerTracks.track(0U).note[2U] == 99U);
        const auto after = tx::captureStateInvariant(h.state);
        assert(after.bankGraphOwner == invariant.bankGraphOwner);
        assert(after.bankCcOwner == invariant.bankCcOwner);
        assert(after.modifiedCounter == invariant.modifiedCounter);
        assert(after.sequencerUndoCount == invariant.sequencerUndoCount);
        assert(after.projectUndoCount == invariant.projectUndoCount);
        if (reachedSuccess) break;
    }
    assert(reachedSuccess);
    std::cout << "[PASS] every melodic preview allocation fails atomically; Cancel allocates zero\n";
}

void testInstrumentLifecycleAndSingleUndo() {
    Harness h;
    setInstrumentPattern(h.state, 67U, true);
    assert(seq::setClipPlaybackRegion(h.state.sequencer, {8U, 1U, 2U, 7U}));
    const auto saveTarget = h.presets.captureTarget();
    const auto saved = h.presets.savePreset(
        "pattern-preset-0001",
        saveTarget,
        false
    );
    assert(saved.ok());

    setInstrumentPattern(h.state, 48U, false);
    assert(seq::setClipPlaybackRegion(h.state.sequencer, {8U, 0U, 3U, 6U}));
    const auto destinationClip = h.state.sequencer.clip;
    const auto target = h.presets.captureTarget();
    const auto inspected = h.presets.inspectPreset(
        "pattern-preset-0001",
        target
    );
    assert(inspected.status == SequencerPatternPresetDomainStatus::OK);
    assert(inspected.descriptor.compatibility ==
           seq::SequencerPatternPresetCompatibility::READY);
    assert(inspected.descriptor.patternLength == 8U);

    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
    const uint32_t modifiedBefore = h.state.project.metadata.modifiedCounter;
    SequencerPatternPresetPreviewSession cancelledPreview{};
    const auto previewed = h.presets.previewPreset(
        "pattern-preset-0001",
        target,
        inspected.descriptor.previewKey,
        cancelledPreview
    );
    assert(previewed.ok());
    assert(cancelledPreview.active());
    assert(h.state.sequencer.pattern.note[2U] == 67U);
    assert(seq::graphView(h.state.sequencer.pattern) != nullptr);
    assert(std::memcmp(
        &h.state.sequencer.clip,
        &destinationClip,
        sizeof(destinationClip)
    ) == 0);
    assert(h.state.sequencerTracks.track(0U).note[2U] == 48U);
    assert(h.state.sequencerHistory.undoCount() == undoBefore);
    assert(h.state.project.metadata.modifiedCounter == modifiedBefore);

    const auto cancelled = h.presets.cancelPresetPreview(cancelledPreview);
    assert(cancelled.ok());
    assert(!cancelledPreview.active());
    assert(h.state.sequencer.pattern.note[2U] == 48U);
    assert(seq::graphView(h.state.sequencer.pattern) == nullptr);
    assert(std::memcmp(
        &h.state.sequencer.clip,
        &destinationClip,
        sizeof(destinationClip)
    ) == 0);
    assert(h.state.sequencerTracks.track(0U).note[2U] == 48U);
    assert(h.state.sequencerHistory.undoCount() == undoBefore);
    assert(h.state.project.metadata.modifiedCounter == modifiedBefore);

    SequencerPatternPresetPreviewSession confirmedPreview{};
    assert(h.presets.previewPreset(
        "pattern-preset-0001",
        target,
        inspected.descriptor.previewKey,
        confirmedPreview
    ).ok());
    const auto applied = h.presets.confirmPresetPreview(confirmedPreview);
    assert(applied.ok());
    assert(applied.activation == SequencerPatternPresetActivation::APPLIED);
    assert(!confirmedPreview.active());
    assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);
    assert(h.state.project.metadata.modifiedCounter == modifiedBefore + 1U);
    assert(std::memcmp(
        &h.state.sequencer.clip,
        &destinationClip,
        sizeof(destinationClip)
    ) == 0);

    assert(h.state.undoSequencerHistory());
    assert(h.state.sequencer.pattern.note[2U] == 48U);
    assert(seq::graphView(h.state.sequencer.pattern) == nullptr);
    assert(std::memcmp(
        &h.state.sequencer.clip,
        &destinationClip,
        sizeof(destinationClip)
    ) == 0);
    assert(h.state.sequencerHistory.undoCount() == undoBefore);
    assert(h.state.redoSequencerHistory());
    assert(h.state.sequencer.pattern.note[2U] == 67U);
    assert(seq::graphView(h.state.sequencer.pattern) != nullptr);
    assert(std::memcmp(
        &h.state.sequencer.clip,
        &destinationClip,
        sizeof(destinationClip)
    ) == 0);

    const auto renamed = h.presets.renamePreset(
        "pattern-preset-0001",
        "Instrument Pattern 0001",
        "Broken Beat"
    );
    assert(renamed.ok());
    const auto renamedInspect = h.presets.inspectPreset(
        "pattern-preset-0001",
        h.presets.captureTarget()
    );
    assert(std::strcmp(
        renamedInspect.descriptor.metadata.semanticName,
        "Broken Beat"
    ) == 0);
    const auto removed = h.presets.deletePreset(
        "pattern-preset-0001",
        "Broken Beat"
    );
    assert(removed.ok());

    std::cout << "[PASS] Instrument Pattern preset lifecycle and single Undo\n";
}

void prepareDrumSource(Harness& h) {
    assert(h.state.sequencerTracks.setTrackKind(
        0U,
        seq::SequencerTrackKind::DRUM,
        true,
        seq::DrumKitPreset::GENERAL_MIDI
    ));
    auto& drum = h.state.sequencerTracks.drumTrack(0U);
    h.state.sequencer.drumSequencer.bindTrack(
        0U,
        drum,
        h.state.sequencerTracks
    );
    assert(drum.pattern.setStepEnabled(1U, 2U, true));
    assert(drum.pattern.setStepVelocity(1U, 2U, 111U));
    bool mappingChanged = false;
    const int16_t slot = seq::ensureDrumAdvancedRootSlot(
        drum,
        h.state.sequencer.pattern,
        1U,
        2U,
        mappingChanged
    );
    assert(slot >= 0 && mappingChanged);
    const auto sequence = seq::createMicroSequence(
        h.state.sequencer.pattern,
        seq::rootStepNodeId(static_cast<uint8_t>(slot)),
        2U
    );
    assert(sequence.ok);
    h.state.sequencerTracks.publishDrumMutation(0U);
    h.state.markSequencerProjectMutated();
}

void testDrumPreviewAllocationFailuresAndNoFailCancel() {
    namespace tx = test_support::sequencer_transaction;
    bool reachedSuccess = false;
    for (std::size_t ordinal = 1U; ordinal <= 64U; ++ordinal) {
        Harness h;
        prepareDrumSource(h);
        assert(h.presets.savePreset(
            "pattern-preset-0002", h.presets.captureTarget(), false).ok());
        auto& drum = h.state.sequencerTracks.drumTrack(0U);
        assert(drum.pattern.setStepVelocity(1U, 2U, 48U));
        h.state.sequencerTracks.publishDrumMutation(0U);
        h.state.markSequencerProjectMutated();
        test_support::drainNotifications();
        h.state.flushProjectMutationCoalescing();
        test_support::drainNotifications();
        h.state.acknowledgeProjectSessionSave(h.state.projectSessionSaveToken());
        const auto target = h.presets.captureTarget();
        const auto inspected = h.presets.inspectPreset("pattern-preset-0002", target);
        assert(inspected.status == SequencerPatternPresetDomainStatus::OK);
        seq::SequencerHistoryPatternSnapshot before;
        tx::captureMusicalSnapshot(h.state, before);
        const auto invariant = tx::captureStateInvariant(h.state);
        SequencerPatternPresetPreviewSession preview;
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            const auto result = h.presets.previewPreset(
                "pattern-preset-0002", target, inspected.descriptor.previewKey, preview);
            if (!result.ok()) {
                assert(!preview.active());
                tx::assertFailureConsumed(ordinal);
                tx::assertStateInvariant(h.state, invariant);
            } else {
                reachedSuccess = true;
                assert(preview.active());
                tx::assertMaxPlusOneStillArmed(ordinal - 1U);
                assert(drum.pattern.lanes[1U].velocity[2U] == 111U);
                assert(h.state.sequencer.pattern.graph);
                std::cout << "[MEASURE] Drum Graph preview allocations=" << ordinal - 1U << '\n';
            }
        }
        if (reachedSuccess) {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(h.presets.cancelPresetPreview(preview).ok());
            tx::assertMaxPlusOneStillArmed(0U);
        }
        assert(drum.pattern.lanes[1U].velocity[2U] == 48U);
        assert(drum.advancedRootSlot(1U, 2U) >= 0);
        tx::assertMusicalSnapshot(h.state, before);
        assert(!h.state.sequencerTracks.track(0U).graph);
        const auto after = tx::captureStateInvariant(h.state);
        assert(after.modifiedCounter == invariant.modifiedCounter);
        assert(after.sequencerUndoCount == invariant.sequencerUndoCount);
        assert(after.projectUndoCount == invariant.projectUndoCount);
        if (reachedSuccess) break;
    }
    assert(reachedSuccess);
    std::cout << "[PASS] every Drum preview allocation fails atomically; Cancel allocates zero\n";
}

void testDrumApplyPreservesKitAndQueuesAtLoop() {
    Harness h;
    prepareDrumSource(h);
    const auto saved = h.presets.savePreset(
        "pattern-preset-0002",
        h.presets.captureTarget(),
        false
    );
    assert(saved.ok());

    auto& drum = h.state.sequencerTracks.drumTrack(0U);
    auto destinationLane = drum.kit.lanes[1U];
    assert(seq::setDrumLaneName(destinationLane, "My Snare"));
    assert(seq::setDrumLaneColorIndex(destinationLane, 6U));
    assert(drum.kit.setLane(1U, destinationLane));
    assert(drum.pattern.setStepEnabled(1U, 2U, false));
    assert(drum.releaseAdvancedRootSlot(1U, 2U));
    h.state.sequencer.pattern.graph.reset();
    h.state.sequencerTracks.track(0U).graph.reset();
    h.state.sequencerTracks.publishDrumMutation(0U);
    h.state.markSequencerProjectMutated();
    h.state.statusBar.playing.set(true);

    const auto target = h.presets.captureTarget();
    const auto inspected = h.presets.inspectPreset(
        "pattern-preset-0002",
        target
    );
    assert(inspected.status == SequencerPatternPresetDomainStatus::OK);
    assert(std::strcmp(
        inspected.descriptor.metadata.semanticName,
        "Drum Pattern 0002"
    ) == 0);
    assert(inspected.descriptor.drumLaneCount == drum.kit.laneCount);

    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
    const uint32_t modifiedBefore = h.state.project.metadata.modifiedCounter;
    SequencerPatternPresetPreviewSession preview{};
    const auto previewed = h.presets.previewPreset(
        "pattern-preset-0002",
        target,
        inspected.descriptor.previewKey,
        preview
    );
    assert(previewed.ok());
    assert(previewed.status == SequencerPatternPresetDomainStatus::QUEUED);
    assert(previewed.activation == SequencerPatternPresetActivation::QUEUED);
    assert(preview.active());
    assert(h.state.sequencerTrackActivations.telemetry(0U).origin ==
           seq::SequencerTrackActivationOrigin::PRESET);
    assert(drum.pattern.stepEnabled(1U, 2U));
    assert(drum.pattern.lanes[1U].velocity[2U] == 111U);
    assert(std::strcmp(seq::drumLaneDisplayName(drum.kit.lanes[1U]), "My Snare") == 0);
    assert(seq::drumLaneDisplayColorIndex(drum.kit.lanes[1U]) == 6U);
    assert(drum.advancedRootSlot(1U, 2U) >= 0);
    assert(seq::graphView(h.state.sequencer.pattern) != nullptr);
    assert(h.state.sequencerHistory.undoCount() == undoBefore);
    assert(h.state.project.metadata.modifiedCounter == modifiedBefore);

    const auto applied = h.presets.confirmPresetPreview(preview);
    assert(applied.ok());
    assert(applied.status == SequencerPatternPresetDomainStatus::QUEUED);
    assert(applied.activation == SequencerPatternPresetActivation::QUEUED);
    assert(!preview.active());
    assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);
    assert(h.state.project.metadata.modifiedCounter == modifiedBefore + 1U);

    assert(h.state.undoSequencerHistory());
    assert(!drum.pattern.stepEnabled(1U, 2U));
    assert(drum.advancedRootSlot(1U, 2U) < 0);
    assert(std::strcmp(seq::drumLaneDisplayName(drum.kit.lanes[1U]), "My Snare") == 0);
    assert(h.state.sequencerTrackActivations.pendingTrackMask() == 0U);
    assert(h.state.sequencerTrackActivations.telemetry(0U).status ==
           seq::SequencerTrackActivationStatus::CANCELLED);

    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        assert(!h.state.redoSequencerHistory());
        test_support::sequencer_transaction::assertFailureConsumed(1U);
        assert(!drum.pattern.stepEnabled(1U, 2U));
        assert(!h.state.sequencer.pattern.graph);
        assert(h.state.sequencerHistory.redoCount() == 1U);
    }
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(2U);
        assert(h.state.redoSequencerHistory());
        test_support::sequencer_transaction::assertMaxPlusOneStillArmed(1U);
    }
    assert(drum.pattern.stepEnabled(1U, 2U));
    assert(drum.advancedRootSlot(1U, 2U) >= 0);
    assert(h.state.sequencer.pattern.graph);
    assert(!h.state.sequencerTracks.track(0U).graph);
    std::cout << "[MEASURE] Drum Graph Redo allocations=1\n";

    std::cout << "[PASS] Drum Pattern preset preserves kit and queues at loop\n";
}

void testDrumCompatibilityRejectsDifferentRoles() {
    Harness h;
    prepareDrumSource(h);
    assert(h.presets.savePreset(
        "pattern-preset-0003",
        h.presets.captureTarget(),
        false
    ).ok());

    auto& drum = h.state.sequencerTracks.drumTrack(0U);
    auto lane = drum.kit.lanes[1U];
    assert(seq::setDrumLaneRole(lane, seq::DrumLaneRole::CLAP));
    assert(drum.kit.setLane(1U, lane));
    h.state.sequencerTracks.publishDrumMutation(0U);
    h.state.markSequencerProjectMutated();

    const auto inspected = h.presets.inspectPreset(
        "pattern-preset-0003",
        h.presets.captureTarget()
    );
    assert(inspected.status == SequencerPatternPresetDomainStatus::INCOMPATIBLE);
    assert(inspected.descriptor.compatibility ==
           seq::SequencerPatternPresetCompatibility::INCOMPATIBLE_DRUM_KIT);

    std::cout << "[PASS] Drum Pattern preset rejects incompatible lane roles\n";
}

void testFactoryAndUserLibrarySources() {
    Harness h;
    setInstrumentPattern(h.state, 49U, false);
    assert(h.presets.savePreset(
        "pattern-preset-0001",
        h.presets.captureTarget(),
        false
    ).ok());

    SequencerPatternPresetDomainServices::Entry entries[15]{};
    auto listed = listSettled(
        h,
        entries,
        seq::SequencerPatternPresetSourceFilter::FACTORY,
        seq::SequencerTrackKind::INSTRUMENT
    );
    assert(listed.ok());
    assert(listed.count == 3U);
    assert(listed.totalCount == 3U);
    assert(entries[0].kind ==
           core::persistence::ProductDirectoryAssetEntryKind::FOLDER);
    assert(std::strcmp(entries[0].id, "@Bass") == 0);

    seq::SequencerPatternPresetLocation bass{};
    assert(bass.enter("Bass"));
    listed = listSettled(
        h,
        entries,
        seq::SequencerPatternPresetSourceFilter::FACTORY,
        seq::SequencerTrackKind::INSTRUMENT,
        nullptr,
        core::persistence::PatternPresetFilePageDirection::FORWARD,
        bass
    );
    assert(listed.ok());
    assert(listed.count == 1U);
    assert(listed.totalCount == 1U);
    assert(std::strcmp(entries[0].id, "factory-instrument-bass-offbeat") == 0);

    listed = listSettled(
        h,
        entries,
        seq::SequencerPatternPresetSourceFilter::ALL,
        seq::SequencerTrackKind::INSTRUMENT
    );
    assert(listed.ok());
    assert(listed.count == 5U);
    assert(listed.totalCount == 5U);
    assert(std::strcmp(entries[4].id, "pattern-preset-0001") == 0);

    char generatedId[32]{};
    for (uint8_t number = 2U; number <= 20U; ++number) {
        std::snprintf(
            generatedId,
            sizeof(generatedId),
            "pattern-preset-%04u",
            static_cast<unsigned>(number)
        );
        assert(h.presets.savePreset(
            generatedId,
            h.presets.captureTarget(),
            false
        ).ok());
    }
    listed = listSettled(
        h,
        entries,
        seq::SequencerPatternPresetSourceFilter::ALL,
        seq::SequencerTrackKind::INSTRUMENT
    );
    assert(listed.ok());
    assert(listed.count == 15U);
    assert(listed.totalCount == 24U);
    assert(listed.hasNext);
    assert(!listed.hasPrevious);
    assert(std::strcmp(entries[14].id, "pattern-preset-0011") == 0);

    listed = listSettled(
        h,
        entries,
        seq::SequencerPatternPresetSourceFilter::ALL,
        seq::SequencerTrackKind::INSTRUMENT,
        "pattern-preset-0011"
    );
    assert(listed.ok());
    assert(listed.count == 9U);
    assert(listed.hasPrevious);
    assert(!listed.hasNext);
    assert(std::strcmp(entries[0].id, "pattern-preset-0012") == 0);
    assert(std::strcmp(entries[8].id, "pattern-preset-0020") == 0);

    listed = listSettled(
        h,
        entries,
        seq::SequencerPatternPresetSourceFilter::ALL,
        seq::SequencerTrackKind::INSTRUMENT,
        "pattern-preset-0012",
        core::persistence::PatternPresetFilePageDirection::BACKWARD
    );
    assert(listed.ok());
    assert(listed.count == 15U);
    assert(!listed.hasPrevious);
    assert(listed.hasNext);
    assert(std::strcmp(entries[0].id, "factory-instrument-bass-offbeat") == 0);
    assert(std::strcmp(entries[4].id, "pattern-preset-0001") == 0);
    assert(std::strcmp(entries[14].id, "pattern-preset-0011") == 0);

    const auto target = h.presets.captureTarget();
    const auto inspected = h.presets.inspectPreset(
        "factory-instrument-rising",
        target
    );
    assert(inspected.status == SequencerPatternPresetDomainStatus::OK);
    assert(inspected.descriptor.source ==
           seq::SequencerPatternPresetSource::FACTORY);
    const auto applied = h.presets.applyPreset(
        "factory-instrument-rising",
        target,
        inspected.descriptor.previewKey
    );
    assert(applied.ok());
    assert(h.state.sequencer.pattern.note[0U] == 60U);
    assert(h.state.sequencer.pattern.note[7U] == 72U);

    const uint8_t undoBeforeCopy = h.state.sequencerHistory.undoCount();
    const auto copied = h.presets.copyFactoryPreset(
        "factory-instrument-rising"
    );
    assert(copied.ok());
    assert(copied.presetId[0] != '\0');
    assert(std::strcmp(
        copied.presetId,
        "factory-instrument-rising"
    ) != 0);
    assert(h.state.sequencerHistory.undoCount() == undoBeforeCopy);
    const auto copiedInspect = h.presets.inspectPreset(
        copied.presetId,
        h.presets.captureTarget()
    );
    assert(copiedInspect.status == SequencerPatternPresetDomainStatus::OK);
    assert(copiedInspect.descriptor.source ==
           seq::SequencerPatternPresetSource::USER);
    assert(std::strcmp(
        copiedInspect.descriptor.metadata.semanticName,
        "Rising sequence"
    ) == 0);

    assert(h.presets.savePreset(
        "factory-instrument-rising",
        h.presets.captureTarget(),
        true
    ).status == SequencerPatternPresetDomainStatus::READ_ONLY);
    assert(h.presets.renamePreset(
        "factory-instrument-rising",
        "Rising sequence",
        "Changed"
    ).status == SequencerPatternPresetDomainStatus::READ_ONLY);
    assert(h.presets.deletePreset(
        "factory-instrument-rising",
        "Rising sequence"
    ).status == SequencerPatternPresetDomainStatus::READ_ONLY);

    std::cout << "[PASS] Factory/User sources merge and Factory stays read-only\n";
}

void testDrumPatternLibraryEntryBelongsToPatternNotLane() {
    Harness h;
    auto& sequencer = h.state.sequencer;
    core::handler::SequencerPatternPresetLibraryAdapter adapter(
        sequencer,
        h.presets
    );
    const auto operations = adapter.operations();

    sequencer.presetLibrary.open(
        seq::SequencerPresetLibraryMode::LOAD,
        seq::SequencerPresetLibraryKind::PATTERN
    );
    sequencer.drumSequencer.selector = seq::DrumSequencerSelector::LANE_EDITOR;
    sequencer.drumSequencer.laneEditor.active = true;
    assert(!operations.beginSession(operations.context));

    sequencer.drumSequencer.laneEditor.active = false;
    sequencer.drumSequencer.selector =
        seq::DrumSequencerSelector::PATTERN_DEFAULTS;
    assert(operations.beginSession(operations.context));
    assert(sequencer.presetLibrary.pattern().target.valid);

    std::cout << "[PASS] Drum Pattern library entry is Pattern-owned\n";
}

}  // namespace

int main() {
    testInstrumentPreviewAllocationFailuresAndNoFailCancel();
    testInstrumentLifecycleAndSingleUndo();
    testDrumPreviewAllocationFailuresAndNoFailCancel();
    testDrumApplyPreservesKitAndQueuesAtLoop();
    testDrumCompatibilityRejectsDifferentRoles();
    testFactoryAndUserLibrarySources();
    testDrumPatternLibraryEntryBelongsToPatternNotLane();
    std::cout << "\nAll SequencerPatternPresetDomainServices tests passed.\n";
    return 0;
}

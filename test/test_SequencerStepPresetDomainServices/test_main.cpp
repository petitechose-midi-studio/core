#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <cstring>
#include <array>
#include <string>
#include <vector>

#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>
#include <oc/interface/IFileSystem.hpp>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/handler/sequencer/SequencerStepPresetDomainServices.hpp"
#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/persistence/SequencerGraphAssetCodec.hpp"
#include "../../src/persistence/StepPresetFileStore.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/project/ProjectTrackDomainOps.hpp"
#include "../../src/state/sequencer/SequencerGraphAsset.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerHistory.hpp"
#include "../../src/state/sequencer/SequencerTrackBankOps.hpp"
#include "../support/CoreStorages.hpp"

#if !defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
#error "This test requires native EXTMEM failure injection"
#endif

namespace {

namespace asset_codec =
    core::persistence::sequencer_graph_asset_codec;

using core::handler::SequencerStepPresetActionResult;
using core::handler::SequencerStepPresetActivation;
using core::handler::SequencerStepPresetDomainServices;
using core::handler::SequencerStepPresetStatus;
using core::persistence::ProductDirectoryCatalog;
using core::persistence::ProductFileService;
using core::persistence::StepPresetFileListEntry;
using core::persistence::StepPresetFileStore;
using core::state::sequencer::SequencerGraphAssetReport;
using core::state::sequencer::SequencerHistoryTrackBankSnapshot;
using core::state::sequencer::SequencerState;
using core::state::sequencer::SequencerStepGraphPreset;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
        "midi-studio-core-step-preset-domain-services-test";
}

void resetTestRoot() {
    std::error_code ec;
    std::filesystem::remove_all(testRoot(), ec);
}

struct FaultInjectingFileSystem : oc::interface::IFileSystem {
    explicit FaultInjectingFileSystem(const char* rootPath)
        : delegate(rootPath) {}

    oc::type::Result<void> init() override { return delegate.init(); }
    bool available() const override { return delegate.available(); }
    oc::type::Result<oc::interface::FileInfo> stat(const char* path) override {
        return delegate.stat(path);
    }
    oc::type::Result<void> list(
        const char* path,
        oc::interface::DirectoryEntryVisitor visitor,
        void* context
    ) override {
        return delegate.list(path, visitor, context);
    }
    oc::type::Result<void> createDirectory(const char* path) override {
        return delegate.createDirectory(path);
    }
    oc::type::Result<void> remove(
        const char* path,
        oc::interface::RemoveMode mode =
            oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
    ) override {
        return delegate.remove(path, mode);
    }
    oc::type::Result<void> rename(
        const char* fromPath,
        const char* toPath
    ) override {
        return delegate.rename(fromPath, toPath);
    }
    oc::type::Result<size_t> read(
        const char* path,
        uint32_t offset,
        uint8_t* buffer,
        size_t size
    ) override {
        auto result = delegate.read(path, offset, buffer, size);
        if (result && mutateAfterNextPresetRead && !mutationDone && path != nullptr &&
            std::strstr(path, mutationPathFragment.c_str()) != nullptr) {
            constexpr uint32_t semanticOffset =
                asset_codec::BASE_HEADER_SIZE + 4U +
                SequencerStepGraphPreset::TECHNICAL_ID_SIZE;
            const uint8_t replacement = 'T';
            const auto changed = delegate.write(
                path,
                semanticOffset,
                &replacement,
                1
            );
            assert(changed && changed.value() == 1);
            assert(delegate.flush(path));
            mutationDone = true;
        }
        return result;
    }
    oc::type::Result<size_t> write(
        const char* path,
        uint32_t offset,
        const uint8_t* data,
        size_t size
    ) override {
        return delegate.write(path, offset, data, size);
    }
    oc::type::Result<void> flush(const char* path) override {
        return delegate.flush(path);
    }
    oc::type::Result<void> beginWrite(
        const char* path,
        uint32_t expectedSize
    ) override {
        return delegate.beginWrite(path, expectedSize);
    }
    oc::type::Result<size_t> appendWrite(
        const uint8_t* data,
        size_t size
    ) override {
        return delegate.appendWrite(data, size);
    }
    oc::type::Result<void> finishWrite() override {
        return delegate.finishWrite();
    }
    void abortWrite() override { delegate.abortWrite(); }

    void mutatePresetAfterNextRead(const char* presetId) {
        mutationPathFragment = std::string("/") + presetId + ".mssp";
        mutateAfterNextPresetRead = true;
        mutationDone = false;
    }

    oc::impl::HostFileSystem delegate;
    std::string mutationPathFragment;
    bool mutateAfterNextPresetRead = false;
    bool mutationDone = false;
};

struct Harness {
    test_support::CoreStorages storages;
    core::state::CoreState state;
    FaultInjectingFileSystem filesystem;
    ProductFileService files;
    ProductDirectoryCatalog catalog;
    SequencerStepPresetDomainServices presets;
    uint32_t nowMs = 0U;

    Harness()
        : state(
              storages.settings
          )
        , filesystem(testRoot().string().c_str())
        , files(filesystem)
        , catalog(files)
        , presets(
              SequencerStepPresetDomainServices::fromCoreState(
                  state,
                  files,
                  catalog
              )
          ) {
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

std::vector<uint8_t> encodePreset(
    const char* technicalId,
    const char* semanticName,
    uint8_t note = 67,
    SequencerStepGraphPreset::ScalePolicy scalePolicy =
        SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
) {
    SequencerState source;
    source.pattern.setContentLength(8);
    source.pattern.setEnabled(2, true);
    assert(source.setStepDataAt(2, note, 96, 155, -3, 84));
    const auto sequence = core::state::sequencer::createMicroSequence(
        source.pattern,
        core::state::sequencer::rootStepNodeId(2),
        2
    );
    assert(sequence.ok);

    const oc::note::sequencer::StepSequencerScaleSettings sourceScale =
        scalePolicy == SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
        ? oc::note::sequencer::StepSequencerScaleSettings{
              .root = 5,
              .type =
                  oc::note::sequencer::StepSequencerScaleType::HarmonicMinor,
              .mode = oc::note::sequencer::
                  StepSequencerScaleConstraintMode::ConstrainNearest,
          }
        : oc::note::sequencer::StepSequencerScaleSettings{};
    SequencerStepGraphPreset preset{};
    SequencerGraphAssetReport report{};
    assert(core::state::sequencer::captureStepGraphPreset(
        source,
        2,
        sourceScale,
        preset,
        &report
    ));
    assert(core::state::sequencer::setStepGraphPresetMetadata(
        preset,
        technicalId,
        semanticName,
        scalePolicy,
        sourceScale
    ));

    std::vector<uint8_t> bytes(
        asset_codec::MAX_ENCODED_SIZE
    );
    const auto encoded = asset_codec::encode(
        preset,
        bytes.data(),
        static_cast<uint16_t>(bytes.size())
    );
    assert(encoded.ok());
    bytes.resize(encoded.bytesWritten);
    return bytes;
}

std::vector<uint8_t> encodeRandomCyclePreset(
    const char* technicalId,
    const char* semanticName
) {
    SequencerState source;
    source.pattern.setContentLength(8);
    source.pattern.setEnabled(2, true);
    assert(source.setStepDataAt(2, 67, 96, 155, -3, 84));
    const auto root = core::state::sequencer::rootStepNodeId(2);
    assert(core::state::sequencer::setNodeLocalVariationRange(
        source.pattern,
        root,
        core::state::sequencer::StepProperty::NOTE,
        7
    ));
    const auto sequence = core::state::sequencer::createMicroSequence(
        source.pattern,
        root,
        2
    );
    assert(sequence.ok);
    const auto* graph = core::state::sequencer::graphView(source.pattern);
    assert(graph != nullptr);
    const auto* childSequence = graph->sequence(sequence.id);
    assert(childSequence != nullptr);
    const auto child = static_cast<uint16_t>(childSequence->firstStepNode + 1U);
    assert(core::state::sequencer::setNodeNoteOffset(source.pattern, child, 5));
    const auto cycle = core::state::sequencer::createCycleStateSet(
        source.pattern,
        child,
        3
    );
    assert(cycle.ok);
    graph = core::state::sequencer::graphView(source.pattern);
    assert(graph != nullptr);
    const auto* cycleSet = graph->cycleSet(cycle.id);
    assert(cycleSet != nullptr && cycleSet->length == 3);
    assert(core::state::sequencer::setNodeNoteOffset(
        source.pattern,
        static_cast<uint16_t>(cycleSet->firstStateNode + 1U),
        -4
    ));
    assert(core::state::sequencer::setNodeNoteOffset(
        source.pattern,
        static_cast<uint16_t>(cycleSet->firstStateNode + 2U),
        9
    ));

    SequencerStepGraphPreset preset{};
    SequencerGraphAssetReport report{};
    assert(core::state::sequencer::captureStepGraphPreset(
        source,
        2,
        {},
        preset,
        &report
    ));
    assert(core::state::sequencer::setStepGraphPresetMetadata(
        preset,
        technicalId,
        semanticName,
        SequencerStepGraphPreset::ScalePolicy::CHROMATIC,
        {}
    ));
    std::vector<uint8_t> bytes(
        asset_codec::MAX_ENCODED_SIZE
    );
    const auto encoded = asset_codec::encode(
        preset,
        bytes.data(),
        static_cast<uint16_t>(bytes.size())
    );
    assert(encoded.ok());
    bytes.resize(encoded.bytesWritten);
    return bytes;
}

std::vector<uint8_t> asPreviousVersion(std::vector<uint8_t> bytes) {
    bytes[4] = static_cast<uint8_t>(
        SequencerStepGraphPreset::CURRENT_FORMAT_VERSION - 1U
    );
    return bytes;
}

void saveBytes(
    ProductFileService& files,
    ProductDirectoryCatalog& catalog,
    const char* id,
    const std::vector<uint8_t>& bytes
) {
    StepPresetFileStore store(files, catalog);
    assert(store.save(
        id,
        bytes.data(),
        static_cast<uint16_t>(bytes.size())
    ));
}

std::vector<uint8_t> loadBytes(
    ProductFileService& files,
    ProductDirectoryCatalog& catalog,
    const char* id
) {
    StepPresetFileStore store(files, catalog);
    std::vector<uint8_t> bytes(StepPresetFileStore::MAX_FILE_SIZE);
    uint16_t size = 0;
    assert(store.load(
        id,
        bytes.data(),
        static_cast<uint16_t>(bytes.size()),
        size
    ));
    bytes.resize(size);
    return bytes;
}

core::handler::SequencerStepPresetListResult listPresetsSettled(
    Harness& h,
    StepPresetFileListEntry* entries,
    uint8_t capacity
) {
    auto listed = h.presets.listPresetsPage(
        entries,
        capacity,
        nullptr,
        core::persistence::StepPresetFilePageDirection::FORWARD
    );
    while (listed.status == SequencerStepPresetStatus::QUEUED) {
        h.advanceCatalog();
        listed = h.presets.listPresetsPage(
            entries,
            capacity,
            nullptr,
            core::persistence::StepPresetFilePageDirection::FORWARD
        );
    }
    return listed;
}

void prepareTarget(Harness& h, uint8_t step = 5) {
    h.state.sequencer.pattern.setContentLength(8);
    assert(core::state::project::setProjectTrackMidiChannel(
        h.state.projectTracks,
        0,
        9
    ).changed());
    h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] = 9;
    h.state.sequencer.pattern.setEnabled(step, false);
    assert(h.state.sequencer.setStepDataAt(step, 41, 12, 40, 4, 100));
    h.state.sequencer.stepEdit.stepIndex.set(step);
    h.state.sequencer.focusedStep.set(step);
    h.state.sequencer.page.set(0);
    assert(core::state::sequencer::initializeTrackBankFromActive(
        h.state.sequencerTracks,
        h.state.sequencer
    ));
    h.state.project.metadata.modifiedCounter = 42;
    h.state.project.metadata.dirty = false;
}

struct LiveInvariant {
    core::app::ExtmemUniquePtr<SequencerHistoryTrackBankSnapshot> musical;
    uint32_t projectRevision = 0;
    bool projectDirty = false;
    uint8_t undoCount = 0;
    uint8_t redoCount = 0;
    size_t retainedHistoryBytes = 0;
    uint16_t pendingActivationMask = 0;
    uint32_t activationTelemetryRevision = 0;
    std::array<
        core::state::sequencer::SequencerTrackActivationTelemetry,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    > activationTelemetry{};
    bool pendingCoalescing = false;
    uint8_t page = 0;
    uint8_t focusedStep = 0;
    uint32_t editorGraphRevision = 0;
    std::array<
        uint32_t,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    > bankGraphRevisions{};
    core::state::sequencer::SequencerContentViewKind contentKind =
        core::state::sequencer::SequencerContentViewKind::ROOT;
    uint16_t ownerNodeId = 0;
    uint16_t sequenceId = 0;
    uint16_t cycleSetId = 0;
};

LiveInvariant captureInvariant(core::state::CoreState& state) {
    LiveInvariant snapshot{};
    snapshot.musical =
        core::app::makeExtmemUnique<SequencerHistoryTrackBankSnapshot>();
    assert(snapshot.musical);
    assert(core::state::sequencer::captureHistorySnapshot(
        state.sequencerTracks,
        state.sequencer,
        *snapshot.musical
    ));
    snapshot.projectRevision = state.project.metadata.modifiedCounter;
    snapshot.projectDirty = state.project.metadata.dirty;
    snapshot.undoCount = state.sequencerHistory.undoCount();
    snapshot.redoCount = state.sequencerHistory.redoCount();
    snapshot.retainedHistoryBytes = state.sequencerHistory.retainedBytes();
    snapshot.pendingActivationMask =
        state.sequencerTrackActivations.pendingTrackMask();
    snapshot.activationTelemetryRevision =
        state.sequencerTrackActivations.telemetryRevision().get();
    for (uint8_t track = 0;
         track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        snapshot.activationTelemetry[track] =
            state.sequencerTrackActivations.telemetry(track);
        snapshot.bankGraphRevisions[track] =
            state.sequencerTracks.track(track).graphRevision.get();
    }
    snapshot.pendingCoalescing =
        state.hasPendingSequencerPatternHistoryCoalescing();
    snapshot.page = state.sequencer.page.get();
    snapshot.focusedStep = state.sequencer.focusedStep.get();
    snapshot.editorGraphRevision = state.sequencer.pattern.graphRevision.get();
    snapshot.contentKind = state.sequencer.contentView.kind.get();
    snapshot.ownerNodeId = state.sequencer.contentView.ownerNodeId.get();
    snapshot.sequenceId = state.sequencer.contentView.sequenceId.get();
    snapshot.cycleSetId = state.sequencer.contentView.cycleSetId.get();
    return snapshot;
}

void assertInvariantUnchanged(
    core::state::CoreState& state,
    const LiveInvariant& before
) {
    auto after = core::app::makeExtmemUnique<
        SequencerHistoryTrackBankSnapshot
    >();
    assert(after);
    assert(core::state::sequencer::captureHistorySnapshot(
        state.sequencerTracks,
        state.sequencer,
        *after
    ));
    assert(core::state::sequencer::sameMusicalHistorySnapshot(
        *before.musical,
        *after
    ));
    assert(state.project.metadata.modifiedCounter == before.projectRevision);
    assert(state.project.metadata.dirty == before.projectDirty);
    assert(state.sequencerHistory.undoCount() == before.undoCount);
    assert(state.sequencerHistory.redoCount() == before.redoCount);
    assert(state.sequencerHistory.retainedBytes() == before.retainedHistoryBytes);
    assert(
        state.sequencerTrackActivations.pendingTrackMask() ==
        before.pendingActivationMask
    );
    assert(
        state.sequencerTrackActivations.telemetryRevision().get() ==
        before.activationTelemetryRevision
    );
    for (uint8_t track = 0;
         track < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++track) {
        const auto telemetry = state.sequencerTrackActivations.telemetry(track);
        assert(telemetry.status == before.activationTelemetry[track].status);
        assert(telemetry.generation == before.activationTelemetry[track].generation);
        assert(
            state.sequencerTracks.track(track).graphRevision.get() ==
            before.bankGraphRevisions[track]
        );
    }
    assert(
        state.hasPendingSequencerPatternHistoryCoalescing() ==
        before.pendingCoalescing
    );
    assert(state.sequencer.page.get() == before.page);
    assert(state.sequencer.focusedStep.get() == before.focusedStep);
    assert(
        state.sequencer.pattern.graphRevision.get() ==
        before.editorGraphRevision
    );
    assert(state.sequencer.contentView.kind.get() == before.contentKind);
    assert(state.sequencer.contentView.ownerNodeId.get() == before.ownerNodeId);
    assert(state.sequencer.contentView.sequenceId.get() == before.sequenceId);
    assert(state.sequencer.contentView.cycleSetId.get() == before.cycleSetId);
}

void applyPendingRuntimeGeneration(core::state::CoreState& state, uint8_t track) {
    auto& queue = state.sequencerTrackActivations;
    const auto publication = queue.captureRuntimePublication();
    queue.applyRuntimePublication(publication);
    const auto realtime = queue.realtimeView(track);
    assert(
        realtime.disposition == core::state::sequencer::
            SequencerTrackActivationRealtimeView::Disposition::STAGED
    );
    assert(queue.markAppliedFromRealtime(track, realtime.generation));
    assert(queue.publishRealtimeTelemetry());
}

void assertManagerRefusalLeavesAssetUnchanged(
    Harness& h,
    const char* id,
    const char* expectedName,
    SequencerStepPresetStatus expectedStatus
) {
    const auto before = loadBytes(h.files, h.catalog, id);
    const auto renamed = h.presets.renamePreset(id, expectedName, "New Name");
    assert(renamed.status == expectedStatus);
    assert(loadBytes(h.files, h.catalog, id) == before);
    const auto removed = h.presets.deletePreset(id, expectedName);
    assert(removed.status == expectedStatus);
    assert(loadBytes(h.files, h.catalog, id) == before);
}

void test_manager_rename_reorders_and_delete_is_guarded() {
    Harness h;
    saveBytes(h.files, h.catalog, "preset-z", encodePreset("preset-z", "Zulu"));
    saveBytes(h.files, h.catalog, "preset-a", encodePreset("preset-a", "Alpha"));
    saveBytes(h.files, h.catalog, "preset-b", encodePreset("preset-b", "Bravo"));

    StepPresetFileListEntry entries[4]{};
    auto listed = listPresetsSettled(h, entries, 4);
    assert(listed.ok() && listed.count == 3);
    assert(std::strcmp(entries[0].id, "preset-a") == 0);
    assert(std::strcmp(entries[1].id, "preset-b") == 0);
    assert(std::strcmp(entries[2].id, "preset-z") == 0);

    const auto before = loadBytes(h.files, h.catalog, "preset-z");
    const auto staleRename = h.presets.renamePreset(
        "preset-z",
        "Wrong Name",
        "Able"
    );
    assert(staleRename.status == SequencerStepPresetStatus::STALE_TARGET);
    assert(loadBytes(h.files, h.catalog, "preset-z") == before);

    const auto renamed = h.presets.renamePreset("preset-z", "Zulu", "Able");
    assert(renamed.ok());
    listed = listPresetsSettled(h, entries, 4);
    assert(listed.ok() && listed.count == 3);
    assert(std::strcmp(entries[0].id, "preset-z") == 0);
    assert(std::strcmp(entries[0].semanticName, "Able") == 0);
    assert(std::strcmp(entries[1].id, "preset-a") == 0);
    assert(std::strcmp(entries[2].id, "preset-b") == 0);

    const auto renamedBytes = loadBytes(h.files, h.catalog, "preset-z");
    assert(renamedBytes.size() == before.size());
    constexpr size_t semanticOffset =
        asset_codec::BASE_HEADER_SIZE + 4U +
        SequencerStepGraphPreset::TECHNICAL_ID_SIZE;
    constexpr size_t semanticEnd =
        semanticOffset + SequencerStepGraphPreset::SEMANTIC_NAME_SIZE;
    for (size_t i = 0; i < before.size(); ++i) {
        if (i >= semanticOffset && i < semanticEnd) continue;
        assert(renamedBytes[i] == before[i]);
    }

    const auto staleDelete = h.presets.deletePreset("preset-z", "Zulu");
    assert(staleDelete.status == SequencerStepPresetStatus::STALE_TARGET);
    assert(loadBytes(h.files, h.catalog, "preset-z") == renamedBytes);
    const auto removed = h.presets.deletePreset("preset-z", "Able");
    assert(removed.ok());
    StepPresetFileStore store(h.files, h.catalog);
    const auto exists = store.exists("preset-z");
    assert(exists && !exists.value());

    std::cout << "[PASS] test_manager_rename_reorders_and_delete_is_guarded\n";
}

void test_manager_refuses_previous_future_and_partial_without_mutation() {
    Harness h;

    auto previous = asPreviousVersion(encodePreset("previous", "Previous"));
    saveBytes(h.files, h.catalog, "previous", previous);
    assertManagerRefusalLeavesAssetUnchanged(
        h,
        "previous",
        "Previous",
        SequencerStepPresetStatus::UNSUPPORTED_VERSION
    );

    auto future = encodePreset("future", "Future");
    future[4] = static_cast<uint8_t>(
        SequencerStepGraphPreset::CURRENT_FORMAT_VERSION + 1U
    );
    saveBytes(h.files, h.catalog, "future", future);
    assertManagerRefusalLeavesAssetUnchanged(
        h,
        "future",
        "Future",
        SequencerStepPresetStatus::UNSUPPORTED_VERSION
    );

    auto partial = encodePreset("partial", "Partial");
    partial.pop_back();
    saveBytes(h.files, h.catalog, "partial", partial);
    assertManagerRefusalLeavesAssetUnchanged(
        h,
        "partial",
        "Partial",
        SequencerStepPresetStatus::CORRUPT
    );

    std::cout << "[PASS] test_manager_refuses_previous_future_and_partial_without_mutation\n";
}

void test_step_presets_require_one_matching_pattern_pitch_context() {
    using Compatibility =
        core::state::sequencer::SequencerStepPresetCompatibility;
    using PitchMode =
        core::state::sequencer::SequencerPitchEditMode;

    Harness h;
    saveBytes(
        h.files,
        h.catalog,
        "chromatic",
        encodePreset(
            "chromatic",
            "Chromatic",
            67,
            SequencerStepGraphPreset::ScalePolicy::CHROMATIC
        )
    );
    saveBytes(
        h.files,
        h.catalog,
        "relative",
        encodePreset(
            "relative",
            "Relative",
            67,
            SequencerStepGraphPreset::ScalePolicy::SCALE_RELATIVE
        )
    );

    const auto followTarget = h.presets.captureTarget();
    const auto chromaticInFollow = h.presets.inspectPreset(
        "chromatic",
        followTarget,
        0,
        1
    );
    assert(
        chromaticInFollow.status ==
        SequencerStepPresetStatus::INCOMPATIBLE
    );
    assert(
        chromaticInFollow.descriptor.compatibility ==
        Compatibility::BLOCKED_PITCH_CONTEXT
    );
    assert(
        std::strcmp(
            chromaticInFollow.descriptor.adaptationSummary,
            "Requires Chromatic"
        ) == 0
    );

    const auto relativeInFollow = h.presets.inspectPreset(
        "relative",
        followTarget,
        0,
        2
    );
    assert(relativeInFollow.status == SequencerStepPresetStatus::OK);
    assert(
        relativeInFollow.descriptor.compatibility ==
            Compatibility::READY ||
        relativeInFollow.descriptor.compatibility ==
            Compatibility::WARNING_ADAPTED
    );

    assert(h.state.sequencer.setPitchEditMode(PitchMode::CHROMATIC));
    const auto chromaticTarget = h.presets.captureTarget();
    const auto chromaticInChromatic = h.presets.inspectPreset(
        "chromatic",
        chromaticTarget,
        0,
        3
    );
    assert(chromaticInChromatic.status == SequencerStepPresetStatus::OK);
    assert(
        chromaticInChromatic.descriptor.compatibility ==
        Compatibility::READY
    );

    const auto relativeInChromatic = h.presets.inspectPreset(
        "relative",
        chromaticTarget,
        0,
        4
    );
    assert(
        relativeInChromatic.status ==
        SequencerStepPresetStatus::INCOMPATIBLE
    );
    assert(
        relativeInChromatic.descriptor.compatibility ==
        Compatibility::BLOCKED_PITCH_CONTEXT
    );
    assert(
        std::strcmp(
            relativeInChromatic.descriptor.adaptationSummary,
            "Requires Follow Scale"
        ) == 0
    );

    std::cout
        << "[PASS] Step presets require one matching Pitch Context\n";
}

void test_apply_preflight_failures_leave_every_live_domain_unchanged() {
    Harness h;
    prepareTarget(h);
    saveBytes(
        h.files,
        h.catalog,
        "apply-source",
        encodePreset("apply-source", "Step Source")
    );

    const auto target = h.presets.captureTarget();
    const auto inspected = h.presets.inspectPreset("apply-source", target, 0, 1);
    assert(inspected.inspected());
    auto wrongPreview = inspected.descriptor.previewKey;
    wrongPreview.assetHash ^= 0x100U;
    const auto beforeWrongPreview = captureInvariant(h.state);
    const auto stale = h.presets.applyPreset(
        "apply-source",
        target,
        wrongPreview
    );
    assert(stale.status == SequencerStepPresetStatus::STALE_TARGET);
    assertInvariantUnchanged(h.state, beforeWrongPreview);

    assert(core::state::sequencer::sequencerHistoryOpenAccepted(
        h.state.beginOrContinueSequencerPatternHistoryCoalescing(
        target.stepIndex,
        core::state::sequencer::StepProperty::VELOCITY,
        100,
        core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly)));
    h.state.sequencer.pattern.velocity[target.stepIndex] = 77;
    assert(h.state.sealSequencerPatternHistoryCoalescing(true));
    const auto beforePendingEdit = captureInvariant(h.state);
    const auto pendingRejected = h.presets.applyPreset(
        "apply-source",
        target,
        inspected.descriptor.previewKey
    );
    assert(pendingRejected.status == SequencerStepPresetStatus::STALE_TARGET);
    assertInvariantUnchanged(h.state, beforePendingEdit);

    std::cout << "[PASS] test_apply_preflight_failures_leave_every_live_domain_unchanged\n";
}

void test_apply_allocation_failure_matrix_is_atomic_and_bounded() {
    constexpr std::size_t APPLY_ALLOCATION_ATTEMPTS = 8U;
    static_assert(APPLY_ALLOCATION_ATTEMPTS <= 12U,
                  "Step preset apply exceeded its frozen allocation-attempt budget");

    for (std::size_t ordinal = 1U; ordinal <= APPLY_ALLOCATION_ATTEMPTS; ++ordinal) {
        Harness h;
        prepareTarget(h);
        saveBytes(h.files, h.catalog, "apply-allocation-source",
                  encodePreset("apply-allocation-source", "Allocation Source"));
        const auto target = h.presets.captureTarget();
        const auto inspected = h.presets.inspectPreset("apply-allocation-source", target, 0U, 1U);
        assert(inspected.inspected());
        const auto before = captureInvariant(h.state);

        {
            core::app::testing::ScopedExtmemAllocationFailure failure(ordinal);
            const auto result = h.presets.applyPreset("apply-allocation-source", target,
                                                      inspected.descriptor.previewKey);
            if (ordinal == 7U) {
                // Graph compaction owns an optional scratch allocation. Its
                // failure deliberately falls back to the uncompacted graph
                // and must not reject an otherwise admitted musical edit.
                assert(result.ok());
                assert(result.activation == SequencerStepPresetActivation::APPLIED);
            } else {
                assert(result.status == SequencerStepPresetStatus::ALLOCATION_UNAVAILABLE);
            }
            assert(core::app::testing::extmemAllocationAttempt == ordinal);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 0U);
        }

        if (ordinal == 7U) {
            assert(h.state.sequencerHistory.undoCount() == 1U);
        } else {
            assertInvariantUnchanged(h.state, before);
        }
    }

    Harness h;
    prepareTarget(h);
    saveBytes(h.files, h.catalog, "apply-max-plus-one", encodePreset("apply-max-plus-one", "Max Plus One"));
    const auto target = h.presets.captureTarget();
    const auto inspected = h.presets.inspectPreset("apply-max-plus-one", target, 0U, 1U);
    assert(inspected.inspected());
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(APPLY_ALLOCATION_ATTEMPTS + 1U);
        const auto result =
            h.presets.applyPreset("apply-max-plus-one", target, inspected.descriptor.previewKey);
        assert(result.ok());
        assert(core::app::testing::extmemAllocationAttempt == APPLY_ALLOCATION_ATTEMPTS);
        assert(core::app::testing::extmemAllocationFailureOrdinal ==
               APPLY_ALLOCATION_ATTEMPTS + 1U);
    }

    std::cout << "[PASS] Step preset Apply freezes 8 allocation outcomes and max+1\n";
}

void test_random_cycle_preview_is_stable_and_generation_admission_is_exact() {
    Harness h;
    prepareTarget(h);
    saveBytes(
        h.files,
        h.catalog,
        "random-cycle",
        encodeRandomCyclePreset("random-cycle", "Random Cycle")
    );
    const auto target = h.presets.captureTarget();
    const auto before = captureInvariant(h.state);

    for (uint8_t stateIndex = 0; stateIndex < 3; ++stateIndex) {
        const uint32_t firstGeneration = static_cast<uint32_t>(10U + stateIndex);
        const uint32_t secondGeneration = static_cast<uint32_t>(20U + stateIndex);
        const auto first = h.presets.inspectPreset(
            "random-cycle",
            target,
            stateIndex,
            firstGeneration
        );
        const auto second = h.presets.inspectPreset(
            "random-cycle",
            target,
            stateIndex,
            secondGeneration
        );
        assert(first.inspected() && second.inspected());
        assert((first.descriptor.contentFlags &
                core::state::sequencer::STEP_PRESET_CONTENT_CYCLE) != 0);
        assert((first.descriptor.contentFlags &
                core::state::sequencer::STEP_PRESET_CONTENT_RANDOM) != 0);
        assert(first.descriptor.previewStateCount == 3);
        assert(first.descriptor.previewStateIndex == stateIndex);
        assert(first.descriptor.previewKey == second.descriptor.previewKey);
        assert(first.descriptor.previewNote == second.descriptor.previewNote);
        assert(std::strcmp(
            first.descriptor.previewSummary,
            second.descriptor.previewSummary
        ) == 0);
        assert(first.descriptor.generation == firstGeneration);
        assert(second.descriptor.generation == secondGeneration);

        const auto expectedRequest = core::state::sequencer::
            SequencerStepPresetPreviewKey{
                .assetHash = core::state::sequencer::sequencerStepPresetIdHash(
                    "random-cycle"
                ),
                .targetHash = first.descriptor.previewKey.targetHash,
                .projectRevision = first.descriptor.previewKey.projectRevision,
                .stateIndex = stateIndex,
            };
        assert(core::state::sequencer::sequencerStepPresetInspectionMatches(
            firstGeneration,
            expectedRequest,
            first.descriptor
        ));
        assert(!core::state::sequencer::sequencerStepPresetInspectionMatches(
            secondGeneration,
            expectedRequest,
            first.descriptor
        ));
        auto staleRequest = expectedRequest;
        staleRequest.stateIndex = static_cast<uint8_t>((stateIndex + 1U) % 3U);
        assert(!core::state::sequencer::sequencerStepPresetInspectionMatches(
            firstGeneration,
            staleRequest,
            first.descriptor
        ));
    }
    assertInvariantUnchanged(h.state, before);

    std::cout
        << "[PASS] test_random_cycle_preview_is_stable_and_generation_admission_is_exact\n";
}

void test_apply_second_read_payload_change_is_stale_and_non_mutating() {
    Harness h;
    prepareTarget(h);
    saveBytes(
        h.files,
        h.catalog,
        "race-source",
        encodePreset("race-source", "Step Source")
    );
    const auto target = h.presets.captureTarget();
    const auto inspected = h.presets.inspectPreset("race-source", target, 0, 1);
    assert(inspected.inspected());

    h.filesystem.mutatePresetAfterNextRead("race-source");
    const auto before = captureInvariant(h.state);
    const auto result = h.presets.applyPreset(
        "race-source",
        target,
        inspected.descriptor.previewKey
    );
    assert(h.filesystem.mutationDone);
    assert(result.status == SequencerStepPresetStatus::STALE_TARGET);
    assertInvariantUnchanged(h.state, before);

    SequencerStepGraphPreset mutated{};
    SequencerGraphAssetReport report{};
    const auto bytes = loadBytes(h.files, h.catalog, "race-source");
    assert(asset_codec::decode(
        bytes.data(),
        static_cast<uint16_t>(bytes.size()),
        mutated,
        &report
    ));
    assert(std::strcmp(mutated.technicalId, "race-source") == 0);
    assert(std::strcmp(mutated.semanticName, "Ttep Source") == 0);

    std::cout << "[PASS] test_apply_second_read_payload_change_is_stale_and_non_mutating\n";
}

void test_apply_activation_conflict_leaves_preexisting_queue_and_state_unchanged() {
    Harness h;
    prepareTarget(h);
    saveBytes(
        h.files,
        h.catalog,
        "queued-source",
        encodePreset("queued-source", "Queued Source")
    );
    const auto target = h.presets.captureTarget();
    const auto inspected = h.presets.inspectPreset("queued-source", target, 0, 1);
    assert(inspected.inspected());

    const uint16_t targetBit = static_cast<uint16_t>(1U << target.trackIndex);
    core::state::sequencer::SequencerTrackActivationBatch occupied{};
    assert(h.state.sequencerTrackActivations.prepare(
        targetBit,
        core::state::project::audibleMask(
            h.state.projectTracks,
            h.state.sequencerTracks.currentEnabledMask()
        ),
        false,
        occupied
    ));
    assert(h.state.sequencerTrackActivations.armPrepared(occupied));
    h.state.sequencerTrackActivations.publishPrepared(occupied);

    const auto before = captureInvariant(h.state);
    const auto result = h.presets.applyPreset(
        "queued-source",
        target,
        inspected.descriptor.previewKey
    );
    assert(result.status == SequencerStepPresetStatus::STALE_TARGET);
    assertInvariantUnchanged(h.state, before);

    std::cout << "[PASS] test_apply_activation_conflict_leaves_preexisting_queue_and_state_unchanged\n";
}

void test_apply_future_and_partial_assets_do_not_mutate_live_state() {
    Harness h;
    prepareTarget(h);
    const auto target = h.presets.captureTarget();

    auto future = encodePreset("apply-future", "Future");
    future[4] = static_cast<uint8_t>(
        SequencerStepGraphPreset::CURRENT_FORMAT_VERSION + 1U
    );
    saveBytes(h.files, h.catalog, "apply-future", future);
    auto inspected = h.presets.inspectPreset("apply-future", target, 0, 1);
    auto before = captureInvariant(h.state);
    auto result = h.presets.applyPreset(
        "apply-future",
        target,
        inspected.descriptor.previewKey
    );
    assert(result.status == SequencerStepPresetStatus::UNSUPPORTED_VERSION);
    assertInvariantUnchanged(h.state, before);

    auto partial = encodePreset("apply-partial", "Partial");
    partial.pop_back();
    saveBytes(h.files, h.catalog, "apply-partial", partial);
    inspected = h.presets.inspectPreset("apply-partial", target, 0, 1);
    before = captureInvariant(h.state);
    result = h.presets.applyPreset(
        "apply-partial",
        target,
        inspected.descriptor.previewKey
    );
    assert(result.status == SequencerStepPresetStatus::CORRUPT);
    assertInvariantUnchanged(h.state, before);

    std::cout << "[PASS] test_apply_future_and_partial_assets_do_not_mutate_live_state\n";
}

void test_apply_stopped_preserves_destination_route_and_undoes_exactly() {
    Harness h;
    prepareTarget(h);
    saveBytes(
        h.files,
        h.catalog,
        "apply-valid",
        encodePreset("apply-valid", "Valid Source", 67)
    );
    const auto target = h.presets.captureTarget();
    const auto inspected = h.presets.inspectPreset("apply-valid", target, 0, 1);
    assert(inspected.inspected());
    const auto before = captureInvariant(h.state);

    const auto result = h.presets.applyPreset(
        "apply-valid",
        target,
        inspected.descriptor.previewKey
    );
    assert(result.ok());
    assert(result.status == SequencerStepPresetStatus::OK);
    assert(result.activation == SequencerStepPresetActivation::APPLIED);
    assert(h.state.sequencer.pattern.note[target.stepIndex] == 67);
    assert(h.state.sequencer.pattern.velocity[target.stepIndex] == 96);
    assert(h.state.projectTracks.authored.midiChannels[
        h.state.currentSharedActiveTrack()
    ] == 9);
    const auto& bankTrack = h.state.sequencerTracks.track(target.trackIndex);
    assert(bankTrack.note[target.stepIndex] == 67);
    assert(bankTrack.velocity[target.stepIndex] == 96);
    assert(h.state.projectTracks.authored.midiChannels[target.trackIndex] == 9);
    assert(h.state.project.metadata.modifiedCounter == 43);
    assert(h.state.project.metadata.dirty);
    assert(h.state.sequencerHistory.undoCount() == before.undoCount + 1U);
    assert(h.state.sequencerHistory.redoCount() == 0);

    const uint16_t targetBit = static_cast<uint16_t>(1U << target.trackIndex);
    assert(h.state.sequencerTrackActivations.pendingTrackMask() == targetBit);
    applyPendingRuntimeGeneration(h.state, target.trackIndex);
    assert(h.state.sequencerTrackActivations.pendingTrackMask() == 0);

    assert(h.state.undoSequencerHistory());
    auto restored = core::app::makeExtmemUnique<SequencerHistoryTrackBankSnapshot>();
    assert(restored);
    assert(core::state::sequencer::captureHistorySnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        *restored
    ));
    assert(core::state::sequencer::sameMusicalHistorySnapshot(
        *before.musical,
        *restored
    ));
    assert(h.state.projectTracks.authored.midiChannels[target.trackIndex] == 9);
    assert(h.state.sequencerHistory.undoCount() == before.undoCount);
    assert(h.state.sequencerHistory.redoCount() == 1);
    assert(h.state.project.metadata.modifiedCounter == 44);
    assert(h.state.sequencerTrackActivations.pendingTrackMask() == targetBit);
    applyPendingRuntimeGeneration(h.state, target.trackIndex);
    assert(h.state.sequencerTrackActivations.pendingTrackMask() == 0);

    std::cout
        << "[PASS] test_apply_stopped_preserves_destination_route_and_undoes_exactly\n";
}

void test_drum_target_reuses_shared_preset_and_preserves_lane_identity() {
    namespace seq = core::state::sequencer;
    Harness h;
    saveBytes(
        h.files,
        h.catalog,
        "drum-shared",
        encodeRandomCyclePreset("drum-shared", "Shared Rhythm")
    );

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
    auto& edit = h.state.sequencer.stepEdit;
    edit.reset();
    edit.visible.set(true);
    edit.drumContext = true;
    edit.drumLane = 1U;
    edit.drumStep = 2U;
    edit.drumRootSlot = 0xFFU;
    edit.stepIndex.set(2U);
    h.state.project.metadata.modifiedCounter = 42U;
    h.state.project.metadata.dirty = false;

    const auto identity = drum.kit.lanes[1U];
    const auto target = h.presets.captureTarget();
    assert(target.valid);
    assert(target.destinationOwnsPitch);
    assert(target.destinationNote == identity.midiNote);
    assert(target.drumLaneIndex == 1U);
    assert(target.drumRootStepIndex == 2U);
    assert(target.drumRootSlot == 0xFFU);

    const auto inspected = h.presets.inspectPreset(
        "drum-shared",
        target,
        0U,
        1U
    );
    assert(inspected.status == SequencerStepPresetStatus::OK);
    assert(inspected.descriptor.compatibility ==
           seq::SequencerStepPresetCompatibility::WARNING_ADAPTED);
    assert(inspected.descriptor.adaptation ==
           seq::SequencerStepPresetAdaptation::DESTINATION_PITCH);
    assert(inspected.descriptor.previewNote == identity.midiNote);
    assert((inspected.descriptor.contentFlags &
            seq::STEP_PRESET_CONTENT_CHORD) == 0U);

    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
    const auto result = h.presets.applyPreset(
        "drum-shared",
        target,
        inspected.descriptor.previewKey
    );
    assert(result.ok());
    assert(result.status == SequencerStepPresetStatus::OK);
    assert(result.activation == SequencerStepPresetActivation::APPLIED);
    assert(h.state.sequencerHistory.undoCount() == undoBefore + 1U);
    assert(drum.pattern.stepEnabled(1U, 2U));
    assert(drum.pattern.lanes[1U].velocity[2U] == 96U);
    assert(drum.pattern.lanes[1U].gate[2U] == 155U);
    assert(drum.pattern.lanes[1U].nudge[2U] == -3);
    assert(drum.pattern.lanes[1U].probability[2U] == 84U);
    assert(drum.kit.lanes[1U].midiNote == identity.midiNote);
    assert(drum.kit.lanes[1U].role == identity.role);
    assert(drum.kit.lanes[1U].overrideMask == identity.overrideMask);
    assert(drum.kit.lanes[1U].icon == identity.icon);
    assert(drum.kit.lanes[1U].colorIndex == identity.colorIndex);
    assert(drum.kit.lanes[1U].name == identity.name);

    const int16_t slot = drum.advancedRootSlot(1U, 2U);
    assert(slot >= 0);
    const auto* graph = seq::graphView(h.state.sequencer.pattern);
    assert(graph != nullptr);
    // The live Pattern graph also owns its canonical root sequence; the
    // preset's nested Micro/Cycle content is additive to that infrastructure.
    assert(graph->sequenceCount >= 1U);
    assert(graph->cycleSetCount >= 1U);
    const auto* root = graph->stepNode(
        seq::rootStepNodeId(static_cast<uint8_t>(slot))
    );
    assert(root != nullptr && root->has(
        oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE
    ));
    for (uint16_t index = 0U; index < graph->stepNodeCount; ++index) {
        const auto* node = graph->stepNode(index);
        assert(node != nullptr);
        assert(!node->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET));
        assert(!node->has(oc::note::sequencer::STEP_NODE_CHORD_MODE));
        assert(!node->has(oc::note::sequencer::STEP_NODE_CHORD_LOCAL));
        assert(node->noteOffset == 0);
        assert(node->localVariation.pitchSemitones == 0U);
    }

    assert(h.state.undoSequencerHistory());
    assert(!drum.pattern.stepEnabled(1U, 2U));
    assert(drum.advancedRootSlot(1U, 2U) < 0);
    assert(drum.kit.lanes[1U].midiNote == identity.midiNote);
    assert(h.state.redoSequencerHistory());
    assert(drum.pattern.stepEnabled(1U, 2U));
    assert(drum.advancedRootSlot(1U, 2U) >= 0);
    assert(drum.kit.lanes[1U].midiNote == identity.midiNote);

    std::cout
        << "[PASS] Drum target reuses shared Step preset and preserves identity\n";
}

void test_apply_playing_is_queued_and_undo_before_boundary_cancels_it() {
    Harness h;
    prepareTarget(h);
    h.state.statusBar.playing.set(true);
    saveBytes(
        h.files,
        h.catalog,
        "apply-queued",
        encodePreset("apply-queued", "Queued Source", 72)
    );
    const auto target = h.presets.captureTarget();
    const auto inspected = h.presets.inspectPreset("apply-queued", target, 0, 1);
    assert(inspected.inspected());
    const auto before = captureInvariant(h.state);

    const auto result = h.presets.applyPreset(
        "apply-queued",
        target,
        inspected.descriptor.previewKey
    );
    assert(result.ok());
    assert(result.status == SequencerStepPresetStatus::QUEUED);
    assert(result.activation == SequencerStepPresetActivation::QUEUED);
    assert(result.activationGeneration != 0);
    assert(h.state.sequencerTrackActivations.telemetry(target.trackIndex).origin ==
           core::state::sequencer::SequencerTrackActivationOrigin::PRESET);
    assert(
        h.presets.activationStatus(target.trackIndex, result.activationGeneration) ==
        core::state::sequencer::SequencerTrackActivationStatus::QUEUED
    );
    assert(h.state.sequencer.pattern.note[target.stepIndex] == 72);
    assert(h.state.projectTracks.authored.midiChannels[target.trackIndex] == 9);
    const uint16_t targetBit = static_cast<uint16_t>(1U << target.trackIndex);
    assert(h.state.sequencerTrackActivations.pendingTrackMask() == targetBit);

    assert(h.state.undoSequencerHistory());
    auto restored = core::app::makeExtmemUnique<SequencerHistoryTrackBankSnapshot>();
    assert(restored);
    assert(core::state::sequencer::captureHistorySnapshot(
        h.state.sequencerTracks,
        h.state.sequencer,
        *restored
    ));
    assert(core::state::sequencer::sameMusicalHistorySnapshot(
        *before.musical,
        *restored
    ));
    assert(h.state.sequencerTrackActivations.pendingTrackMask() == 0);
    assert(
        h.state.sequencerTrackActivations.telemetry(target.trackIndex).status ==
        core::state::sequencer::SequencerTrackActivationStatus::CANCELLED
    );
    assert(
        h.presets.activationStatus(target.trackIndex, result.activationGeneration) ==
        core::state::sequencer::SequencerTrackActivationStatus::CANCELLED
    );
    assert(h.state.sequencerHistory.undoCount() == before.undoCount);
    assert(h.state.sequencerHistory.redoCount() == 1);

    std::cout
        << "[PASS] test_apply_playing_is_queued_and_undo_before_boundary_cancels_it\n";
}

}  // namespace

int main() {
    test_manager_rename_reorders_and_delete_is_guarded();
    test_manager_refuses_previous_future_and_partial_without_mutation();
    test_step_presets_require_one_matching_pattern_pitch_context();
    test_apply_preflight_failures_leave_every_live_domain_unchanged();
    test_apply_allocation_failure_matrix_is_atomic_and_bounded();
    test_random_cycle_preview_is_stable_and_generation_admission_is_exact();
    test_apply_second_read_payload_change_is_stale_and_non_mutating();
    test_apply_activation_conflict_leaves_preexisting_queue_and_state_unchanged();
    test_apply_future_and_partial_assets_do_not_mutate_live_state();
    test_apply_stopped_preserves_destination_route_and_undoes_exactly();
    test_drum_target_reuses_shared_preset_and_preserves_lane_identity();
    test_apply_playing_is_queued_and_undo_before_boundary_cancels_it();

    std::cout << "\nAll SequencerStepPresetDomainServices tests passed.\n";
    return 0;
}

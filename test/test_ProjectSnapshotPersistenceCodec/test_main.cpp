#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>

#include "persistence/ProjectFileContainer.hpp"
#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectSnapshotPersistenceCodec.hpp"
#include "persistence/ProjectTrackStatePersistenceCodec.hpp"
#include "state/project/ProjectSnapshot.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace {

namespace project = core::state::project;
namespace project_file = core::persistence::project_file;
namespace snapshot_codec = core::persistence::project_snapshot_codec;
namespace track_codec = core::persistence::project_track_codec;
namespace sequencer = core::state::sequencer;

using ProjectBytes = std::array<uint8_t, core::persistence::PROJECT_FILE_MAX_SIZE>;

bool reportHas(
    const project_file::LoadReport& report,
    project_file::LoadCode code
) {
    for (uint8_t index = 0U; index < report.itemCount; ++index) {
        if (report.items[index].code == code) return true;
    }
    return false;
}

project::ProjectTrackSnapshot authoredTracks() {
    auto tracks = project::defaultProjectTrackSnapshot();
    for (uint8_t track = 0U; track < project::PROJECT_TRACK_COUNT; ++track) {
        tracks.midiChannels[track] = static_cast<uint8_t>(15U - track);
        tracks.delayMs[track] = static_cast<int16_t>(track * 7 - 40);
    }
    tracks.mutedMask = 0x8421U;
    tracks.soloMask = 0x0180U;
    return tracks;
}

bool sameTracks(
    const project::ProjectTrackSnapshot& lhs,
    const project::ProjectTrackSnapshot& rhs
) {
    return lhs.midiChannels == rhs.midiChannels &&
           lhs.delayMs == rhs.delayMs &&
           lhs.mutedMask == rhs.mutedMask &&
           lhs.soloMask == rhs.soloMask;
}

project::ProjectSnapshot makeSnapshot() {
    project::ProjectSnapshot snapshot{};
    assert(snapshot.projectControl);
    std::strncpy(
        snapshot.project.metadata.id.data(),
        "hardware-smoke",
        snapshot.project.metadata.id.size() - 1U
    );
    std::strncpy(
        snapshot.project.metadata.name.data(),
        "hardware-smoke",
        snapshot.project.metadata.name.size() - 1U
    );
    snapshot.project.metadata.hasSavedIdentity = true;
    snapshot.project.metadata.modifiedCounter = 42U;
    snapshot.project.transport.tempoBpm = 132.5F;
    snapshot.project.transport.swingPercent = 57U;
    snapshot.project.musical.scale.root = 7U;
    snapshot.project.editing.ccLaneDefaultControllers = {1U, 11U, 71U, 74U};

    snapshot.projectTracks = authoredTracks();
    snapshot.sharedTrackEnabledMask = 0x1043U;
    snapshot.sharedTrackActive = 6U;
    snapshot.macroTracks[6].enabledPageMask = 0x0007U;
    snapshot.macroTracks[6].activePage = 2U;
    snapshot.macroTracks[6].pages[2].activeMacroMask = 0x15U;
    snapshot.macroTracks[6].pages[2].cc[2] = 74U;
    snapshot.macroTracks[6].pages[2].values[2] = 0.625F;

    snapshot.sequencer.flat.enabledMask = snapshot.sharedTrackEnabledMask;
    snapshot.sequencer.flat.activeTrack = snapshot.sharedTrackActive;
    snapshot.sequencer.flat.tracks[6].note[0] = 64U;
    snapshot.sequencer.flat.tracks[6].velocity[0] = 103U;
    snapshot.drumTracks = core::app::makeExtmemUnique<
        core::state::sequencer::DrumTrackBankSnapshot>();
    assert(snapshot.drumTracks);
    for (auto& track : snapshot.drumTracks->tracks) track.reset();
    snapshot.drumTracks->drumTrackMask = static_cast<uint16_t>(1U << 6U);
    auto& drum = snapshot.drumTracks->tracks[6U];
    assert(drum.pattern.setStepEnabled(1U, 3U, true));
    assert(drum.pattern.setStepVelocity(1U, 3U, 111U));
    assert(drum.pattern.setLaneTimingCustom(1U, 7U, 2U));

    // One Drum hit owns Graph root slot 0. Persist a nested Micro -> Cycle
    // payload so the Project round-trip covers both the cold lane mapping and
    // the Graph owned by the active sequencer Track.
    sequencer::SequencerState advanced{};
    assert(advanced.setStepDataAt(0U, 64U, 103U, 100U, 0, 100U));
    const auto root = sequencer::rootStepNodeId(0U);
    const auto micro = sequencer::createMicroSequence(
        advanced.pattern,
        root,
        3U
    );
    assert(micro.ok);
    const auto* graph = sequencer::graphView(advanced.pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    const auto microNode = static_cast<sequencer::SequencerGraphNodeId>(
        sequence->firstStepNode + 1U
    );
    assert(sequencer::setNodeVelocityOffset(
        advanced.pattern,
        microNode,
        -23
    ));
    const auto cycle = sequencer::createCycleStateSet(
        advanced.pattern,
        microNode,
        2U
    );
    assert(cycle.ok);
    graph = sequencer::graphView(advanced.pattern);
    assert(graph != nullptr);
    const auto* cycleSet = graph->cycleSet(cycle.id);
    assert(cycleSet != nullptr);
    const auto cycleNode = static_cast<sequencer::SequencerGraphNodeId>(
        cycleSet->firstStateNode + 1U
    );
    assert(sequencer::setNodeGateOffset(
        advanced.pattern,
        cycleNode,
        17
    ));
    assert(drum.bindAdvancedRootSlot(0U, 1U, 3U));
    sequencer::captureSnapshot(
        advanced.pattern,
        snapshot.sequencer.flat.tracks[6U]
    );
    snapshot.sequencer.editorGraph = std::move(advanced.pattern.graph);
    return snapshot;
}

uint32_t encodeSnapshot(
    const project::ProjectSnapshot& snapshot,
    ProjectBytes& bytes
) {
    const auto encoded = snapshot_codec::encodeProjectSnapshot(
        snapshot,
        bytes.data(),
        static_cast<uint32_t>(bytes.size())
    );
    assert(encoded.status == project_file::Status::OK);
    return encoded.bytesWritten;
}

struct RebuildOptions {
    bool omitTrackState = false;
    bool omitMacro = false;
    bool omitSequencer = false;
    bool setMetaChunkFlags = false;
    bool addManifest = false;
    project_file::ChunkId changeVersionOf = project_file::ChunkId::MANIFEST;
    int8_t versionDelta = 0;
};

uint32_t rebuildContainer(
    const ProjectBytes& source,
    uint32_t sourceSize,
    ProjectBytes& destination,
    RebuildOptions options
) {
    std::array<project_file::DecodedChunkView, project_file::MAX_CHUNKS> decoded{};
    project_file::LoadReport report{};
    const auto read = project_file::scan(
        source.data(),
        sourceSize,
        decoded.data(),
        static_cast<uint16_t>(decoded.size()),
        &report
    );
    assert(read.status == project_file::Status::OK);

    std::array<project_file::ChunkView, project_file::MAX_CHUNKS> chunks{};
    uint16_t count = 0U;
    for (uint16_t index = 0U; index < read.chunkCount; ++index) {
        const auto& item = decoded[index];
        const bool track = item.id == project_file::chunkIdValue(
            project_file::ChunkId::TRACK_STATE
        );
        const bool macro = item.id == project_file::chunkIdValue(
            project_file::ChunkId::MACRO_STATE
        );
        const bool sequencer = item.id == project_file::chunkIdValue(
            project_file::ChunkId::SEQUENCER_STATE
        );
        if ((track && options.omitTrackState) ||
            (macro && options.omitMacro) ||
            (sequencer && options.omitSequencer)) {
            continue;
        }
        auto minor = item.versionMinor;
        if (item.id == project_file::chunkIdValue(options.changeVersionOf)) {
            minor = static_cast<uint8_t>(
                static_cast<int16_t>(minor) + options.versionDelta
            );
        }
        chunks[count++] = {
            .id = item.id,
            .versionMajor = item.versionMajor,
            .versionMinor = minor,
            .flags = static_cast<uint16_t>(
                item.flags |
                (options.setMetaChunkFlags &&
                         item.id == project_file::chunkIdValue(
                             project_file::ChunkId::PROJECT_META
                         )
                     ? 1U
                     : 0U)
            ),
            .data = item.data,
            .size = item.size,
        };
    }
    const uint8_t manifestPayload = 0U;
    if (options.addManifest) {
        chunks[count++] = {
            .id = project_file::chunkIdValue(project_file::ChunkId::MANIFEST),
            .versionMajor = 1U,
            .versionMinor = 0U,
            .flags = 0U,
            .data = &manifestPayload,
            .size = sizeof(manifestPayload),
        };
    }

    const auto encoded = project_file::encode(
        chunks.data(),
        count,
        0U,
        destination.data(),
        static_cast<uint32_t>(destination.size())
    );
    assert(encoded.status == project_file::Status::OK);
    return encoded.bytesWritten;
}

void testCurrentSnapshotRoundTripAndDeterminism() {
    const auto source = makeSnapshot();
    auto first = std::make_unique<ProjectBytes>();
    auto second = std::make_unique<ProjectBytes>();
    assert(first && second);
    const uint32_t firstSize = encodeSnapshot(source, *first);
    const uint32_t secondSize = encodeSnapshot(source, *second);
    assert(firstSize == secondSize);
    assert(std::equal(
        first->begin(),
        first->begin() + firstSize,
        second->begin()
    ));

    project::ProjectSnapshot loaded{};
    project_file::LoadReport report{};
    const auto decoded = snapshot_codec::decodeProjectSnapshot(
        first->data(),
        firstSize,
        loaded,
        &report
    );
    assert(decoded.ok && decoded.overwriteSafe);
    assert(decoded.loadStatus == project_file::LoadStatus::OK);
    assert(report.ok());
    assert(std::strcmp(loaded.project.metadata.id.data(), "hardware-smoke") == 0);
    assert(loaded.project.transport.tempoBpm == 132.5F);
    assert(loaded.sharedTrackEnabledMask == source.sharedTrackEnabledMask);
    assert(loaded.sharedTrackActive == source.sharedTrackActive);
    assert(loaded.macroTracks[6].pages[2].cc[2] == 74U);
    assert(loaded.sequencer.flat.tracks[6].note[0] == 64U);
    assert(loaded.drumTracks);
    assert(loaded.drumTracks->drumTrackMask == static_cast<uint16_t>(1U << 6U));
    assert(loaded.drumTracks->tracks[6U].pattern.stepEnabled(1U, 3U));
    assert(loaded.drumTracks->tracks[6U].pattern.lanes[1U].velocity[3U] == 111U);
    assert(loaded.drumTracks->tracks[6U].pattern.effectiveLength(1U) == 7U);
    assert(loaded.drumTracks->tracks[6U].advancedRootSlot(1U, 3U) == 0);
    assert(loaded.sequencer.editorGraph);
    const auto* rootNode = loaded.sequencer.editorGraph->stepNode(
        sequencer::rootStepNodeId(0U)
    );
    assert(rootNode != nullptr);
    const auto* microSequence = loaded.sequencer.editorGraph->sequence(
        rootNode->childSequenceId
    );
    assert(microSequence != nullptr);
    assert(microSequence->length == 3U);
    const auto* microNode = loaded.sequencer.editorGraph->stepNode(
        static_cast<sequencer::SequencerGraphNodeId>(
            microSequence->firstStepNode + 1U
        )
    );
    assert(microNode != nullptr);
    assert(microNode->velocityOffset == -23);
    const auto* cycleSet = loaded.sequencer.editorGraph->cycleSet(
        microNode->cycleSetId
    );
    assert(cycleSet != nullptr);
    assert(cycleSet->length == 2U);
    const auto* cycleNode = loaded.sequencer.editorGraph->stepNode(
        static_cast<sequencer::SequencerGraphNodeId>(
            cycleSet->firstStateNode + 1U
        )
    );
    assert(cycleNode != nullptr);
    assert(cycleNode->gateOffset == 17);
    assert(sameTracks(loaded.projectTracks, source.projectTracks));

    std::cout << "[PASS] current snapshot round-trip is deterministic\n";
}

void testMissingCurrentTrackChunkIsRejected() {
    const auto source = makeSnapshot();
    auto current = std::make_unique<ProjectBytes>();
    auto withoutTracks = std::make_unique<ProjectBytes>();
    assert(current && withoutTracks);
    const uint32_t currentSize = encodeSnapshot(source, *current);
    const uint32_t rebuiltSize = rebuildContainer(
        *current,
        currentSize,
        *withoutTracks,
        {.omitTrackState = true}
    );

    project::ProjectSnapshot loaded{};
    project_file::LoadReport report{};
    const auto decoded = snapshot_codec::decodeProjectSnapshot(
        withoutTracks->data(),
        rebuiltSize,
        loaded,
        &report
    );
    assert(!decoded.ok);
    assert(!decoded.overwriteSafe);
    assert(decoded.loadStatus == project_file::LoadStatus::FAILED);
    assert(reportHas(report, project_file::LoadCode::MISSING_REQUIRED_CHUNK));

    std::cout << "[PASS] missing authoritative Track chunk is rejected\n";
}

void testMissingMacroAndSequencerChunksAreRejectedAtomically() {
    const auto source = makeSnapshot();
    auto current = std::make_unique<ProjectBytes>();
    auto reduced = std::make_unique<ProjectBytes>();
    assert(current && reduced);
    const uint32_t currentSize = encodeSnapshot(source, *current);
    const uint32_t reducedSize = rebuildContainer(
        *current,
        currentSize,
        *reduced,
        {.omitMacro = true, .omitSequencer = true}
    );

    project::ProjectSnapshot loaded{};
    loaded.project.transport.tempoBpm = 91.25F;
    const auto sentinelTracks = authoredTracks();
    loaded.projectTracks = sentinelTracks;
    project_file::LoadReport report{};
    const auto decoded = snapshot_codec::decodeProjectSnapshot(
        reduced->data(),
        reducedSize,
        loaded,
        &report
    );
    assert(!decoded.ok && !decoded.overwriteSafe);
    assert(decoded.loadStatus == project_file::LoadStatus::FAILED);
    assert(reportHas(report, project_file::LoadCode::MISSING_REQUIRED_CHUNK));
    assert(loaded.project.transport.tempoBpm == 91.25F);
    assert(sameTracks(loaded.projectTracks, sentinelTracks));

    std::cout << "[PASS] missing runtime chunks are rejected atomically\n";
}

void testCurrentProjectChunkSetIsExact() {
    const auto source = makeSnapshot();
    auto current = std::make_unique<ProjectBytes>();
    auto flagged = std::make_unique<ProjectBytes>();
    auto unexpected = std::make_unique<ProjectBytes>();
    assert(current && flagged && unexpected);
    const uint32_t currentSize = encodeSnapshot(source, *current);
    const uint32_t flaggedSize = rebuildContainer(
        *current,
        currentSize,
        *flagged,
        {.setMetaChunkFlags = true}
    );
    const uint32_t unexpectedSize = rebuildContainer(
        *current,
        currentSize,
        *unexpected,
        {.addManifest = true}
    );

    project::ProjectSnapshot loaded{};
    loaded.project.transport.tempoBpm = 91.25F;
    project_file::LoadReport report{};
    auto decoded = snapshot_codec::decodeProjectSnapshot(
        flagged->data(),
        flaggedSize,
        loaded,
        &report
    );
    assert(!decoded.ok);
    assert(reportHas(
        report,
        project_file::LoadCode::UNSUPPORTED_CHUNK_FLAGS
    ));
    assert(loaded.project.transport.tempoBpm == 91.25F);

    report.reset();
    decoded = snapshot_codec::decodeProjectSnapshot(
        unexpected->data(),
        unexpectedSize,
        loaded,
        &report
    );
    assert(!decoded.ok);
    assert(reportHas(report, project_file::LoadCode::UNEXPECTED_CHUNK));
    assert(loaded.project.transport.tempoBpm == 91.25F);

    std::cout << "[PASS] current project chunk set is exact\n";
}

void testStaleCurrentChunkVersionsAreRejectedStrictly() {
    const auto source = makeSnapshot();
    auto current = std::make_unique<ProjectBytes>();
    auto staleControl = std::make_unique<ProjectBytes>();
    auto futureTrack = std::make_unique<ProjectBytes>();
    assert(current && staleControl && futureTrack);
    const uint32_t currentSize = encodeSnapshot(source, *current);

    const uint32_t staleSize = rebuildContainer(
        *current,
        currentSize,
        *staleControl,
        {
            .changeVersionOf = project_file::ChunkId::MODULATION_GRAPH,
            .versionDelta = -1,
        }
    );
    project::ProjectSnapshot partial{};
    std::strncpy(
        partial.project.metadata.name.data(),
        "unchanged",
        partial.project.metadata.name.size() - 1U
    );
    project_file::LoadReport partialReport{};
    const auto staleDecoded = snapshot_codec::decodeProjectSnapshot(
        staleControl->data(),
        staleSize,
        partial,
        &partialReport
    );
    assert(!staleDecoded.ok);
    assert(staleDecoded.loadStatus == project_file::LoadStatus::FAILED);
    assert(!staleDecoded.overwriteSafe);
    assert(reportHas(
        partialReport,
        project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION
    ));
    assert(
        std::strcmp(
            partial.project.metadata.name.data(),
            "unchanged"
        ) == 0
    );

    const uint32_t futureSize = rebuildContainer(
        *current,
        currentSize,
        *futureTrack,
        {
            .changeVersionOf = project_file::ChunkId::TRACK_STATE,
            .versionDelta = 1,
        }
    );
    project::ProjectSnapshot rejected{};
    project_file::LoadReport rejectedReport{};
    const auto futureDecoded = snapshot_codec::decodeProjectSnapshot(
        futureTrack->data(),
        futureSize,
        rejected,
        &rejectedReport
    );
    assert(!futureDecoded.ok);
    assert(futureDecoded.loadStatus == project_file::LoadStatus::FAILED);
    assert(!futureDecoded.overwriteSafe);
    assert(reportHas(
        rejectedReport,
        project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION
    ));

    std::memcpy(
        futureTrack->data(),
        current->data(),
        currentSize
    );
    (*futureTrack)[4] = static_cast<uint8_t>(
        project_file::CONTAINER_VERSION_MAJOR + 1U
    );
    project::ProjectSnapshot futureContainer{};
    std::strncpy(
        futureContainer.project.metadata.name.data(),
        "unchanged",
        futureContainer.project.metadata.name.size() - 1U
    );
    project_file::LoadReport futureContainerReport{};
    const auto futureContainerDecoded =
        snapshot_codec::decodeProjectSnapshot(
            futureTrack->data(),
            currentSize,
            futureContainer,
            &futureContainerReport
        );
    assert(!futureContainerDecoded.ok);
    assert(
        futureContainerDecoded.loadStatus ==
        project_file::LoadStatus::FAILED
    );
    assert(!futureContainerDecoded.overwriteSafe);
    assert(reportHas(
        futureContainerReport,
        project_file::LoadCode::UNSUPPORTED_CONTAINER_VERSION
    ));
    assert(
        std::strcmp(
            futureContainer.project.metadata.name.data(),
            "unchanged"
        ) == 0
    );

    std::cout
        << "[PASS] non-current project formats are rejected atomically\n";
}

}  // namespace

int main() {
    testCurrentSnapshotRoundTripAndDeterminism();
    testMissingCurrentTrackChunkIsRejected();
    testMissingMacroAndSequencerChunksAreRejectedAtomically();
    testCurrentProjectChunkSetIsExact();
    testStaleCurrentChunkVersionsAreRejectedStrictly();
    std::cout << "All ProjectSnapshotPersistenceCodec tests passed\n";
    return 0;
}

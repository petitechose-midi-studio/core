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

namespace {

namespace project = core::state::project;
namespace project_file = core::persistence::project_file;
namespace snapshot_codec = core::persistence::project_snapshot_codec;
namespace track_codec = core::persistence::project_track_codec;

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
    const auto read = project_file::decode(
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
            .flags = item.flags,
            .data = item.data,
            .size = item.size,
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
    assert(reportHas(report, project_file::LoadCode::CHUNK_PAYLOAD_INVALID));

    std::cout << "[PASS] missing authoritative Track chunk is rejected\n";
}

void testMissingMacroAndSequencerChunksDefaultSafely() {
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
    project_file::LoadReport report{};
    const auto decoded = snapshot_codec::decodeProjectSnapshot(
        reduced->data(),
        reducedSize,
        loaded,
        &report
    );
    assert(decoded.ok && decoded.overwriteSafe);
    assert(decoded.loadStatus == project_file::LoadStatus::OK);
    assert(reportHas(report, project_file::LoadCode::MISSING_OPTIONAL_CHUNK));
    assert(sameTracks(loaded.projectTracks, source.projectTracks));

    std::cout << "[PASS] missing optional runtime chunks default safely\n";
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
    project_file::LoadReport partialReport{};
    const auto staleDecoded = snapshot_codec::decodeProjectSnapshot(
        staleControl->data(),
        staleSize,
        partial,
        &partialReport
    );
    assert(staleDecoded.ok);
    assert(staleDecoded.loadStatus == project_file::LoadStatus::PARTIAL);
    assert(!staleDecoded.overwriteSafe);
    assert(reportHas(
        partialReport,
        project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION
    ));
    assert(partial.projectControl);
    assert(partial.projectControl->modulation.sourceCount == 0U);

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

    std::cout << "[PASS] stale current chunks are rejected strictly\n";
}

}  // namespace

int main() {
    testCurrentSnapshotRoundTripAndDeterminism();
    testMissingCurrentTrackChunkIsRejected();
    testMissingMacroAndSequencerChunksDefaultSafely();
    testStaleCurrentChunkVersionsAreRejectedStrictly();
    std::cout << "All ProjectSnapshotPersistenceCodec tests passed\n";
    return 0;
}

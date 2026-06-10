#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/ProjectStatePersistenceCodec.hpp"
#include "persistence/SequencerPersistenceEnvelope.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::persistence::project_snapshot_codec {

namespace {

namespace project_file = core::persistence::project_file;
namespace project_state_codec = core::persistence::project_state_codec;
namespace sequencer_codec = core::persistence::sequencer_codec;

FLASHMEM void addReport(project_file::LoadReport* report,
                        project_file::LoadSeverity severity,
                        project_file::LoadCode code,
                        uint32_t chunkId,
                        uint8_t sourceMajor = 0,
                        uint8_t sourceMinor = 0) {
    if (report == nullptr) return;
    report->add(severity,
                code,
                chunkId,
                sourceMajor,
                sourceMinor,
                PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
                PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR);
}

FLASHMEM const project_file::DecodedChunkView* findChunk(
    const project_file::DecodedChunkView* chunks,
    uint16_t count,
    project_file::ChunkId id
) {
    const uint32_t raw = project_file::chunkIdValue(id);
    for (uint16_t i = 0; i < count; ++i) {
        if (chunks[i].id == raw) return &chunks[i];
    }
    return nullptr;
}

FLASHMEM bool chunkVersionSupported(const project_file::DecodedChunkView& chunk,
                                    project_file::LoadReport* report) {
    if (chunk.versionMajor <= PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR) return true;
    addReport(report,
              project_file::LoadSeverity::WARNING,
              project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION,
              chunk.id,
              chunk.versionMajor,
              chunk.versionMinor);
    return false;
}

FLASHMEM void reportDefaulted(project_file::LoadReport* report, project_file::ChunkId id) {
    addReport(report,
              project_file::LoadSeverity::INFO,
              project_file::LoadCode::DEFAULTED_CHUNK,
              project_file::chunkIdValue(id));
}

FLASHMEM void reportMissingOptional(project_file::LoadReport* report, project_file::ChunkId id) {
    addReport(report,
              project_file::LoadSeverity::INFO,
              project_file::LoadCode::MISSING_OPTIONAL_CHUNK,
              project_file::chunkIdValue(id));
    reportDefaulted(report, id);
}

FLASHMEM void fillMacroPayload(const core::state::project::ProjectSnapshot& snapshot,
                               ProjectMacroStatePayload& out) {
    out.activeTrack = snapshot.sharedTrackActive;
    out.trackEnabledMask = snapshot.sharedTrackEnabledMask;
    out.tracks = snapshot.macroTracks;
}

FLASHMEM bool readMacroChunk(const project_file::DecodedChunkView* chunk,
                             core::state::project::ProjectSnapshot& target,
                             project_file::LoadReport* report) {
    if (chunk == nullptr) {
        reportMissingOptional(report, project_file::ChunkId::MACRO_STATE);
        return true;
    }
    if (!chunkVersionSupported(*chunk, report)) {
        reportDefaulted(report, project_file::ChunkId::MACRO_STATE);
        return false;
    }
    if (chunk->size != sizeof(ProjectMacroStatePayload) || chunk->data == nullptr) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        reportDefaulted(report, project_file::ChunkId::MACRO_STATE);
        return false;
    }

    ProjectMacroStatePayload payload{};
    std::memcpy(&payload, chunk->data, sizeof(payload));
    target.sharedTrackActive = payload.activeTrack;
    target.sharedTrackEnabledMask = payload.trackEnabledMask;
    target.macroTracks = payload.tracks;
    return true;
}

FLASHMEM bool buildSequencerEnvelope(
    const core::state::project::ProjectSnapshot& snapshot,
    sequencer_codec::EnvelopeBuffer& out,
    uint16_t& outSize
) {
    auto bank = core::app::makeExtmemUnique<core::state::sequencer::SequencerTrackBankState>();
    auto active = core::app::makeExtmemUnique<core::state::sequencer::SequencerState>();
    if (!bank || !active) return false;

    if (!core::state::sequencer::applyHistorySnapshot(*bank, *active, snapshot.sequencer)) {
        return false;
    }

    const auto encoded = sequencer_codec::fillProjectSequencerEnvelope(
        *bank,
        *active,
        out.bytes.data(),
        static_cast<uint16_t>(out.bytes.size())
    );
    if (!encoded.ok) return false;
    outSize = encoded.size;
    return true;
}

FLASHMEM bool readSequencerChunk(const project_file::DecodedChunkView* chunk,
                                 core::state::project::ProjectSnapshot& target,
                                 project_file::LoadReport* report) {
    if (chunk == nullptr) {
        reportMissingOptional(report, project_file::ChunkId::SEQUENCER_STATE);
        return true;
    }
    if (!chunkVersionSupported(*chunk, report)) {
        reportDefaulted(report, project_file::ChunkId::SEQUENCER_STATE);
        return false;
    }
    if (chunk->size > sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE || chunk->data == nullptr) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        reportDefaulted(report, project_file::ChunkId::SEQUENCER_STATE);
        return false;
    }

    auto bank = core::app::makeExtmemUnique<core::state::sequencer::SequencerTrackBankState>();
    auto active = core::app::makeExtmemUnique<core::state::sequencer::SequencerState>();
    if (!bank || !active) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        reportDefaulted(report, project_file::ChunkId::SEQUENCER_STATE);
        return false;
    }

    bank->syncSharedTrackState(target.sharedTrackEnabledMask, target.sharedTrackActive);

    if (!sequencer_codec::applyProjectSequencerEnvelope(
            chunk->data,
            static_cast<uint16_t>(chunk->size),
            *bank,
            *active
        ) ||
        !core::state::sequencer::captureHistorySnapshot(*bank, *active, target.sequencer)) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        reportDefaulted(report, project_file::ChunkId::SEQUENCER_STATE);
        return false;
    }

    return true;
}

}  // namespace

FLASHMEM project_file::EncodeResult encodeProjectSnapshot(
    const core::state::project::ProjectSnapshot& snapshot,
    uint8_t* out,
    uint32_t outCapacity
) {
    project_state_codec::ProjectMetaPayload meta{};
    project_state_codec::ProjectTransportPayload transport{};
    project_state_codec::ProjectMusicalContextPayload musical{};
    project_state_codec::ProjectRoutingPayload routing{};
    project_state_codec::fillMetaPayload(snapshot.project.metadata, meta);
    project_state_codec::fillTransportPayload(snapshot.project.transport, transport);
    project_state_codec::fillMusicalContextPayload(snapshot.project.musical, musical);
    project_state_codec::fillRoutingPayload(snapshot.project.routing, routing);

    auto macro = core::app::makeExtmemUnique<ProjectMacroStatePayload>();
    auto sequencer = core::app::makeExtmemUnique<sequencer_codec::EnvelopeBuffer>();
    if (!macro || !sequencer) {
        return {.status = project_file::Status::SCRATCH_ALLOCATION_FAILED, .bytesWritten = 0};
    }

    fillMacroPayload(snapshot, *macro);
    uint16_t sequencerSize = 0;
    if (!buildSequencerEnvelope(snapshot, *sequencer, sequencerSize)) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }

    const project_file::ChunkView chunks[] = {
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::PROJECT_META),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = reinterpret_cast<const uint8_t*>(&meta),
            .size = sizeof(meta),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::TRANSPORT),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = reinterpret_cast<const uint8_t*>(&transport),
            .size = sizeof(transport),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::MUSICAL_CONTEXT),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = reinterpret_cast<const uint8_t*>(&musical),
            .size = sizeof(musical),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::ROUTING),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = reinterpret_cast<const uint8_t*>(&routing),
            .size = sizeof(routing),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::MACRO_STATE),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = reinterpret_cast<const uint8_t*>(macro.get()),
            .size = sizeof(ProjectMacroStatePayload),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::SEQUENCER_STATE),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = sequencer->bytes.data(),
            .size = sequencerSize,
        },
    };

    return project_file::encode(
        chunks,
        static_cast<uint16_t>(sizeof(chunks) / sizeof(chunks[0])),
        snapshot.project.metadata.modifiedCounter,
        out,
        outCapacity
    );
}

FLASHMEM DecodeResult decodeProjectSnapshot(
    const uint8_t* data,
    uint32_t size,
    core::state::project::ProjectSnapshot& out,
    project_file::LoadReport* report
) {
    project_file::DecodedChunkView chunks[project_file::MAX_CHUNKS] = {};
    auto decodeResult = project_file::decode(
        data,
        size,
        chunks,
        project_file::MAX_CHUNKS,
        report
    );
    if (decodeResult.status != project_file::Status::OK) {
        return {
            .ok = false,
            .containerStatus = decodeResult.status,
            .loadStatus = report == nullptr ? project_file::LoadStatus::FAILED : report->status,
            .overwriteSafe = false,
        };
    }

    core::state::project::ProjectSnapshot next;
    project_state_codec::applyProjectStateChunks(
        chunks,
        decodeResult.chunkCount,
        next.project,
        report
    );
    readMacroChunk(findChunk(chunks, decodeResult.chunkCount, project_file::ChunkId::MACRO_STATE),
                   next,
                   report);
    readSequencerChunk(
        findChunk(chunks, decodeResult.chunkCount, project_file::ChunkId::SEQUENCER_STATE),
        next,
        report
    );

    out = std::move(next);
    const project_file::LoadStatus loadStatus =
        report == nullptr ? project_file::LoadStatus::OK : report->status;
    const bool overwriteSafe =
        report == nullptr ? decodeResult.overwriteSafe : report->overwriteSafe;
    return {
        .ok = loadStatus != project_file::LoadStatus::FAILED,
        .containerStatus = decodeResult.status,
        .loadStatus = loadStatus,
        .overwriteSafe = overwriteSafe,
    };
}

}  // namespace core::persistence::project_snapshot_codec

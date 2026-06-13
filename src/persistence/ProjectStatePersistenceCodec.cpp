#include "persistence/ProjectStatePersistenceCodec.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/ProjectChunkMigration.hpp"
#include "state/project/ProjectDomainRules.hpp"

namespace core::persistence::project_state_codec {

namespace {

namespace project_file = core::persistence::project_file;
namespace migration = core::persistence::project_migration;

constexpr uint8_t kMetaFlagDirty = 1U << 0U;
constexpr uint8_t kMetaFlagHasSavedIdentity = 1U << 1U;
constexpr uint8_t kMusicalFlagPatternsInheritScale = 1U << 0U;
constexpr uint8_t kMusicalFlagClipsInheritScale = 1U << 1U;
FLASHMEM uint16_t tempoToCentiBpm(float tempoBpm) {
    return core::state::project::projectTempoToCentiBpm(tempoBpm);
}

FLASHMEM float centiBpmToTempo(uint16_t centiBpm) {
    return core::state::project::projectCentiBpmToTempo(centiBpm);
}

FLASHMEM void copyFixedText(const char* source, char* target, size_t size) {
    if (target == nullptr || size == 0) return;
    std::memset(target, 0, size);
    if (source == nullptr) return;
    std::strncpy(target, source, size - 1U);
}

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
                PROJECT_STATE_CHUNK_VERSION_MAJOR,
                PROJECT_STATE_CHUNK_VERSION_MINOR);
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

template <typename Payload>
FLASHMEM bool readPayload(const project_file::DecodedChunkView* chunk,
                          Payload& out,
                          project_file::LoadReport* report) {
    if (chunk == nullptr) return false;
    if (chunk->versionMajor > PROJECT_STATE_CHUNK_VERSION_MAJOR) {
        addReport(report,
                  project_file::LoadSeverity::WARNING,
                  project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        return false;
    }
    const bool needsMigration =
        chunk->versionMajor < PROJECT_STATE_CHUNK_VERSION_MAJOR ||
        (chunk->versionMajor == PROJECT_STATE_CHUNK_VERSION_MAJOR &&
         chunk->versionMinor < PROJECT_STATE_CHUNK_VERSION_MINOR &&
         chunk->size != sizeof(Payload));
    if (needsMigration) {
        const auto migrated = migration::migrateToCurrent(
            *chunk,
            reinterpret_cast<uint8_t*>(&out),
            sizeof(out)
        );
        switch (migrated.status) {
            case migration::Status::MIGRATED:
                addReport(report,
                          project_file::LoadSeverity::INFO,
                          project_file::LoadCode::MIGRATED_CHUNK,
                          chunk->id,
                          chunk->versionMajor,
                          chunk->versionMinor);
                return migrated.bytesWritten == sizeof(out);
            case migration::Status::INVALID_PAYLOAD:
            case migration::Status::OUTPUT_TOO_SMALL:
                addReport(report,
                          project_file::LoadSeverity::ERROR,
                          project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                          chunk->id,
                          chunk->versionMajor,
                          chunk->versionMinor);
                return false;
            case migration::Status::UNSUPPORTED:
            case migration::Status::NOT_NEEDED:
            default:
                addReport(report,
                          project_file::LoadSeverity::WARNING,
                          project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION,
                          chunk->id,
                          chunk->versionMajor,
                          chunk->versionMinor);
                return false;
        }
    }
    if (chunk->size != sizeof(Payload) || chunk->data == nullptr) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        return false;
    }
    std::memcpy(&out, chunk->data, sizeof(Payload));
    return true;
}

template <typename Payload>
FLASHMEM bool applyOptionalChunk(const project_file::DecodedChunkView* chunks,
                                 uint16_t count,
                                 project_file::ChunkId id,
                                 project_file::LoadReport* report,
                                 void (*applyPayload)(const Payload&,
                                                      core::state::project::ProjectState&),
                                 core::state::project::ProjectState& target) {
    const auto* chunk = findChunk(chunks, count, id);
    if (chunk == nullptr) {
        addReport(report,
                  project_file::LoadSeverity::INFO,
                  project_file::LoadCode::MISSING_OPTIONAL_CHUNK,
                  project_file::chunkIdValue(id));
        addReport(report,
                  project_file::LoadSeverity::INFO,
                  project_file::LoadCode::DEFAULTED_CHUNK,
                  project_file::chunkIdValue(id));
        return true;
    }

    Payload payload{};
    if (!readPayload(chunk, payload, report)) {
        addReport(report,
                  project_file::LoadSeverity::INFO,
                  project_file::LoadCode::DEFAULTED_CHUNK,
                  project_file::chunkIdValue(id));
        return false;
    }
    applyPayload(payload, target);
    return true;
}

FLASHMEM void applyMetaToProject(const ProjectMetaPayload& payload,
                                 core::state::project::ProjectState& target) {
    applyMetaPayload(payload, target.metadata);
}

FLASHMEM void applyTransportToProject(const ProjectTransportPayload& payload,
                                      core::state::project::ProjectState& target) {
    applyTransportPayload(payload, target.transport);
}

FLASHMEM void applyMusicalToProject(const ProjectMusicalContextPayload& payload,
                                    core::state::project::ProjectState& target) {
    applyMusicalContextPayload(payload, target.musical);
}

FLASHMEM void applyRoutingToProject(const ProjectRoutingPayload& payload,
                                    core::state::project::ProjectState& target) {
    applyRoutingPayload(payload, target.routing);
}

}  // namespace

FLASHMEM void fillMetaPayload(const core::state::project::ProjectMetadata& source,
                              ProjectMetaPayload& out) {
    copyFixedText(source.id.data(), out.id, sizeof(out.id));
    copyFixedText(source.name.data(), out.name, sizeof(out.name));
    out.modifiedCounter = source.modifiedCounter;
    out.flags = 0;
    if (source.dirty) out.flags |= kMetaFlagDirty;
    if (source.hasSavedIdentity) out.flags |= kMetaFlagHasSavedIdentity;
}

FLASHMEM void applyMetaPayload(const ProjectMetaPayload& payload,
                               core::state::project::ProjectMetadata& target) {
    copyFixedText(payload.id, target.id.data(), target.id.size());
    copyFixedText(payload.name, target.name.data(), target.name.size());
    target.modifiedCounter = payload.modifiedCounter;
    target.dirty = (payload.flags & kMetaFlagDirty) != 0;
    target.hasSavedIdentity = (payload.flags & kMetaFlagHasSavedIdentity) != 0;
}

FLASHMEM void fillTransportPayload(const core::state::project::ProjectTransportState& source,
                                   ProjectTransportPayload& out) {
    out.tempoCentiBpm = tempoToCentiBpm(source.tempoBpm);
    out.swingPercent = core::state::project::sanitizeProjectSwingPercent(source.swingPercent);
    out.runMode = core::state::project::sanitizeProjectRunMode(source.runMode);
}

FLASHMEM void applyTransportPayload(const ProjectTransportPayload& payload,
                                    core::state::project::ProjectTransportState& target) {
    target.tempoBpm = centiBpmToTempo(payload.tempoCentiBpm);
    target.swingPercent = core::state::project::sanitizeProjectSwingPercent(payload.swingPercent);
    target.runMode = core::state::project::sanitizeProjectRunMode(payload.runMode);
}

FLASHMEM void fillMusicalContextPayload(const core::state::project::ProjectMusicalContext& source,
                                        ProjectMusicalContextPayload& out) {
    auto scale = source.scale;
    scale.clamp();
    out.scaleRoot = scale.root;
    out.scaleType = static_cast<uint8_t>(scale.type);
    out.scaleConstraintMode = static_cast<uint8_t>(scale.mode);
    out.flags = 0;
    if (source.patternsInheritScale) out.flags |= kMusicalFlagPatternsInheritScale;
    if (source.clipsInheritScale) out.flags |= kMusicalFlagClipsInheritScale;
}

FLASHMEM void applyMusicalContextPayload(const ProjectMusicalContextPayload& payload,
                                         core::state::project::ProjectMusicalContext& target) {
    target.scale = {
        .root = payload.scaleRoot,
        .type = static_cast<oc::note::sequencer::StepSequencerScaleType>(payload.scaleType),
        .mode = static_cast<oc::note::sequencer::StepSequencerScaleConstraintMode>(
            payload.scaleConstraintMode
        ),
    };
    target.scale.clamp();
    target.patternsInheritScale = (payload.flags & kMusicalFlagPatternsInheritScale) != 0;
    target.clipsInheritScale = (payload.flags & kMusicalFlagClipsInheritScale) != 0;
}

FLASHMEM void fillRoutingPayload(const core::state::project::ProjectRoutingState& source,
                                 ProjectRoutingPayload& out) {
    for (uint8_t i = 0; i < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        out.outputMidiChannels[i] =
            core::state::project::sanitizeProjectMidiChannel(source.outputMidiChannels[i]);
    }
}

FLASHMEM void applyRoutingPayload(const ProjectRoutingPayload& payload,
                                  core::state::project::ProjectRoutingState& target) {
    for (uint8_t i = 0; i < target.outputMidiChannels.size(); ++i) {
        target.outputMidiChannels[i] =
            core::state::project::sanitizeProjectMidiChannel(payload.outputMidiChannels[i]);
    }
}

FLASHMEM project_file::EncodeResult encodeProjectState(
    const core::state::project::ProjectState& state,
    uint8_t* out,
    uint32_t outCapacity
) {
    ProjectMetaPayload meta{};
    ProjectTransportPayload transport{};
    ProjectMusicalContextPayload musical{};
    ProjectRoutingPayload routing{};

    fillMetaPayload(state.metadata, meta);
    fillTransportPayload(state.transport, transport);
    fillMusicalContextPayload(state.musical, musical);
    fillRoutingPayload(state.routing, routing);

    const project_file::ChunkView chunks[] = {
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::PROJECT_META),
            .versionMajor = PROJECT_STATE_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_STATE_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = reinterpret_cast<const uint8_t*>(&meta),
            .size = sizeof(meta),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::TRANSPORT),
            .versionMajor = PROJECT_STATE_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_STATE_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = reinterpret_cast<const uint8_t*>(&transport),
            .size = sizeof(transport),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::MUSICAL_CONTEXT),
            .versionMajor = PROJECT_STATE_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_STATE_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = reinterpret_cast<const uint8_t*>(&musical),
            .size = sizeof(musical),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::ROUTING),
            .versionMajor = PROJECT_STATE_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_STATE_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = reinterpret_cast<const uint8_t*>(&routing),
            .size = sizeof(routing),
        },
    };

    return project_file::encode(
        chunks,
        static_cast<uint16_t>(sizeof(chunks) / sizeof(chunks[0])),
        state.metadata.modifiedCounter,
        out,
        outCapacity
    );
}

FLASHMEM void applyProjectStateChunks(const project_file::DecodedChunkView* chunks,
                                      uint16_t chunkCount,
                                      core::state::project::ProjectState& target,
                                      project_file::LoadReport* report) {
    applyOptionalChunk<ProjectMetaPayload>(
        chunks,
        chunkCount,
        project_file::ChunkId::PROJECT_META,
        report,
        applyMetaToProject,
        target
    );
    applyOptionalChunk<ProjectTransportPayload>(
        chunks,
        chunkCount,
        project_file::ChunkId::TRANSPORT,
        report,
        applyTransportToProject,
        target
    );
    applyOptionalChunk<ProjectMusicalContextPayload>(
        chunks,
        chunkCount,
        project_file::ChunkId::MUSICAL_CONTEXT,
        report,
        applyMusicalToProject,
        target
    );
    applyOptionalChunk<ProjectRoutingPayload>(
        chunks,
        chunkCount,
        project_file::ChunkId::ROUTING,
        report,
        applyRoutingToProject,
        target
    );
}

FLASHMEM DecodeResult decodeProjectState(const uint8_t* data,
                                         uint32_t size,
                                         core::state::project::ProjectState& out,
                                         project_file::LoadReport* report) {
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

    core::state::project::ProjectState next;
    applyProjectStateChunks(chunks, decodeResult.chunkCount, next, report);

    out = next;
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

}  // namespace core::persistence::project_state_codec

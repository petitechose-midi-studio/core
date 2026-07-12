#include "persistence/ProjectStatePersistenceCodec.hpp"

#include <array>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceBinaryCodec.hpp"
#include "persistence/ProjectChunkMigration.hpp"
#include "state/project/ProjectDomainRules.hpp"

namespace core::persistence::project_state_codec {

namespace {

namespace project_file = core::persistence::project_file;
namespace migration = core::persistence::project_migration;
namespace binary = core::persistence::binary_codec;

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

template <typename Payload, uint32_t PayloadSize>
FLASHMEM bool readPayload(const project_file::DecodedChunkView* chunk,
                          Payload& out,
                          bool (*decodePayload)(const uint8_t*, uint32_t, Payload&),
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
         chunk->versionMinor < PROJECT_STATE_CHUNK_VERSION_MINOR);
    if (needsMigration) {
        std::array<uint8_t, PayloadSize> migratedBytes{};
        const auto migrated = migration::migrateToCurrent(
            *chunk,
            migratedBytes.data(),
            static_cast<uint32_t>(migratedBytes.size())
        );
        switch (migrated.status) {
            case migration::Status::MIGRATED:
                addReport(report,
                          project_file::LoadSeverity::INFO,
                          project_file::LoadCode::MIGRATED_CHUNK,
                          chunk->id,
                          chunk->versionMajor,
                          chunk->versionMinor);
                return migrated.bytesWritten == PayloadSize &&
                       decodePayload(migratedBytes.data(), PayloadSize, out);
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
    if (chunk->size != PayloadSize || chunk->data == nullptr) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        return false;
    }
    return decodePayload(chunk->data, chunk->size, out);
}

template <typename Payload, uint32_t PayloadSize>
FLASHMEM bool applyOptionalChunk(const project_file::DecodedChunkView* chunks,
                                 uint16_t count,
                                 project_file::ChunkId id,
                                 project_file::LoadReport* report,
                                 bool (*decodePayload)(const uint8_t*, uint32_t, Payload&),
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
    if (!readPayload<Payload, PayloadSize>(chunk, payload, decodePayload, report)) {
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

FLASHMEM void applyEditingToProject(const ProjectEditingPayload& payload,
                                    core::state::project::ProjectState& target) {
    applyEditingPayload(payload, target.editing);
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

FLASHMEM bool encodeMetaPayload(const ProjectMetaPayload& payload,
                                uint8_t* out,
                                uint32_t outCapacity) {
    if (outCapacity != PROJECT_META_PAYLOAD_SIZE) return false;
    binary::Writer writer(out, outCapacity);
    return writer.writeBytes(payload.id, sizeof(payload.id)) &&
           writer.writeBytes(payload.name, sizeof(payload.name)) &&
           writer.writeU32(payload.modifiedCounter) &&
           writer.writeU8(payload.flags) &&
           writer.writeU8(0) &&
           writer.writeU16(0) &&
           writer.ok() &&
           writer.offset() == PROJECT_META_PAYLOAD_SIZE;
}

FLASHMEM bool decodeMetaPayload(const uint8_t* data,
                                uint32_t size,
                                ProjectMetaPayload& out) {
    if (size != PROJECT_META_PAYLOAD_SIZE) return false;
    binary::Reader reader(data, size);
    return reader.readBytes(out.id, sizeof(out.id)) &&
           reader.readBytes(out.name, sizeof(out.name)) &&
           reader.readU32(out.modifiedCounter) &&
           reader.readU8(out.flags) &&
           reader.skip(3) &&
           reader.ok() &&
           reader.offset() == PROJECT_META_PAYLOAD_SIZE;
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

FLASHMEM bool encodeTransportPayload(const ProjectTransportPayload& payload,
                                     uint8_t* out,
                                     uint32_t outCapacity) {
    if (outCapacity != PROJECT_TRANSPORT_PAYLOAD_SIZE) return false;
    binary::Writer writer(out, outCapacity);
    return writer.writeU16(payload.tempoCentiBpm) &&
           writer.writeU8(payload.swingPercent) &&
           writer.writeU8(payload.runMode) &&
           writer.writeU32(0) &&
           writer.ok() &&
           writer.offset() == PROJECT_TRANSPORT_PAYLOAD_SIZE;
}

FLASHMEM bool decodeTransportPayload(const uint8_t* data,
                                     uint32_t size,
                                     ProjectTransportPayload& out) {
    if (size != PROJECT_TRANSPORT_PAYLOAD_SIZE) return false;
    binary::Reader reader(data, size);
    return reader.readU16(out.tempoCentiBpm) &&
           reader.readU8(out.swingPercent) &&
           reader.readU8(out.runMode) &&
           reader.skip(4) &&
           reader.ok() &&
           reader.offset() == PROJECT_TRANSPORT_PAYLOAD_SIZE;
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

FLASHMEM bool encodeMusicalContextPayload(const ProjectMusicalContextPayload& payload,
                                          uint8_t* out,
                                          uint32_t outCapacity) {
    if (outCapacity != PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE) return false;
    binary::Writer writer(out, outCapacity);
    return writer.writeU8(payload.scaleRoot) &&
           writer.writeU8(payload.scaleType) &&
           writer.writeU8(payload.scaleConstraintMode) &&
           writer.writeU8(payload.flags) &&
           writer.writeU32(0) &&
           writer.ok() &&
           writer.offset() == PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE;
}

FLASHMEM bool decodeMusicalContextPayload(const uint8_t* data,
                                          uint32_t size,
                                          ProjectMusicalContextPayload& out) {
    if (size != PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE) return false;
    binary::Reader reader(data, size);
    return reader.readU8(out.scaleRoot) &&
           reader.readU8(out.scaleType) &&
           reader.readU8(out.scaleConstraintMode) &&
           reader.readU8(out.flags) &&
           reader.skip(4) &&
           reader.ok() &&
           reader.offset() == PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE;
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

FLASHMEM bool encodeRoutingPayload(const ProjectRoutingPayload& payload,
                                   uint8_t* out,
                                   uint32_t outCapacity) {
    if (outCapacity != PROJECT_ROUTING_PAYLOAD_SIZE) return false;
    binary::Writer writer(out, outCapacity);
    return writer.writeBytes(payload.outputMidiChannels,
                             PROJECT_ROUTING_PAYLOAD_SIZE) &&
           writer.ok() &&
           writer.offset() == PROJECT_ROUTING_PAYLOAD_SIZE;
}

FLASHMEM bool decodeRoutingPayload(const uint8_t* data,
                                   uint32_t size,
                                   ProjectRoutingPayload& out) {
    if (size != PROJECT_ROUTING_PAYLOAD_SIZE) return false;
    binary::Reader reader(data, size);
    return reader.readBytes(out.outputMidiChannels, PROJECT_ROUTING_PAYLOAD_SIZE) &&
           reader.ok() &&
           reader.offset() == PROJECT_ROUTING_PAYLOAD_SIZE;
}

FLASHMEM void applyRoutingPayload(const ProjectRoutingPayload& payload,
                                  core::state::project::ProjectRoutingState& target) {
    for (uint8_t i = 0; i < target.outputMidiChannels.size(); ++i) {
        target.outputMidiChannels[i] =
            core::state::project::sanitizeProjectMidiChannel(payload.outputMidiChannels[i]);
    }
}

FLASHMEM void fillEditingPayload(const core::state::project::ProjectEditingState& source,
                                 ProjectEditingPayload& out) {
    out.stepPasteMode = static_cast<uint8_t>(
        core::state::project::sanitizeProjectStepPasteMode(source.stepPasteMode)
    );
}

FLASHMEM bool encodeEditingPayload(const ProjectEditingPayload& payload,
                                   uint8_t* out,
                                   uint32_t outCapacity) {
    if (outCapacity != PROJECT_EDITING_PAYLOAD_SIZE) return false;
    binary::Writer writer(out, outCapacity);
    return writer.writeU8(payload.stepPasteMode) &&
           writer.writeU8(0) &&
           writer.writeU16(0) &&
           writer.writeU32(0) &&
           writer.ok() &&
           writer.offset() == PROJECT_EDITING_PAYLOAD_SIZE;
}

FLASHMEM bool decodeEditingPayload(const uint8_t* data,
                                   uint32_t size,
                                   ProjectEditingPayload& out) {
    if (size != PROJECT_EDITING_PAYLOAD_SIZE) return false;
    binary::Reader reader(data, size);
    return reader.readU8(out.stepPasteMode) &&
           reader.skip(7) &&
           reader.ok() &&
           reader.offset() == PROJECT_EDITING_PAYLOAD_SIZE;
}

FLASHMEM void applyEditingPayload(const ProjectEditingPayload& payload,
                                  core::state::project::ProjectEditingState& target) {
    target.stepPasteMode =
        core::state::project::sanitizeProjectStepPasteMode(payload.stepPasteMode);
}

FLASHMEM void applyProjectStateChunks(const project_file::DecodedChunkView* chunks,
                                      uint16_t chunkCount,
                                      core::state::project::ProjectState& target,
                                      project_file::LoadReport* report) {
    applyOptionalChunk<ProjectMetaPayload, PROJECT_META_PAYLOAD_SIZE>(
        chunks,
        chunkCount,
        project_file::ChunkId::PROJECT_META,
        report,
        decodeMetaPayload,
        applyMetaToProject,
        target
    );
    applyOptionalChunk<ProjectTransportPayload, PROJECT_TRANSPORT_PAYLOAD_SIZE>(
        chunks,
        chunkCount,
        project_file::ChunkId::TRANSPORT,
        report,
        decodeTransportPayload,
        applyTransportToProject,
        target
    );
    applyOptionalChunk<ProjectMusicalContextPayload, PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE>(
        chunks,
        chunkCount,
        project_file::ChunkId::MUSICAL_CONTEXT,
        report,
        decodeMusicalContextPayload,
        applyMusicalToProject,
        target
    );
    applyOptionalChunk<ProjectRoutingPayload, PROJECT_ROUTING_PAYLOAD_SIZE>(
        chunks,
        chunkCount,
        project_file::ChunkId::ROUTING,
        report,
        decodeRoutingPayload,
        applyRoutingToProject,
        target
    );
    applyOptionalChunk<ProjectEditingPayload, PROJECT_EDITING_PAYLOAD_SIZE>(
        chunks,
        chunkCount,
        project_file::ChunkId::EDITING,
        report,
        decodeEditingPayload,
        applyEditingToProject,
        target
    );
}

}  // namespace core::persistence::project_state_codec

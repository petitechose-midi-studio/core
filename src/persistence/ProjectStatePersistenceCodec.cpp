#include "persistence/ProjectStatePersistenceCodec.hpp"

#include <cmath>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceBinaryCodec.hpp"
#include "state/project/ProjectDomainRules.hpp"
#include "state/project/ProjectSlug.hpp"

namespace core::persistence::project_state_codec {

namespace {

namespace project_file = core::persistence::project_file;
namespace binary = core::persistence::binary_codec;

constexpr uint8_t kMetaFlagDirty = 1U << 0U;
constexpr uint8_t kMetaFlagHasSavedIdentity = 1U << 1U;
constexpr uint8_t kMetaKnownFlags =
    kMetaFlagDirty | kMetaFlagHasSavedIdentity;
constexpr uint8_t kMusicalFlagPatternsInheritScale = 1U << 0U;
constexpr uint8_t kMusicalFlagClipsInheritScale = 1U << 1U;
constexpr uint8_t kMusicalKnownFlags =
    kMusicalFlagPatternsInheritScale | kMusicalFlagClipsInheritScale;
FLASHMEM float centiBpmToTempo(uint16_t centiBpm) {
    return static_cast<float>(centiBpm) / 100.0F;
}

FLASHMEM void copyFixedText(const char* source, char* target, size_t size) {
    if (target == nullptr || size == 0) return;
    std::memset(target, 0, size);
    if (source == nullptr) return;
    std::strncpy(target, source, size - 1U);
}

FLASHMEM bool fixedSlugCanonical(const char* text, size_t capacity) {
    if (text == nullptr || capacity == 0U) return false;
    size_t length = 0U;
    while (length < capacity && text[length] != '\0') ++length;
    if (length == capacity ||
        !core::state::project::validProjectSlug(text)) {
        return false;
    }
    for (size_t index = length + 1U; index < capacity; ++index) {
        if (text[index] != '\0') return false;
    }
    return true;
}

FLASHMEM bool fixedEmptyTextCanonical(const char* text, size_t capacity) {
    if (text == nullptr || capacity == 0U || text[0] != '\0') return false;
    for (size_t index = 1U; index < capacity; ++index) {
        if (text[index] != '\0') return false;
    }
    return true;
}

FLASHMEM bool metaPayloadCanonical(const ProjectMetaPayload& payload) {
    if ((payload.flags & static_cast<uint8_t>(~kMetaKnownFlags)) != 0U ||
        !fixedSlugCanonical(payload.name, sizeof(payload.name))) {
        return false;
    }
    return (payload.flags & kMetaFlagHasSavedIdentity) != 0U
        ? fixedSlugCanonical(payload.id, sizeof(payload.id))
        : fixedEmptyTextCanonical(payload.id, sizeof(payload.id));
}

FLASHMEM bool transportPayloadCanonical(
    const ProjectTransportPayload& payload
) {
    constexpr uint16_t minimumTempo =
        static_cast<uint16_t>(
            core::state::project::PROJECT_TEMPO_MIN_BPM * 100.0F
        );
    constexpr uint16_t maximumTempo =
        static_cast<uint16_t>(
            core::state::project::PROJECT_TEMPO_MAX_BPM * 100.0F
        );
    return payload.tempoCentiBpm >= minimumTempo &&
           payload.tempoCentiBpm <= maximumTempo &&
           payload.swingPercent <=
               core::state::project::PROJECT_SWING_MAX_PERCENT &&
           payload.runMode < core::state::project::PROJECT_RUN_MODE_COUNT;
}

FLASHMEM bool tempoToCanonicalCentiBpm(
    float tempoBpm,
    uint16_t& centiBpm
) {
    if (!std::isfinite(tempoBpm) ||
        tempoBpm < core::state::project::PROJECT_TEMPO_MIN_BPM ||
        tempoBpm > core::state::project::PROJECT_TEMPO_MAX_BPM) {
        return false;
    }

    const float scaled = tempoBpm * 100.0F;
    const auto rounded = static_cast<uint16_t>(scaled + 0.5F);
    const float restored = static_cast<float>(rounded) / 100.0F;
    if (std::fabs(restored - tempoBpm) > 0.0001F) return false;
    centiBpm = rounded;
    return true;
}

FLASHMEM bool musicalPayloadCanonical(
    const ProjectMusicalContextPayload& payload
) {
    return payload.scaleRoot < 12U &&
           payload.scaleType <= static_cast<uint8_t>(
               oc::note::sequencer::StepSequencerScaleType::WholeTone
           ) &&
           payload.scaleConstraintMode <= static_cast<uint8_t>(
               oc::note::sequencer::StepSequencerScaleConstraintMode::
                   ConstrainDown
           ) &&
           (payload.flags & static_cast<uint8_t>(~kMusicalKnownFlags)) == 0U;
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
    if (chunk->flags != 0U) {
        addReport(report,
                  project_file::LoadSeverity::WARNING,
                  project_file::LoadCode::UNSUPPORTED_CHUNK_FLAGS,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        return false;
    }
    if (chunk->versionMajor != PROJECT_STATE_CHUNK_VERSION_MAJOR ||
        chunk->versionMinor != PROJECT_STATE_CHUNK_VERSION_MINOR) {
        addReport(report,
                  project_file::LoadSeverity::WARNING,
                  project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        return false;
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
    if (!decodePayload(chunk->data, chunk->size, out)) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        return false;
    }
    return true;
}

template <typename Payload, uint32_t PayloadSize>
FLASHMEM bool applyRequiredChunk(const project_file::DecodedChunkView* chunks,
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
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::MISSING_REQUIRED_CHUNK,
                  project_file::chunkIdValue(id));
        return false;
    }

    Payload payload{};
    if (!readPayload<Payload, PayloadSize>(chunk, payload, decodePayload, report)) {
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

FLASHMEM void applyEditingToProject(const ProjectEditingPayload& payload,
                                    core::state::project::ProjectState& target) {
    applyEditingPayload(payload, target.editing);
}

}  // namespace

FLASHMEM bool fillMetaPayload(
    const core::state::project::ProjectMetadata& source,
    ProjectMetaPayload& out
) {
    ProjectMetaPayload pending{};
    std::memcpy(pending.id, source.id.data(), sizeof(pending.id));
    std::memcpy(pending.name, source.name.data(), sizeof(pending.name));
    pending.modifiedCounter = source.modifiedCounter;
    if (source.dirty) pending.flags |= kMetaFlagDirty;
    if (source.hasSavedIdentity) pending.flags |= kMetaFlagHasSavedIdentity;
    if (!metaPayloadCanonical(pending)) return false;
    out = pending;
    return true;
}

FLASHMEM bool encodeMetaPayload(const ProjectMetaPayload& payload,
                                uint8_t* out,
                                uint32_t outCapacity) {
    if (out == nullptr ||
        outCapacity != PROJECT_META_PAYLOAD_SIZE ||
        !metaPayloadCanonical(payload)) {
        return false;
    }
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
    if (data == nullptr || size != PROJECT_META_PAYLOAD_SIZE) return false;
    ProjectMetaPayload pending{};
    uint8_t reserved0 = 0U;
    uint16_t reserved1 = 0U;
    binary::Reader reader(data, size);
    if (!reader.readBytes(pending.id, sizeof(pending.id)) ||
        !reader.readBytes(pending.name, sizeof(pending.name)) ||
        !reader.readU32(pending.modifiedCounter) ||
        !reader.readU8(pending.flags) ||
        !reader.readU8(reserved0) ||
        !reader.readU16(reserved1) ||
        !reader.ok() ||
        reader.offset() != PROJECT_META_PAYLOAD_SIZE ||
        reserved0 != 0U ||
        reserved1 != 0U ||
        !metaPayloadCanonical(pending)) {
        return false;
    }
    out = pending;
    return true;
}

FLASHMEM void applyMetaPayload(const ProjectMetaPayload& payload,
                               core::state::project::ProjectMetadata& target) {
    copyFixedText(payload.id, target.id.data(), target.id.size());
    copyFixedText(payload.name, target.name.data(), target.name.size());
    target.modifiedCounter = payload.modifiedCounter;
    target.dirty = (payload.flags & kMetaFlagDirty) != 0;
    target.hasSavedIdentity = (payload.flags & kMetaFlagHasSavedIdentity) != 0;
}

FLASHMEM bool fillTransportPayload(
    const core::state::project::ProjectTransportState& source,
    ProjectTransportPayload& out
) {
    ProjectTransportPayload pending{};
    if (!tempoToCanonicalCentiBpm(source.tempoBpm, pending.tempoCentiBpm)) {
        return false;
    }
    pending.swingPercent = source.swingPercent;
    pending.runMode = source.runMode;
    if (!transportPayloadCanonical(pending)) return false;
    out = pending;
    return true;
}

FLASHMEM bool encodeTransportPayload(const ProjectTransportPayload& payload,
                                     uint8_t* out,
                                     uint32_t outCapacity) {
    if (out == nullptr ||
        outCapacity != PROJECT_TRANSPORT_PAYLOAD_SIZE ||
        !transportPayloadCanonical(payload)) {
        return false;
    }
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
    if (data == nullptr || size != PROJECT_TRANSPORT_PAYLOAD_SIZE) {
        return false;
    }
    ProjectTransportPayload pending{};
    uint32_t reserved = 0U;
    binary::Reader reader(data, size);
    if (!reader.readU16(pending.tempoCentiBpm) ||
        !reader.readU8(pending.swingPercent) ||
        !reader.readU8(pending.runMode) ||
        !reader.readU32(reserved) ||
        !reader.ok() ||
        reader.offset() != PROJECT_TRANSPORT_PAYLOAD_SIZE ||
        reserved != 0U ||
        !transportPayloadCanonical(pending)) {
        return false;
    }
    out = pending;
    return true;
}

FLASHMEM void applyTransportPayload(const ProjectTransportPayload& payload,
                                    core::state::project::ProjectTransportState& target) {
    target.tempoBpm = centiBpmToTempo(payload.tempoCentiBpm);
    target.swingPercent = payload.swingPercent;
    target.runMode = payload.runMode;
}

FLASHMEM bool fillMusicalContextPayload(
    const core::state::project::ProjectMusicalContext& source,
    ProjectMusicalContextPayload& out
) {
    ProjectMusicalContextPayload pending{};
    pending.scaleRoot = source.scale.root;
    pending.scaleType = static_cast<uint8_t>(source.scale.type);
    pending.scaleConstraintMode = static_cast<uint8_t>(source.scale.mode);
    if (source.patternsInheritScale) {
        pending.flags |= kMusicalFlagPatternsInheritScale;
    }
    if (source.clipsInheritScale) {
        pending.flags |= kMusicalFlagClipsInheritScale;
    }
    if (!musicalPayloadCanonical(pending)) return false;
    out = pending;
    return true;
}

FLASHMEM bool encodeMusicalContextPayload(const ProjectMusicalContextPayload& payload,
                                          uint8_t* out,
                                          uint32_t outCapacity) {
    if (out == nullptr ||
        outCapacity != PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE ||
        !musicalPayloadCanonical(payload)) {
        return false;
    }
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
    if (data == nullptr || size != PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE) {
        return false;
    }
    ProjectMusicalContextPayload pending{};
    uint32_t reserved = 0U;
    binary::Reader reader(data, size);
    if (!reader.readU8(pending.scaleRoot) ||
        !reader.readU8(pending.scaleType) ||
        !reader.readU8(pending.scaleConstraintMode) ||
        !reader.readU8(pending.flags) ||
        !reader.readU32(reserved) ||
        !reader.ok() ||
        reader.offset() != PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE ||
        reserved != 0U ||
        !musicalPayloadCanonical(pending)) {
        return false;
    }
    out = pending;
    return true;
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
    target.patternsInheritScale = (payload.flags & kMusicalFlagPatternsInheritScale) != 0;
    target.clipsInheritScale = (payload.flags & kMusicalFlagClipsInheritScale) != 0;
}

FLASHMEM bool fillEditingPayload(
    const core::state::project::ProjectEditingState& source,
    ProjectEditingPayload& out
) {
    ProjectEditingPayload pending{};
    pending.stepPasteMode = static_cast<uint8_t>(source.stepPasteMode);
    pending.ccLaneDefaultsMarker = PROJECT_EDITING_CC_LANE_DEFAULTS_MARKER;
    for (uint8_t lane = 0;
         lane < core::state::project::PROJECT_CC_LANE_DEFAULT_COUNT;
         ++lane) {
        pending.ccLaneDefaultControllers[lane] =
            source.ccLaneDefaultControllers[lane];
    }
    if (pending.stepPasteMode >=
        core::state::project::PROJECT_STEP_PASTE_MODE_COUNT) {
        return false;
    }
    for (const uint8_t controller : pending.ccLaneDefaultControllers) {
        if (!core::state::project::validProjectMidiCc(controller)) return false;
    }
    out = pending;
    return true;
}

FLASHMEM bool encodeEditingPayload(const ProjectEditingPayload& payload,
                                   uint8_t* out,
                                   uint32_t outCapacity) {
    if (out == nullptr || outCapacity != PROJECT_EDITING_PAYLOAD_SIZE ||
        payload.stepPasteMode >=
            core::state::project::PROJECT_STEP_PASTE_MODE_COUNT ||
        payload.ccLaneDefaultsMarker !=
            PROJECT_EDITING_CC_LANE_DEFAULTS_MARKER) {
        return false;
    }
    for (const uint8_t controller : payload.ccLaneDefaultControllers) {
        if (!core::state::project::validProjectMidiCc(controller)) return false;
    }
    binary::Writer writer(out, outCapacity);
    return writer.writeU8(payload.stepPasteMode) &&
           writer.writeU8(payload.ccLaneDefaultsMarker) &&
           writer.writeBytes(
               payload.ccLaneDefaultControllers,
               core::state::project::PROJECT_CC_LANE_DEFAULT_COUNT
           ) &&
           writer.writeU16(0) &&
           writer.ok() &&
           writer.offset() == PROJECT_EDITING_PAYLOAD_SIZE;
}

FLASHMEM bool decodeEditingPayload(const uint8_t* data,
                                   uint32_t size,
                                   ProjectEditingPayload& out) {
    if (data == nullptr || size != PROJECT_EDITING_PAYLOAD_SIZE) return false;
    ProjectEditingPayload pending{};
    uint16_t reserved = 0;
    binary::Reader reader(data, size);
    if (!reader.readU8(pending.stepPasteMode) ||
        !reader.readU8(pending.ccLaneDefaultsMarker) ||
        !reader.readBytes(
            pending.ccLaneDefaultControllers,
            core::state::project::PROJECT_CC_LANE_DEFAULT_COUNT
        ) ||
        !reader.readU16(reserved) || !reader.ok() ||
        reader.offset() != PROJECT_EDITING_PAYLOAD_SIZE || reserved != 0U ||
        pending.stepPasteMode >=
            core::state::project::PROJECT_STEP_PASTE_MODE_COUNT) {
        return false;
    }

    if (pending.ccLaneDefaultsMarker !=
        PROJECT_EDITING_CC_LANE_DEFAULTS_MARKER) {
        return false;
    }
    for (const uint8_t controller : pending.ccLaneDefaultControllers) {
        if (!core::state::project::validProjectMidiCc(controller)) return false;
    }

    out = pending;
    return true;
}

FLASHMEM void applyEditingPayload(const ProjectEditingPayload& payload,
                                  core::state::project::ProjectEditingState& target) {
    target.stepPasteMode =
        static_cast<core::state::project::ProjectStepPasteMode>(
            payload.stepPasteMode
        );
    for (uint8_t lane = 0;
         lane < core::state::project::PROJECT_CC_LANE_DEFAULT_COUNT;
         ++lane) {
        target.ccLaneDefaultControllers[lane] =
            payload.ccLaneDefaultControllers[lane];
    }
}

FLASHMEM bool applyProjectStateChunks(const project_file::DecodedChunkView* chunks,
                                      uint16_t chunkCount,
                                      core::state::project::ProjectState& target,
                                      project_file::LoadReport* report) {
    auto pending = target;
    bool valid = true;
    valid = applyRequiredChunk<ProjectMetaPayload, PROJECT_META_PAYLOAD_SIZE>(
        chunks,
        chunkCount,
        project_file::ChunkId::PROJECT_META,
        report,
        decodeMetaPayload,
        applyMetaToProject,
        pending
    ) && valid;
    valid = applyRequiredChunk<ProjectTransportPayload, PROJECT_TRANSPORT_PAYLOAD_SIZE>(
        chunks,
        chunkCount,
        project_file::ChunkId::TRANSPORT,
        report,
        decodeTransportPayload,
        applyTransportToProject,
        pending
    ) && valid;
    valid = applyRequiredChunk<ProjectMusicalContextPayload, PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE>(
        chunks,
        chunkCount,
        project_file::ChunkId::MUSICAL_CONTEXT,
        report,
        decodeMusicalContextPayload,
        applyMusicalToProject,
        pending
    ) && valid;
    valid = applyRequiredChunk<ProjectEditingPayload, PROJECT_EDITING_PAYLOAD_SIZE>(
        chunks,
        chunkCount,
        project_file::ChunkId::EDITING,
        report,
        decodeEditingPayload,
        applyEditingToProject,
        pending
    ) && valid;
    if (valid) target = pending;
    return valid;
}

}  // namespace core::persistence::project_state_codec

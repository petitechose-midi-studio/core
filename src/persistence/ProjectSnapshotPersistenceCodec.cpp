#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

#include <array>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/LegacyMacroAutomationPersistenceCodec.hpp"
#include "persistence/MacroTrackBankPersistenceCodec.hpp"
#include "persistence/ProjectStatePersistenceCodec.hpp"
#include "persistence/SequencerPersistenceEnvelope.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::persistence::project_snapshot_codec {

struct ProjectSnapshotCodecWorkspace::Storage {
    std::array<uint8_t, PROJECT_MACRO_STATE_PAYLOAD_SIZE> macro;
    std::array<uint8_t, PROJECT_MACRO_AUTOMATION_MAX_PAYLOAD_SIZE> macroAutomation;
    sequencer_codec::EnvelopeBuffer sequencerEnvelope;
};

ProjectSnapshotCodecWorkspace::ProjectSnapshotCodecWorkspace() = default;
ProjectSnapshotCodecWorkspace::~ProjectSnapshotCodecWorkspace() = default;
ProjectSnapshotCodecWorkspace::ProjectSnapshotCodecWorkspace(
    ProjectSnapshotCodecWorkspace&&
) noexcept = default;
ProjectSnapshotCodecWorkspace& ProjectSnapshotCodecWorkspace::operator=(
    ProjectSnapshotCodecWorkspace&&
) noexcept = default;

FLASHMEM bool ProjectSnapshotCodecWorkspace::prepare() {
    if (!storage_) {
        storage_ = core::app::makeExtmemUniqueForOverwrite<Storage>();
    }
    return static_cast<bool>(storage_);
}

namespace {

namespace project_file = core::persistence::project_file;
namespace project_state_codec = core::persistence::project_state_codec;
namespace macro_track_codec = core::persistence::macro_track_codec;
namespace sequencer_codec = core::persistence::sequencer_codec;
namespace legacy_macro_codec =
    core::persistence::macro_automation_legacy_codec;
namespace macro = core::state::macro;

FLASHMEM void resetMacroTracks(core::state::project::ProjectSnapshot& target) {
    for (uint8_t i = 0; i < target.macroTracks.size(); ++i) {
        target.macroTracks[i].initDefaults(i);
    }
    target.sharedTrackEnabledMask = macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;
    target.sharedTrackActive = 0;
}

FLASHMEM void addReport(project_file::LoadReport* report,
                        project_file::LoadSeverity severity,
                        project_file::LoadCode code,
                        uint32_t chunkId,
                        uint8_t sourceMajor = 0,
                        uint8_t sourceMinor = 0,
                        uint8_t targetMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
                        uint8_t targetMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR) {
    if (report == nullptr) return;
    report->add(severity,
                code,
                chunkId,
                sourceMajor,
                sourceMinor,
                targetMajor,
                targetMinor);
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
                                    uint8_t expectedMinor,
                                    project_file::LoadReport* report) {
    if (chunk.versionMajor == PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR &&
        chunk.versionMinor == expectedMinor) {
        return true;
    }

    addReport(report,
              project_file::LoadSeverity::WARNING,
              project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION,
              chunk.id,
              chunk.versionMajor,
              chunk.versionMinor,
              PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
              expectedMinor);
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

FLASHMEM bool readMacroChunk(const project_file::DecodedChunkView* chunk,
                             core::state::project::ProjectSnapshot& target,
                             project_file::LoadReport* report) {
    if (chunk == nullptr) {
        reportMissingOptional(report, project_file::ChunkId::MACRO_STATE);
        return true;
    }
    if (!chunkVersionSupported(*chunk, PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR, report)) {
        reportDefaulted(report, project_file::ChunkId::MACRO_STATE);
        return false;
    }
    if (chunk->size != PROJECT_MACRO_STATE_PAYLOAD_SIZE || chunk->data == nullptr) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        reportDefaulted(report, project_file::ChunkId::MACRO_STATE);
        return false;
    }

    uint16_t enabledTrackMask = macro::MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;
    uint8_t activeTrack = 0;
    if (!macro_track_codec::decodeTrackBankPayloadInto(
            chunk->data,
            chunk->size,
            target.macroTracks,
            enabledTrackMask,
            activeTrack
        )) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        reportDefaulted(report, project_file::ChunkId::MACRO_STATE);
        resetMacroTracks(target);
        return false;
    }

    target.sharedTrackActive = activeTrack;
    target.sharedTrackEnabledMask = enabledTrackMask;
    return true;
}

FLASHMEM bool readMacroAutomationChunk(const project_file::DecodedChunkView* chunk,
                                       core::state::project::ProjectSnapshot& target,
                                       project_file::LoadReport* report) {
    if (!target.macroAutomation) {
        reportDefaulted(report, project_file::ChunkId::MACRO_AUTOMATION);
        return false;
    }
    if (chunk == nullptr) {
        reportMissingOptional(report, project_file::ChunkId::MACRO_AUTOMATION);
        target.macroAutomation->clear();
        return true;
    }
    const bool currentVersion =
        chunk->versionMajor == PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR &&
        chunk->versionMinor == PROJECT_MACRO_AUTOMATION_CHUNK_VERSION_MINOR;
    const bool legacyV14 =
        chunk->versionMajor == PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR &&
        chunk->versionMinor == PROJECT_MACRO_AUTOMATION_LEGACY_CHUNK_VERSION_MINOR;
    if (!currentVersion && !legacyV14) {
        addReport(report,
                  project_file::LoadSeverity::WARNING,
                  project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor,
                  PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
                  PROJECT_MACRO_AUTOMATION_CHUNK_VERSION_MINOR);
        reportDefaulted(report, project_file::ChunkId::MACRO_AUTOMATION);
        target.macroAutomation->clear();
        return false;
    }
    if (!legacy_macro_codec::decodeIntoPending(
            chunk->data,
            chunk->size,
            chunk->versionMinor,
            *target.macroAutomation
        )) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
        reportDefaulted(report, project_file::ChunkId::MACRO_AUTOMATION);
        target.macroAutomation->clear();
        return false;
    }
    if (legacyV14) {
        addReport(report,
                  project_file::LoadSeverity::INFO,
                  project_file::LoadCode::MIGRATED_CHUNK,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor,
                  PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
                  PROJECT_MACRO_AUTOMATION_CHUNK_VERSION_MINOR);
    }
    core::state::macro::macroAutomationCompactPool(*target.macroAutomation);
    return true;
}

FLASHMEM bool buildSequencerEnvelope(
    const core::state::project::ProjectSnapshot& snapshot,
    sequencer_codec::EnvelopeBuffer& out,
    uint32_t& outSize
) {
    OC_PERF_SCOPE(perfEnvelope, "persistence.project-codec.sequencer");

    sequencer_codec::ProjectSequencerSnapshotEncodeSource source{};
    source.flat = &snapshot.sequencer.flat;
    source.focusedStep = snapshot.sequencer.focusedStep;
    source.activeStepProperty = snapshot.sequencer.activeStepProperty;
    const uint8_t activeTrack =
        core::state::sequencer::SequencerTrackBankState::sanitizeActiveTrack(
            snapshot.sequencer.flat.enabledMask,
            snapshot.sequencer.flat.activeTrack
        );
    for (uint8_t i = 0; i < source.graphs.size(); ++i) {
        source.graphs[i] = (i == activeTrack)
            ? snapshot.sequencer.editorGraph.get()
            : snapshot.sequencer.bankGraphs[i].get();
        source.ccLanes[i] = (i == activeTrack)
            ? snapshot.sequencer.editorCcLanes.get()
            : snapshot.sequencer.bankCcLanes[i].get();
    }

    const auto encoded = sequencer_codec::fillProjectSequencerEnvelope(
        source,
        out.bytes.data(),
        static_cast<uint32_t>(out.bytes.size())
    );
    if (!encoded.ok) return false;
    outSize = encoded.size;
    OC_PERF_UNITS(perfEnvelope, outSize, source.graphs.size());
    return true;
}

FLASHMEM bool readSequencerChunk(const project_file::DecodedChunkView* chunk,
                                 core::state::project::ProjectSnapshot& target,
                                 project_file::LoadReport* report) {
    if (chunk == nullptr) {
        reportMissingOptional(report, project_file::ChunkId::SEQUENCER_STATE);
        return true;
    }
    if (!chunkVersionSupported(*chunk, PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR, report)) {
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
            chunk->size,
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
    uint32_t outCapacity,
    ProjectSnapshotCodecWorkspace& workspace
) {
    if (!snapshot.macroAutomation) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }
    if (!workspace.prepare()) {
        return {.status = project_file::Status::SCRATCH_ALLOCATION_FAILED, .bytesWritten = 0};
    }
    OC_PERF_SCOPE(perfEncode, "persistence.project-codec.encode");

    project_state_codec::ProjectMetaPayload meta{};
    project_state_codec::ProjectTransportPayload transport{};
    project_state_codec::ProjectMusicalContextPayload musical{};
    project_state_codec::ProjectRoutingPayload routing{};
    project_state_codec::ProjectEditingPayload editing{};
    project_state_codec::fillMetaPayload(snapshot.project.metadata, meta);
    project_state_codec::fillTransportPayload(snapshot.project.transport, transport);
    project_state_codec::fillMusicalContextPayload(snapshot.project.musical, musical);
    project_state_codec::fillRoutingPayload(snapshot.project.routing, routing);
    project_state_codec::fillEditingPayload(snapshot.project.editing, editing);

    std::array<uint8_t, project_state_codec::PROJECT_META_PAYLOAD_SIZE> metaBytes{};
    std::array<uint8_t, project_state_codec::PROJECT_TRANSPORT_PAYLOAD_SIZE> transportBytes{};
    std::array<uint8_t, project_state_codec::PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE> musicalBytes{};
    std::array<uint8_t, project_state_codec::PROJECT_ROUTING_PAYLOAD_SIZE> routingBytes{};
    std::array<uint8_t, project_state_codec::PROJECT_EDITING_PAYLOAD_SIZE> editingBytes{};
    if (!project_state_codec::encodeMetaPayload(
            meta,
            metaBytes.data(),
            static_cast<uint32_t>(metaBytes.size())
        ) ||
        !project_state_codec::encodeTransportPayload(
            transport,
            transportBytes.data(),
            static_cast<uint32_t>(transportBytes.size())
        ) ||
        !project_state_codec::encodeMusicalContextPayload(
            musical,
            musicalBytes.data(),
            static_cast<uint32_t>(musicalBytes.size())
        ) ||
        !project_state_codec::encodeRoutingPayload(
            routing,
            routingBytes.data(),
            static_cast<uint32_t>(routingBytes.size())
        ) ||
        !project_state_codec::encodeEditingPayload(
            editing,
            editingBytes.data(),
            static_cast<uint32_t>(editingBytes.size())
        )) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }

    auto& scratch = *workspace.storage_;

    if (!macro_track_codec::encodeTrackBankPayload(
            snapshot.macroTracks,
            snapshot.sharedTrackEnabledMask,
            snapshot.sharedTrackActive,
            scratch.macro.data(),
            static_cast<uint32_t>(scratch.macro.size())
        )) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }
    uint32_t macroAutomationSize = 0;
    if (!snapshot.macroAutomation ||
        !legacy_macro_codec::encodeV15(
            *snapshot.macroAutomation,
            scratch.macroAutomation.data(),
            static_cast<uint32_t>(scratch.macroAutomation.size()),
            macroAutomationSize
        )) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }
    uint32_t sequencerSize = 0;
    if (!buildSequencerEnvelope(
            snapshot,
            scratch.sequencerEnvelope,
            sequencerSize
        )) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }

    const project_file::ChunkView chunks[] = {
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::PROJECT_META),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = metaBytes.data(),
            .size = project_state_codec::PROJECT_META_PAYLOAD_SIZE,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::TRANSPORT),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = transportBytes.data(),
            .size = project_state_codec::PROJECT_TRANSPORT_PAYLOAD_SIZE,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::MUSICAL_CONTEXT),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = musicalBytes.data(),
            .size = project_state_codec::PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::ROUTING),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = routingBytes.data(),
            .size = project_state_codec::PROJECT_ROUTING_PAYLOAD_SIZE,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::EDITING),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = editingBytes.data(),
            .size = project_state_codec::PROJECT_EDITING_PAYLOAD_SIZE,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::MACRO_STATE),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = scratch.macro.data(),
            .size = PROJECT_MACRO_STATE_PAYLOAD_SIZE,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::MACRO_AUTOMATION),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_MACRO_AUTOMATION_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = scratch.macroAutomation.data(),
            .size = macroAutomationSize,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::SEQUENCER_STATE),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = scratch.sequencerEnvelope.bytes.data(),
            .size = sequencerSize,
        },
    };

    auto encoded = project_file::encode(
        chunks,
        static_cast<uint16_t>(sizeof(chunks) / sizeof(chunks[0])),
        snapshot.project.metadata.modifiedCounter,
        out,
        outCapacity
    );
    OC_PERF_UNITS(perfEncode, encoded.bytesWritten, sequencerSize);
    return encoded;
}

FLASHMEM project_file::EncodeResult encodeProjectSnapshot(
    const core::state::project::ProjectSnapshot& snapshot,
    uint8_t* out,
    uint32_t outCapacity
) {
    ProjectSnapshotCodecWorkspace workspace;
    if (!workspace.prepare()) {
        return {.status = project_file::Status::SCRATCH_ALLOCATION_FAILED, .bytesWritten = 0};
    }
    return encodeProjectSnapshot(snapshot, out, outCapacity, workspace);
}

FLASHMEM DecodeResult decodeProjectSnapshot(
    const uint8_t* data,
    uint32_t size,
    core::state::project::ProjectSnapshot& out,
    project_file::LoadReport* report
) {
    project_file::LoadReport localReport{};
    auto* effectiveReport = report != nullptr ? report : &localReport;
    project_file::DecodedChunkView chunks[project_file::MAX_CHUNKS] = {};
    auto decodeResult = project_file::decode(
        data,
        size,
        chunks,
        project_file::MAX_CHUNKS,
        effectiveReport
    );
    if (decodeResult.status != project_file::Status::OK) {
        return {
            .ok = false,
            .containerStatus = decodeResult.status,
            .loadStatus = effectiveReport->status,
            .overwriteSafe = false,
        };
    }

    auto next = core::state::project::makeProjectSnapshot();
    if (!next) {
        return {
            .ok = false,
            .containerStatus = project_file::Status::SCRATCH_ALLOCATION_FAILED,
            .loadStatus = project_file::LoadStatus::FAILED,
            .overwriteSafe = false,
        };
    }
    project_state_codec::applyProjectStateChunks(
        chunks,
        decodeResult.chunkCount,
        next->project,
        effectiveReport
    );
    readMacroChunk(findChunk(chunks, decodeResult.chunkCount, project_file::ChunkId::MACRO_STATE),
                   *next,
                   effectiveReport);
    readMacroAutomationChunk(
        findChunk(chunks, decodeResult.chunkCount, project_file::ChunkId::MACRO_AUTOMATION),
        *next,
        effectiveReport
    );
    readSequencerChunk(
        findChunk(chunks, decodeResult.chunkCount, project_file::ChunkId::SEQUENCER_STATE),
        *next,
        effectiveReport
    );

    out = std::move(*next);
    const project_file::LoadStatus loadStatus = effectiveReport->status;
    const bool overwriteSafe = effectiveReport->overwriteSafe;
    return {
        .ok = loadStatus != project_file::LoadStatus::FAILED,
        .containerStatus = decodeResult.status,
        .loadStatus = loadStatus,
        .overwriteSafe = overwriteSafe,
    };
}

}  // namespace core::persistence::project_snapshot_codec

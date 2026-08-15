#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

#include <array>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "app/ExtmemAllocator.hpp"
#include "diagnostics/MemoryFootprintReporter.hpp"
#include "persistence/MacroTrackBankPersistenceCodec.hpp"
#include "persistence/ProjectControlPersistenceCodec.hpp"
#include "persistence/ProjectStatePersistenceCodec.hpp"
#include "persistence/ProjectTrackStatePersistenceCodec.hpp"
#include "persistence/SequencerPersistenceEnvelope.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::persistence::project_snapshot_codec {

struct ProjectSnapshotCodecWorkspace::Storage {
    std::array<
        uint8_t,
        core::persistence::project_track_codec::PROJECT_TRACK_STATE_PAYLOAD_SIZE
    > projectTracks;
    std::array<uint8_t, PROJECT_MACRO_STATE_PAYLOAD_SIZE> macro;
    std::array<uint8_t, PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE> projectControl;
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
    static_assert(sizeof(Storage) == 600295U, "project encode scratch ABI drift");
    if (!storage_) {
        storage_ = core::app::makeExtmemUniqueForOverwrite<Storage>();
    }
    return static_cast<bool>(storage_);
}

namespace {

namespace project_file = core::persistence::project_file;
namespace project_state_codec = core::persistence::project_state_codec;
namespace project_track_codec = core::persistence::project_track_codec;
namespace macro_track_codec = core::persistence::macro_track_codec;
namespace project_control_codec = core::persistence::project_control_codec;
namespace sequencer_codec = core::persistence::sequencer_codec;
namespace macro = core::state::macro;

constexpr std::array<project_file::ChunkId, 9> kCurrentProjectChunks{
    project_file::ChunkId::PROJECT_META,
    project_file::ChunkId::TRANSPORT,
    project_file::ChunkId::MUSICAL_CONTEXT,
    project_file::ChunkId::EDITING,
    project_file::ChunkId::TRACK_STATE,
    project_file::ChunkId::MACRO_STATE,
    project_file::ChunkId::MACRO_AUTOMATION,
    project_file::ChunkId::MODULATION_GRAPH,
    project_file::ChunkId::SEQUENCER_STATE,
};

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

FLASHMEM bool currentProjectChunkSetValid(
    const project_file::DecodedChunkView* chunks,
    uint16_t count,
    project_file::LoadReport* report
) {
    bool valid = true;
    for (const auto id : kCurrentProjectChunks) {
        if (findChunk(chunks, count, id) != nullptr) continue;
        addReport(
            report,
            project_file::LoadSeverity::WARNING,
            project_file::LoadCode::MISSING_REQUIRED_CHUNK,
            project_file::chunkIdValue(id)
        );
        valid = false;
    }
    for (uint16_t index = 0U; index < count; ++index) {
        const auto& chunk = chunks[index];
        bool expected = false;
        for (const auto id : kCurrentProjectChunks) {
            if (chunk.id == project_file::chunkIdValue(id)) {
                expected = true;
                break;
            }
        }
        if (!expected) {
            addReport(
                report,
                project_file::LoadSeverity::WARNING,
                project_file::LoadCode::UNEXPECTED_CHUNK,
                chunk.id,
                chunk.versionMajor,
                chunk.versionMinor
            );
            valid = false;
        }
        if (chunk.flags != 0U) {
            addReport(
                report,
                project_file::LoadSeverity::WARNING,
                project_file::LoadCode::UNSUPPORTED_CHUNK_FLAGS,
                chunk.id,
                chunk.versionMajor,
                chunk.versionMinor
            );
            valid = false;
        }
    }
    return valid;
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

FLASHMEM bool readMacroChunk(const project_file::DecodedChunkView* chunk,
                             core::state::project::ProjectSnapshot& target,
                             project_file::LoadReport* report) {
    OC_PERF_SCOPE(perfMacro, "persistence.project-codec.decode.macro");
    OC_PERF_UNITS(perfMacro, chunk != nullptr ? chunk->size : 0U, 0U);
    if (chunk == nullptr) {
        addReport(
            report,
            project_file::LoadSeverity::ERROR,
            project_file::LoadCode::MISSING_REQUIRED_CHUNK,
            project_file::chunkIdValue(project_file::ChunkId::MACRO_STATE)
        );
        return false;
    }
    if (!chunkVersionSupported(
            *chunk,
            PROJECT_MACRO_STATE_CHUNK_VERSION_MINOR,
            report
        )) {
        return false;
    }
    if (chunk->size != PROJECT_MACRO_STATE_PAYLOAD_SIZE || chunk->data == nullptr) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
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
        return false;
    }

    target.sharedTrackActive = activeTrack;
    target.sharedTrackEnabledMask = enabledTrackMask;
    return true;
}

FLASHMEM project_control_codec::ChunkPayloadView controlChunkView(
    const project_file::DecodedChunkView* chunk
) {
    if (chunk == nullptr) return {};
    return {
        .present = true,
        .versionMajor = chunk->versionMajor,
        .versionMinor = chunk->versionMinor,
        .flags = chunk->flags,
        .data = chunk->data,
        .size = chunk->size,
    };
}

FLASHMEM void reportControlChunkStatus(
    project_file::LoadReport* report,
    project_file::ChunkId id,
    const project_file::DecodedChunkView* chunk,
    project_control_codec::ChunkStatus status,
    uint8_t targetMinor
) {
    using ChunkStatus = project_control_codec::ChunkStatus;
    const uint32_t chunkId = project_file::chunkIdValue(id);
    const uint8_t sourceMajor = chunk != nullptr ? chunk->versionMajor : 0U;
    const uint8_t sourceMinor = chunk != nullptr ? chunk->versionMinor : 0U;
    switch (status) {
        case ChunkStatus::CURRENT:
            return;
        case ChunkStatus::MISSING:
            addReport(
                report,
                project_file::LoadSeverity::ERROR,
                project_file::LoadCode::MISSING_REQUIRED_CHUNK,
                chunkId,
                sourceMajor,
                sourceMinor,
                project_control_codec::PROJECT_CONTROL_CHUNK_VERSION_MAJOR,
                targetMinor
            );
            return;
        case ChunkStatus::UNSUPPORTED_VERSION:
            addReport(
                report,
                project_file::LoadSeverity::WARNING,
                project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION,
                chunkId,
                sourceMajor,
                sourceMinor,
                project_control_codec::PROJECT_CONTROL_CHUNK_VERSION_MAJOR,
                targetMinor
            );
            return;
        case ChunkStatus::CAPACITY_EXCEEDED:
            addReport(
                report,
                project_file::LoadSeverity::ERROR,
                project_file::LoadCode::OUTPUT_CAPACITY_EXCEEDED,
                chunkId,
                sourceMajor,
                sourceMinor,
                project_control_codec::PROJECT_CONTROL_CHUNK_VERSION_MAJOR,
                targetMinor
            );
            return;
        case ChunkStatus::INVALID_PAYLOAD:
        default:
            addReport(
                report,
                project_file::LoadSeverity::ERROR,
                project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                chunkId,
                sourceMajor,
                sourceMinor,
                project_control_codec::PROJECT_CONTROL_CHUNK_VERSION_MAJOR,
                targetMinor
            );
            return;
    }
}

FLASHMEM bool readProjectControlChunks(
    const project_file::DecodedChunkView* automation,
    const project_file::DecodedChunkView* modulation,
    core::state::project::ProjectSnapshot& target,
    project_file::LoadReport* report
) {
    OC_PERF_SCOPE(
        perfControl,
        "persistence.project-codec.decode.project-control"
    );
    OC_PERF_UNITS(
        perfControl,
        (automation != nullptr ? automation->size : 0U) +
            (modulation != nullptr ? modulation->size : 0U),
        0U
    );
    if (!target.projectControl) {
        addReport(
            report,
            project_file::LoadSeverity::FATAL,
            project_file::LoadCode::OUTPUT_CAPACITY_EXCEEDED,
            0U
        );
        return false;
    }
    const auto decoded = project_control_codec::decodeProjectControlPayloads(
        controlChunkView(automation),
        controlChunkView(modulation),
        *target.projectControl
    );
    if (decoded.status == project_control_codec::Status::SCRATCH_ALLOCATION_FAILED) {
        addReport(
            report,
            project_file::LoadSeverity::FATAL,
            project_file::LoadCode::OUTPUT_CAPACITY_EXCEEDED,
            0U
        );
        return false;
    }

    reportControlChunkStatus(
        report,
        project_file::ChunkId::MACRO_AUTOMATION,
        automation,
        decoded.automationStatus,
        project_control_codec::PROJECT_AUTOMATION_CHUNK_VERSION_MINOR
    );
    reportControlChunkStatus(
        report,
        project_file::ChunkId::MODULATION_GRAPH,
        modulation,
        decoded.modulationStatus,
        project_control_codec::PROJECT_MODULATION_GRAPH_CHUNK_VERSION_MINOR
    );
    return decoded.decoded();
}

FLASHMEM bool buildSequencerEnvelope(
    const core::state::project::ProjectSnapshot& snapshot,
    sequencer_codec::EnvelopeBuffer& out,
    uint32_t& outSize
) {
    OC_PERF_SCOPE(perfEnvelope, "persistence.project-codec.sequencer");

    sequencer_codec::ProjectSequencerSnapshotEncodeSource source{};
    source.flat = &snapshot.sequencer.flat;
    source.drums = snapshot.drumTracks.get();
    source.focusedStep = snapshot.sequencer.focusedStep;
    source.activeStepProperty = snapshot.sequencer.activeStepProperty;
    const uint8_t activeTrack = snapshot.sequencer.flat.activeTrack;
    if (activeTrack >= source.graphs.size() ||
        snapshot.sequencer.flat.enabledMask == 0U ||
        (snapshot.sequencer.flat.enabledMask &
         static_cast<uint16_t>(1U << activeTrack)) == 0U) {
        return false;
    }
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
    OC_PERF_SCOPE(
        perfSequencer,
        "persistence.project-codec.decode.sequencer"
    );
    OC_PERF_UNITS(
        perfSequencer,
        chunk != nullptr ? chunk->size : 0U,
        0U
    );
    if (chunk == nullptr) {
        addReport(
            report,
            project_file::LoadSeverity::ERROR,
            project_file::LoadCode::MISSING_REQUIRED_CHUNK,
            project_file::chunkIdValue(
                project_file::ChunkId::SEQUENCER_STATE
            )
        );
        return false;
    }
    if (!chunkVersionSupported(
            *chunk,
            PROJECT_SEQUENCER_STATE_CHUNK_VERSION_MINOR,
            report
        )) {
        return false;
    }
    if (chunk->size > sequencer_codec::MAX_ENVELOPE_PAYLOAD_SIZE || chunk->data == nullptr) {
        addReport(report,
                  project_file::LoadSeverity::ERROR,
                  project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
                  chunk->id,
                  chunk->versionMajor,
                  chunk->versionMinor);
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
        return false;
    }

    if (bank->drumTrackMask() != 0U) {
        if (!target.drumTracks) {
            target.drumTracks = core::app::makeExtmemUnique<
                core::state::sequencer::DrumTrackBankSnapshot>();
        }
        if (!target.drumTracks) {
            addReport(report,
                      project_file::LoadSeverity::ERROR,
                      project_file::LoadCode::OUTPUT_CAPACITY_EXCEEDED,
                      chunk->id,
                      chunk->versionMajor,
                      chunk->versionMinor);
            return false;
        }
        bank->captureDrumTrackBank(*target.drumTracks);
    } else {
        target.drumTracks.reset();
    }

    return true;
}

enum class ProjectTrackChunkStatus : uint8_t {
    VALID = 0,
    INVALID,
};

FLASHMEM ProjectTrackChunkStatus readProjectTrackChunk(
    const project_file::DecodedChunkView* chunk,
    core::state::project::ProjectTrackSnapshot& out,
    project_file::LoadReport* report
) {
    OC_PERF_SCOPE(
        perfTrack,
        "persistence.project-codec.decode.track-state"
    );
    OC_PERF_UNITS(perfTrack, chunk != nullptr ? chunk->size : 0U, 0U);
    if (chunk == nullptr) {
        addReport(
            report,
            project_file::LoadSeverity::FATAL,
            project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
            project_file::chunkIdValue(project_file::ChunkId::TRACK_STATE),
            0U,
            0U,
            project_track_codec::PROJECT_TRACK_CHUNK_VERSION_MAJOR,
            project_track_codec::PROJECT_TRACK_CHUNK_VERSION_MINOR
        );
        return ProjectTrackChunkStatus::INVALID;
    }

    const auto decoded = project_track_codec::decodeProjectTrackStatePayload(
        chunk->data,
        chunk->size,
        chunk->versionMajor,
        chunk->versionMinor,
        out
    );
    if (decoded.decoded()) return ProjectTrackChunkStatus::VALID;

    addReport(
        report,
        project_file::LoadSeverity::FATAL,
        decoded.status == project_track_codec::Status::UNSUPPORTED_VERSION
            ? project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION
            : project_file::LoadCode::CHUNK_PAYLOAD_INVALID,
        chunk->id,
        chunk->versionMajor,
        chunk->versionMinor,
        project_track_codec::PROJECT_TRACK_CHUNK_VERSION_MAJOR,
        project_track_codec::PROJECT_TRACK_CHUNK_VERSION_MINOR
    );
    return ProjectTrackChunkStatus::INVALID;
}

}  // namespace

FLASHMEM project_file::EncodeResult encodeProjectSnapshot(
    const core::state::project::ProjectSnapshot& snapshot,
    uint8_t* out,
    uint32_t outCapacity,
    ProjectSnapshotCodecWorkspace& workspace
) {
    if (!snapshot.projectControl) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }
#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.persistence.project-encode-begin"
    );
#endif
    if (!workspace.prepare()) {
        return {.status = project_file::Status::SCRATCH_ALLOCATION_FAILED, .bytesWritten = 0};
    }
    OC_PERF_SCOPE(perfEncode, "persistence.project-codec.encode");

    project_state_codec::ProjectMetaPayload meta{};
    project_state_codec::ProjectTransportPayload transport{};
    project_state_codec::ProjectMusicalContextPayload musical{};
    project_state_codec::ProjectEditingPayload editing{};
    if (!project_state_codec::fillMetaPayload(snapshot.project.metadata, meta) ||
        !project_state_codec::fillTransportPayload(
            snapshot.project.transport,
            transport
        ) ||
        !project_state_codec::fillMusicalContextPayload(
            snapshot.project.musical,
            musical
        ) ||
        !project_state_codec::fillEditingPayload(
            snapshot.project.editing,
            editing
        )) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }

    std::array<uint8_t, project_state_codec::PROJECT_META_PAYLOAD_SIZE> metaBytes{};
    std::array<uint8_t, project_state_codec::PROJECT_TRANSPORT_PAYLOAD_SIZE> transportBytes{};
    std::array<uint8_t, project_state_codec::PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE> musicalBytes{};
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
        !project_state_codec::encodeEditingPayload(
            editing,
            editingBytes.data(),
            static_cast<uint32_t>(editingBytes.size())
        )) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }

    auto& scratch = *workspace.storage_;

    const auto projectTracksEncoded =
        project_track_codec::encodeProjectTrackStatePayload(
            snapshot.projectTracks,
            scratch.projectTracks.data(),
            static_cast<uint32_t>(scratch.projectTracks.size())
        );
    if (!projectTracksEncoded.encoded()) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }

    if (!macro_track_codec::encodeTrackBankPayload(
            snapshot.macroTracks,
            snapshot.sharedTrackEnabledMask,
            snapshot.sharedTrackActive,
            scratch.macro.data(),
            static_cast<uint32_t>(scratch.macro.size())
        )) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }
    const auto controlPayloads =
        project_control_codec::encodeProjectControlPayloads(
            *snapshot.projectControl,
            scratch.projectControl.data(),
            static_cast<uint32_t>(scratch.projectControl.size())
        );
    if (!controlPayloads.encoded()) {
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
            .id = project_file::chunkIdValue(project_file::ChunkId::EDITING),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = editingBytes.data(),
            .size = project_state_codec::PROJECT_EDITING_PAYLOAD_SIZE,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::TRACK_STATE),
            .versionMajor = project_track_codec::PROJECT_TRACK_CHUNK_VERSION_MAJOR,
            .versionMinor = project_track_codec::PROJECT_TRACK_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = scratch.projectTracks.data(),
            .size = project_track_codec::PROJECT_TRACK_STATE_PAYLOAD_SIZE,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::MACRO_STATE),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_MACRO_STATE_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = scratch.macro.data(),
            .size = PROJECT_MACRO_STATE_PAYLOAD_SIZE,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::MACRO_AUTOMATION),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_MACRO_AUTOMATION_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = scratch.projectControl.data() +
                controlPayloads.automationOffset,
            .size = controlPayloads.automationSize,
        },
        {
            .id = project_file::chunkIdValue(
                project_file::ChunkId::MODULATION_GRAPH
            ),
            .versionMajor =
                project_control_codec::PROJECT_CONTROL_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_MODULATION_GRAPH_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = scratch.projectControl.data() +
                controlPayloads.modulationOffset,
            .size = controlPayloads.modulationSize,
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::SEQUENCER_STATE),
            .versionMajor = PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
            .versionMinor = PROJECT_SEQUENCER_STATE_CHUNK_VERSION_MINOR,
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
#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.persistence.project-encode-workspace"
    );
#endif
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
    OC_PERF_SCOPE(perfDecode, "persistence.project-codec.decode");
    OC_PERF_UNITS(perfDecode, size, 0U);
#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.persistence.project-decode-begin"
    );
#endif
    project_file::LoadReport localReport{};
    auto* effectiveReport = report != nullptr ? report : &localReport;
    project_file::DecodedChunkView chunks[project_file::MAX_CHUNKS] = {};
    auto scanResult = project_file::scan(
        data,
        size,
        chunks,
        project_file::MAX_CHUNKS,
        effectiveReport
    );
    if (scanResult.status != project_file::Status::OK) {
        return {
            .ok = false,
            .containerStatus = scanResult.status,
            .loadStatus = effectiveReport->status,
            .overwriteSafe = false,
        };
    }
    if (effectiveReport->hasIssues()) {
        effectiveReport->markRejected();
        return {
            .ok = false,
            .containerStatus = scanResult.status,
            .loadStatus = effectiveReport->status,
            .overwriteSafe = false,
        };
    }
    if (!currentProjectChunkSetValid(
            chunks,
            scanResult.chunkCount,
            effectiveReport
        )) {
        effectiveReport->markRejected();
        return {
            .ok = false,
            .containerStatus = scanResult.status,
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
#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.persistence.project-decode-snapshot"
    );
#endif
    const bool projectStateRead = project_state_codec::applyProjectStateChunks(
        chunks,
        scanResult.chunkCount,
        next->project,
        effectiveReport
    );
    const auto* macroChunk =
        findChunk(chunks, scanResult.chunkCount, project_file::ChunkId::MACRO_STATE);
    readMacroChunk(macroChunk, *next, effectiveReport);
    if (!readProjectControlChunks(
            findChunk(
                chunks,
                scanResult.chunkCount,
                project_file::ChunkId::MACRO_AUTOMATION
            ),
            findChunk(
                chunks,
                scanResult.chunkCount,
                project_file::ChunkId::MODULATION_GRAPH
            ),
            *next,
            effectiveReport
        ) && effectiveReport->failed()) {
        return {
            .ok = false,
            .containerStatus = project_file::Status::SCRATCH_ALLOCATION_FAILED,
            .loadStatus = effectiveReport->status,
            .overwriteSafe = false,
        };
    }
    const auto* sequencerChunk =
        findChunk(chunks, scanResult.chunkCount, project_file::ChunkId::SEQUENCER_STATE);
    readSequencerChunk(
        sequencerChunk,
        *next,
        effectiveReport
    );

    const auto trackChunkStatus = readProjectTrackChunk(
        findChunk(chunks, scanResult.chunkCount, project_file::ChunkId::TRACK_STATE),
        next->projectTracks,
        effectiveReport
    );
    if (!projectStateRead ||
        trackChunkStatus == ProjectTrackChunkStatus::INVALID ||
        effectiveReport->hasIssues()) {
        effectiveReport->markRejected();
        return {
            .ok = false,
            .containerStatus = scanResult.status,
            .loadStatus = effectiveReport->status,
            .overwriteSafe = false,
        };
    }
    out = std::move(*next);
#if OC_ENABLE_STATS
    core::diagnostics::recordDynamicMemorySample(
        "memory.psram.persistence.project-decode-end"
    );
#endif
    return {
        .ok = true,
        .containerStatus = scanResult.status,
        .loadStatus = project_file::LoadStatus::OK,
        .overwriteSafe = true,
    };
}

}  // namespace core::persistence::project_snapshot_codec

#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

#include <array>
#include <cmath>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/MacroTrackBankPersistenceCodec.hpp"
#include "persistence/PersistenceBinaryCodec.hpp"
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
namespace binary = core::persistence::binary_codec;
namespace macro = core::state::macro;

FLASHMEM bool validateMacroAutomationBank(
    const core::state::macro::MacroAutomationBankState& bank
);

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

FLASHMEM uint32_t macroAutomationPayloadSize(uint8_t entryCount, uint16_t pointCount) {
    return PROJECT_MACRO_AUTOMATION_HEADER_SIZE +
           static_cast<uint32_t>(entryCount) * PROJECT_MACRO_AUTOMATION_ENTRY_SIZE +
           static_cast<uint32_t>(pointCount) * PROJECT_MACRO_AUTOMATION_POINT_SIZE;
}

FLASHMEM bool writeMacroAutomationCurveRef(binary::Writer& writer,
                                           const macro::MacroAutomationCurveRef& curve) {
    return writer.writeU8(curve.active ? 1U : 0U) &&
           writer.writeU8(static_cast<uint8_t>(curve.playbackState)) &&
           writer.writeU16(curve.pointOffset) &&
           writer.writeU16(curve.pointCount) &&
           writer.writeU16(curve.sourceDurationTicks) &&
           writer.writeU16(curve.durationTicks) &&
           writer.writeU16(curve.windowOffsetTicks) &&
           writer.writeU8(static_cast<uint8_t>(curve.interpolation)) &&
           writer.writeU8(static_cast<uint8_t>(curve.modulationOrigin));
}

FLASHMEM bool readMacroAutomationCurveRef(binary::Reader& reader,
                                          macro::MacroAutomationCurveRef& curve,
                                          bool legacyV14) {
    uint8_t active = 0;
    uint8_t playbackState = 0;
    uint8_t interpolation = 0;
    uint8_t modulationOrigin = 0;
    if (!reader.readU8(active) ||
        !reader.readU8(playbackState) ||
        !reader.readU16(curve.pointOffset) ||
        !reader.readU16(curve.pointCount) ||
        !reader.readU16(curve.sourceDurationTicks) ||
        !reader.readU16(curve.durationTicks) ||
        !reader.readU16(curve.windowOffsetTicks) ||
        !reader.readU8(interpolation) ||
        !reader.readU8(modulationOrigin)) {
        return false;
    }
    if (interpolation != static_cast<uint8_t>(macro::MacroAutomationInterpolation::LINEAR)) {
        return false;
    }
    if (legacyV14 && (playbackState != 0U || modulationOrigin != 0U)) {
        return false;
    }
    curve.active = active != 0;
    curve.interpolation = static_cast<macro::MacroAutomationInterpolation>(interpolation);
    curve.playbackState = legacyV14
        ? macro::MacroCurvePlaybackState::ACTIVE
        : static_cast<macro::MacroCurvePlaybackState>(playbackState);
    curve.modulationOrigin = legacyV14
        ? macro::MacroModulationOrigin::NATIVE
        : static_cast<macro::MacroModulationOrigin>(modulationOrigin);
    if (!macro::macroCurvePlaybackStateValid(curve.playbackState) ||
        !macro::macroModulationOriginValid(curve.modulationOrigin)) {
        return false;
    }
    return true;
}

FLASHMEM bool writeMacroAutomationSlotState(binary::Writer& writer,
                                            const macro::MacroAutomationSlotState& state) {
    return writeMacroAutomationCurveRef(writer, state.automation) &&
           writeMacroAutomationCurveRef(writer, state.modulation) &&
           writer.writeFloat32(state.modulationDepth);
}

FLASHMEM bool readMacroAutomationSlotState(binary::Reader& reader,
                                           macro::MacroAutomationSlotState& state,
                                           bool legacyV14) {
    return readMacroAutomationCurveRef(reader, state.automation, legacyV14) &&
           readMacroAutomationCurveRef(reader, state.modulation, legacyV14) &&
           reader.readFloat32(state.modulationDepth);
}

FLASHMEM bool fillMacroAutomationPayload(
    const core::state::project::ProjectSnapshot& snapshot,
    uint8_t* out,
    uint32_t outCapacity,
    uint32_t& outSize
) {
    outSize = 0;
    if (out == nullptr) return false;

    if (!snapshot.macroAutomation) return false;
    const auto& bank = *snapshot.macroAutomation;
    if (!validateMacroAutomationBank(bank)) return false;

    const uint8_t entryCount = bank.entryCount;
    const uint16_t pointCount = bank.pointPool.used;
    const uint32_t required = macroAutomationPayloadSize(entryCount, pointCount);
    if (required > outCapacity || required > PROJECT_MACRO_AUTOMATION_MAX_PAYLOAD_SIZE) {
        return false;
    }

    binary::Writer writer(out, outCapacity);
    if (!writer.writeU8(entryCount) ||
        !writer.writeU8(0) ||
        !writer.writeU16(pointCount) ||
        !writer.writeU32(0)) {
        return false;
    }

    for (uint8_t i = 0; i < entryCount; ++i) {
        const auto& source = bank.entries[i];
        if (!writer.writeU8(source.address.track) ||
            !writer.writeU8(source.address.page) ||
            !writer.writeU8(source.address.macro) ||
            !writer.writeU8(0) ||
            !writeMacroAutomationSlotState(writer, source.state)) {
            return false;
        }
    }

    for (uint16_t i = 0; i < pointCount; ++i) {
        const auto& point = bank.pointPool.points[i];
        if (!writer.writeU16(point.tick) || !writer.writeI16(point.value)) {
            return false;
        }
    }

    if (!writer.ok() || writer.offset() != required) return false;
    outSize = writer.offset();
    return true;
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

FLASHMEM bool validateMacroAutomationCurve(
    const core::state::macro::MacroAutomationCurveRef& curve,
    const core::state::macro::MacroAutomationPointPool& pool
) {
    if (!core::state::macro::macroCurvePlaybackStateValid(curve.playbackState) ||
        !core::state::macro::macroModulationOriginValid(curve.modulationOrigin)) {
        return false;
    }
    if (!curve.active) return curve.pointCount == 0;
    if (curve.durationTicks == 0 || curve.sourceDurationTicks == 0 || curve.pointCount == 0) {
        return false;
    }
    if (curve.windowOffsetTicks > curve.sourceDurationTicks) return false;
    if (curve.pointOffset >= pool.used) return false;
    const uint32_t end =
        static_cast<uint32_t>(curve.pointOffset) + static_cast<uint32_t>(curve.pointCount);
    if (end > pool.used || end > core::state::macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY) {
        return false;
    }
    uint16_t previousTick = 0;
    for (uint16_t i = 0; i < curve.pointCount; ++i) {
        const auto& point = pool.points[static_cast<uint16_t>(curve.pointOffset + i)];
        if (point.tick > curve.sourceDurationTicks) return false;
        if (i > 0 && point.tick < previousTick) return false;
        previousTick = point.tick;
    }
    return true;
}

struct MacroAutomationCurveRange {
    uint16_t start = 0;
    uint16_t end = 0;
};

FLASHMEM bool appendMacroAutomationCurveRange(
    const core::state::macro::MacroAutomationCurveRef& curve,
    std::array<MacroAutomationCurveRange, core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY * 2>& ranges,
    uint8_t& count
) {
    if (!curve.active) return true;
    if (count >= ranges.size()) return false;
    ranges[count++] = MacroAutomationCurveRange{
        .start = curve.pointOffset,
        .end = static_cast<uint16_t>(curve.pointOffset + curve.pointCount),
    };
    return true;
}

FLASHMEM void sortMacroAutomationRanges(
    std::array<MacroAutomationCurveRange, core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY * 2>& ranges,
    uint8_t count
) {
    for (uint8_t i = 1; i < count; ++i) {
        const auto current = ranges[i];
        uint8_t j = i;
        while (j > 0 && ranges[j - 1U].start > current.start) {
            ranges[j] = ranges[j - 1U];
            --j;
        }
        ranges[j] = current;
    }
}

FLASHMEM bool macroAutomationPoolIsFullyCovered(
    const std::array<MacroAutomationCurveRange, core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY * 2>& ranges,
    uint8_t count,
    uint16_t poolUsed
) {
    if (poolUsed == 0) return count == 0;
    if (count == 0 || ranges[0].start != 0) return false;
    uint16_t cursor = 0;
    for (uint8_t i = 0; i < count; ++i) {
        if (ranges[i].start != cursor || ranges[i].end <= ranges[i].start) return false;
        cursor = ranges[i].end;
    }
    return cursor == poolUsed;
}

FLASHMEM bool validateMacroAutomationBank(
    const core::state::macro::MacroAutomationBankState& bank
) {
    if (bank.entryCount > core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY) return false;
    if (bank.pointPool.used > core::state::macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY) {
        return false;
    }
    std::array<
        MacroAutomationCurveRange,
        core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY * 2> ranges{};
    uint8_t rangeCount = 0;
    const uint8_t count = bank.entryCount;
    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = bank.entries[i];
        if (!entry.active) return false;
        if (!core::state::macro::macroAutomationAddressValid(entry.address)) return false;
        for (uint8_t j = static_cast<uint8_t>(i + 1U); j < count; ++j) {
            if (bank.entries[j].active &&
                core::state::macro::macroAutomationAddressEquals(
                    entry.address,
                    bank.entries[j].address
                )) {
                return false;
            }
        }
        if (!core::state::macro::macroAutomationCurveLifecycleValid(
                entry.state.automation
            ) ||
            !core::state::macro::macroModulationCurveLifecycleValid(
                entry.state.modulation
            )) {
            return false;
        }
        if (!validateMacroAutomationCurve(entry.state.automation, bank.pointPool)) return false;
        if (!validateMacroAutomationCurve(entry.state.modulation, bank.pointPool)) return false;
        if (!std::isfinite(entry.state.modulationDepth) ||
            entry.state.modulationDepth < 0.0f ||
            entry.state.modulationDepth > 1.0f) {
            return false;
        }
        if (!appendMacroAutomationCurveRange(entry.state.automation, ranges, rangeCount)) {
            return false;
        }
        if (!appendMacroAutomationCurveRange(entry.state.modulation, ranges, rangeCount)) {
            return false;
        }
    }
    sortMacroAutomationRanges(ranges, rangeCount);
    return macroAutomationPoolIsFullyCovered(ranges, rangeCount, bank.pointPool.used);
}

FLASHMEM bool readMacroAutomationPayload(const uint8_t* data,
                                          uint32_t size,
                                          macro::MacroAutomationBankState& out,
                                          bool legacyV14) {
    out.clear();
    if (data == nullptr || size < PROJECT_MACRO_AUTOMATION_HEADER_SIZE) return false;

    binary::Reader reader(data, size);
    uint8_t entryCount = 0;
    uint8_t reserved0 = 0;
    uint16_t pointCount = 0;
    uint32_t reserved1 = 0;
    if (!reader.readU8(entryCount) ||
        !reader.readU8(reserved0) ||
        !reader.readU16(pointCount) ||
        !reader.readU32(reserved1)) {
        return false;
    }
    (void)reserved0;
    (void)reserved1;

    if (entryCount > macro::MACRO_AUTOMATION_SLOT_CAPACITY) return false;
    if (pointCount > macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY) {
        return false;
    }

    const uint32_t required = macroAutomationPayloadSize(
        entryCount,
        pointCount
    );
    if (required != size || required > PROJECT_MACRO_AUTOMATION_MAX_PAYLOAD_SIZE) {
        return false;
    }

    out.entryCount = entryCount;
    for (uint8_t i = 0; i < entryCount; ++i) {
        macro::MacroAutomationSlotAddress address{};
        macro::MacroAutomationSlotState state{};
        uint8_t reservedEntry = 0;
        if (!reader.readU8(address.track) ||
            !reader.readU8(address.page) ||
            !reader.readU8(address.macro) ||
            !reader.readU8(reservedEntry) ||
            !readMacroAutomationSlotState(reader, state, legacyV14)) {
            return false;
        }
        (void)reservedEntry;

        out.entries[i] = macro::MacroAutomationSlotEntry{
            .active = true,
            .address = address,
            .state = state,
        };
    }

    out.pointPool.used = pointCount;
    for (uint16_t i = 0; i < pointCount; ++i) {
        uint16_t tick = 0;
        int16_t value = 0;
        if (!reader.readU16(tick) || !reader.readI16(value)) return false;
        out.pointPool.points[i] = macro::MacroPackedCurvePoint{
            .tick = tick,
            .value = value,
        };
    }

    return reader.ok() && reader.offset() == size && validateMacroAutomationBank(out);
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
    if (!readMacroAutomationPayload(
            chunk->data,
            chunk->size,
            *target.macroAutomation,
            legacyV14
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
    if (!fillMacroAutomationPayload(
            snapshot,
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

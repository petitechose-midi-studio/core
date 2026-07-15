#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "persistence/LegacyMacroAutomationPersistenceCodec.hpp"
#include "persistence/MacroTrackBankPersistenceCodec.hpp"
#include "persistence/ProjectControlLegacyMigration.hpp"
#include "persistence/ProjectControlPersistenceCodec.hpp"
#include "persistence/ProjectFileContainer.hpp"
#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectStatePersistencePayloads.hpp"
#include "persistence/SequencerPersistenceEnvelope.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace {

namespace control = core::persistence::project_control_codec;
namespace legacy_codec =
    core::persistence::macro_automation_legacy_codec;
namespace migration = core::persistence::project_control_migration;
namespace project_file = core::persistence::project_file;
namespace project_state_codec = core::persistence::project_state_codec;
namespace macro_track_codec = core::persistence::macro_track_codec;
namespace sequencer_codec = core::persistence::sequencer_codec;
namespace macro = core::state::macro;
namespace mod = core::state::modulation;

using DomainPtr = std::unique_ptr<mod::ProjectControlDomainState>;
using LegacyPtr = std::unique_ptr<macro::MacroAutomationBankState>;

mod::ModulationDestination destinationFromAddress(uint16_t address) {
    return {
        mod::ModulationDestinationKind::MACRO_SLOT,
        static_cast<uint8_t>(address / mod::PROJECT_MODULATION_MACRO_COUNT),
        0U,
        static_cast<uint8_t>(address % mod::PROJECT_MODULATION_MACRO_COUNT),
    };
}

macro::MacroAutomationCurveRef legacyCurve(
    uint16_t offset,
    uint16_t count,
    macro::MacroCurvePlaybackState playback =
        macro::MacroCurvePlaybackState::ACTIVE,
    macro::MacroModulationOrigin origin = macro::MacroModulationOrigin::NATIVE
) {
    macro::MacroAutomationCurveRef curve{};
    curve.active = true;
    curve.playbackState = playback;
    curve.pointOffset = offset;
    curve.pointCount = count;
    curve.sourceDurationTicks = 192U;
    curve.durationTicks = 768U;
    curve.windowOffsetTicks = 192U;
    curve.interpolation = macro::MacroAutomationInterpolation::LINEAR;
    curve.modulationOrigin = origin;
    return curve;
}

LegacyPtr makeLegacyBank() {
    auto bank = std::make_unique<macro::MacroAutomationBankState>();
    bank->entryCount = 2U;

    auto& later = bank->entries[0];
    later.active = true;
    later.address = {2U, 1U, 3U};
    later.state.automation = legacyCurve(
        0U,
        2U,
        macro::MacroCurvePlaybackState::OFF
    );
    later.state.modulation = legacyCurve(
        2U,
        2U,
        macro::MacroCurvePlaybackState::ACTIVE,
        macro::MacroModulationOrigin::CONVERTED_MEAN
    );
    later.state.modulationDepth = 0.5f;

    auto& first = bank->entries[1];
    first.active = true;
    first.address = {0U, 0U, 1U};
    first.state.automation = legacyCurve(4U, 2U);
    first.state.modulationDepth = 0.0f;

    bank->pointPool.used = 6U;
    bank->pointPool.points[0] = {0U, 4096};
    bank->pointPool.points[1] = {192U, 28000};
    bank->pointPool.points[2] = {0U, -32768};
    bank->pointPool.points[3] = {192U, 22000};
    bank->pointPool.points[4] = {0U, -4000};
    bank->pointPool.points[5] = {0U, 16000};
    assert(legacy_codec::validBank(*bank));
    return bank;
}

mod::ProjectCurveId appendCurve(
    mod::ProjectControlDomainState& domain,
    mod::ProjectCurveValueDomain valueDomain,
    uint16_t pointCount,
    int16_t value = 0
) {
    assert(pointCount > 0U);
    assert(static_cast<uint32_t>(domain.curves.pointCount) + pointCount <=
           mod::PROJECT_CURVE_POINT_CAPACITY);
    const mod::ProjectCurveId id{domain.curves.nextCurveId++};
    auto& record = domain.curves.records[domain.curves.recordCount++];
    record = {};
    record.id = id;
    record.pointOffset = domain.curves.pointCount;
    record.pointCount = pointCount;
    record.sourceDurationTicks = 1U;
    record.durationTicks = 1U;
    record.referenceCount = 1U;
    record.valueDomain = valueDomain;
    record.origin = mod::ProjectCurveOrigin::NATIVE;
    for (uint16_t index = 0; index < pointCount; ++index) {
        domain.curves.points[domain.curves.pointCount++] = {0U, value};
    }
    return id;
}

void writeSourceName(
    std::array<char, mod::PROJECT_MODULATOR_NAME_CAPACITY>& name,
    uint16_t index
) {
    name.fill('\0');
    name[0] = 'S';
    name[1] = static_cast<char>('0' + ((index / 100U) % 10U));
    name[2] = static_cast<char>('0' + ((index / 10U) % 10U));
    name[3] = static_cast<char>('0' + (index % 10U));
}

DomainPtr makeTypicalDomain() {
    auto legacy = makeLegacyBank();
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    assert(migration::liftLegacyMacroAutomationBank(
        *legacy,
        *domain
    ).migrated());

    assert(domain->modulation.sourceCount == 1U);
    const auto original = domain->modulation.sources[0].id;
    assert(mod::setProjectModulatorReach(
        domain->modulation,
        original,
        mod::ModulatorReach{.kind = mod::ModulatorReachKind::PROJECT}
    ).changed());
    const auto clone = mod::duplicateProjectModulator(
        domain->modulation,
        domain->curves,
        original,
        "Shared Shape"
    );
    assert(clone.changed());
    mod::ModulationBindingDraft cloneBinding{};
    cloneBinding.sourceId = clone.sourceId;
    cloneBinding.destination = destinationFromAddress(7U);
    cloneBinding.amountQ15 = -12000;
    assert(mod::addProjectModulationBinding(
        domain->modulation,
        cloneBinding
    ).changed());

    mod::ModulatorLfoDraft lfo{};
    lfo.name = "Slow LFO";
    lfo.reach.kind = mod::ModulatorReachKind::PROJECT;
    lfo.parameters.periodTicks = 1536U;
    lfo.parameters.shape = mod::ModulatorLfoShape::TRIANGLE;
    const auto lfoResult = mod::createLfoModulator(domain->modulation, lfo);
    assert(lfoResult.changed());
    mod::ModulationBindingDraft lfoBinding{};
    lfoBinding.sourceId = lfoResult.sourceId;
    lfoBinding.destination = destinationFromAddress(11U);
    lfoBinding.amountQ15 = 8192;
    lfoBinding.inputRange = mod::ModulationInputRange::UNIPOLAR;
    assert(mod::addProjectModulationBinding(
        domain->modulation,
        lfoBinding
    ).changed());
    mod::ModulationTriggerDraft trigger{};
    trigger.sourceId = lfoResult.sourceId;
    trigger.trigger.kind = mod::ModulationTriggerKind::TRANSPORT_START;
    assert(mod::addProjectModulationTrigger(
        domain->modulation,
        trigger
    ).changed());
    assert(mod::validProjectModulationDomain(
        domain->modulation,
        domain->curves,
        &domain->automation
    ));
    return domain;
}

DomainPtr makeAutomationOnlyDomain(uint16_t pointCount) {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    const auto curve = appendCurve(
        *domain,
        mod::ProjectCurveValueDomain::ABSOLUTE,
        pointCount,
        12000
    );
    domain->automation.entryCount = 1U;
    domain->automation.entries[0].destination = destinationFromAddress(0U);
    domain->automation.entries[0].curveId = curve;
    domain->automation.entries[0].flags =
        mod::PROJECT_AUTOMATION_CURVE_FLAG_ENABLED;
    assert(mod::validProjectModulationDomain(
        domain->modulation,
        domain->curves,
        &domain->automation
    ));
    return domain;
}

DomainPtr makeDenseDomain() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    constexpr uint16_t AUTOMATION_POINTS =
        mod::PROJECT_CURVE_POINT_CAPACITY - mod::PROJECT_MODULATOR_CAPACITY;
    constexpr uint16_t FIRST_AUTOMATION_POINTS =
        AUTOMATION_POINTS - (mod::PROJECT_AUTOMATION_ENTRY_CAPACITY - 1U);

    domain->automation.entryCount = mod::PROJECT_AUTOMATION_ENTRY_CAPACITY;
    for (uint16_t index = 0;
         index < mod::PROJECT_AUTOMATION_ENTRY_CAPACITY;
         ++index) {
        const uint16_t count = index == 0U ? FIRST_AUTOMATION_POINTS : 1U;
        const auto curve = appendCurve(
            *domain,
            mod::ProjectCurveValueDomain::ABSOLUTE,
            count,
            16000
        );
        auto& entry = domain->automation.entries[index];
        entry.destination = destinationFromAddress(index);
        entry.curveId = curve;
        entry.flags = mod::PROJECT_AUTOMATION_CURVE_FLAG_ENABLED;
    }

    auto& graph = domain->modulation;
    graph.sourceCount = mod::PROJECT_MODULATOR_CAPACITY;
    graph.outputBindingCount = mod::PROJECT_MODULATION_BINDING_CAPACITY;
    graph.triggerBindingCount = mod::PROJECT_MODULATION_TRIGGER_CAPACITY;
    graph.nextSourceId = mod::PROJECT_MODULATOR_CAPACITY + 1U;
    graph.nextBindingId =
        mod::PROJECT_MODULATION_BINDING_CAPACITY +
        mod::PROJECT_MODULATION_TRIGGER_CAPACITY + 1U;
    for (uint16_t index = 0; index < graph.sourceCount; ++index) {
        const auto curve = appendCurve(
            *domain,
            mod::ProjectCurveValueDomain::BIPOLAR,
            1U,
            static_cast<int16_t>((index % 2U) == 0U ? -12000 : 12000)
        );
        auto& source = graph.sources[index];
        source = {};
        source.id = {static_cast<uint32_t>(index + 1U)};
        writeSourceName(source.name, index);
        source.reach.kind = mod::ModulatorReachKind::PROJECT;
        source.kind = mod::ModulatorKind::RECORDED_SHAPE;
        source.flags = mod::PROJECT_MODULATOR_FLAG_ENABLED;
        source.schemaVersion = 1U;
        source.parameters.recordedCurveId = curve;

        for (uint8_t edge = 0; edge < 4U; ++edge) {
            const uint16_t bindingIndex = static_cast<uint16_t>(
                index * 4U + edge
            );
            auto& binding = graph.outputBindings[bindingIndex];
            binding = {};
            binding.id = {static_cast<uint32_t>(bindingIndex + 1U)};
            binding.sourceId = source.id;
            binding.destination = destinationFromAddress(
                static_cast<uint16_t>((index + edge) % 128U)
            );
            binding.amountQ15 = static_cast<int16_t>(1000 + edge);
            binding.flags = mod::PROJECT_MODULATION_BINDING_FLAG_ENABLED;
        }

        auto& trigger = graph.triggerBindings[index];
        trigger = {};
        trigger.id = {
            static_cast<uint32_t>(
                mod::PROJECT_MODULATION_BINDING_CAPACITY + index + 1U
            ),
        };
        trigger.sourceId = source.id;
        trigger.trigger.kind = mod::ModulationTriggerKind::TRANSPORT_START;
        trigger.flags = mod::PROJECT_MODULATION_TRIGGER_FLAG_ENABLED;
    }
    assert(domain->curves.recordCount == mod::PROJECT_CURVE_LIVE_CAPACITY);
    assert(domain->curves.pointCount == mod::PROJECT_CURVE_POINT_CAPACITY);
    assert(mod::validProjectModulationDomain(
        graph,
        domain->curves,
        &domain->automation
    ));
    return domain;
}

control::ChunkPayloadView automationView(
    const std::vector<uint8_t>& bytes,
    const control::EncodeResult& encoded
) {
    return {
        .present = true,
        .versionMajor = control::PROJECT_CONTROL_CHUNK_VERSION_MAJOR,
        .versionMinor = control::PROJECT_AUTOMATION_CHUNK_VERSION_MINOR,
        .flags = 0U,
        .data = bytes.data() + encoded.automationOffset,
        .size = encoded.automationSize,
    };
}

control::ChunkPayloadView modulationView(
    const std::vector<uint8_t>& bytes,
    const control::EncodeResult& encoded
) {
    return {
        .present = true,
        .versionMajor = control::PROJECT_CONTROL_CHUNK_VERSION_MAJOR,
        .versionMinor = control::PROJECT_MODULATION_GRAPH_CHUNK_VERSION_MINOR,
        .flags = 0U,
        .data = bytes.data() + encoded.modulationOffset,
        .size = encoded.modulationSize,
    };
}

control::EncodeResult encodeDomain(
    const mod::ProjectControlDomainState& domain,
    std::vector<uint8_t>& bytes
) {
    bytes.assign(control::PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE, 0xCDU);
    const auto result = control::encodeProjectControlPayloads(
        domain,
        bytes.data(),
        static_cast<uint32_t>(bytes.size())
    );
    assert(result.encoded());
    bytes.resize(result.bytesWritten);
    return result;
}

void testExactLayoutEmptyRoundTripAndPreflightAtomicity() {
    assert(sizeof(mod::ProjectControlDomainState) == 159516U);
    assert(control::PROJECT_AUTOMATION_MAX_PAYLOAD_SIZE == 134688U);
    assert(control::PROJECT_MODULATION_GRAPH_MAX_PAYLOAD_SIZE == 152608U);
    assert(control::PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE == 156224U);

    auto empty = std::make_unique<mod::ProjectControlDomainState>();
    std::vector<uint8_t> bytes;
    const auto encoded = encodeDomain(*empty, bytes);
    assert(encoded.automationSize == 32U);
    assert(encoded.modulationSize == 32U);
    assert(encoded.bytesWritten == 64U);

    auto decoded = std::make_unique<mod::ProjectControlDomainState>();
    const auto result = control::decodeProjectControlPayloads(
        automationView(bytes, encoded),
        modulationView(bytes, encoded),
        *decoded
    );
    assert(result.decoded());
    assert(result.automationStatus == control::ChunkStatus::CURRENT);
    assert(result.modulationStatus == control::ChunkStatus::CURRENT);
    assert(!result.partial && result.overwriteSafe);
    assert(std::memcmp(empty.get(), decoded.get(), sizeof(*empty)) == 0);

    std::vector<uint8_t> roundTrip;
    const auto reencoded = encodeDomain(*decoded, roundTrip);
    assert(reencoded.bytesWritten == encoded.bytesWritten);
    assert(roundTrip == bytes);

    std::vector<uint8_t> shortBuffer(63U, 0xA5U);
    const auto before = shortBuffer;
    const auto failed = control::encodeProjectControlPayloads(
        *empty,
        shortBuffer.data(),
        static_cast<uint32_t>(shortBuffer.size())
    );
    assert(failed.status == control::Status::BUFFER_TOO_SMALL);
    assert(failed.bytesRequired == 64U);
    assert(shortBuffer == before);
}

void testLegacyLiftIsDeterministicCanonicalAndAtomic() {
    auto legacy = makeLegacyBank();
    auto first = std::make_unique<mod::ProjectControlDomainState>();
    auto second = std::make_unique<mod::ProjectControlDomainState>();
    const auto firstResult = migration::liftLegacyMacroAutomationBank(
        *legacy,
        *first
    );
    const auto secondResult = migration::liftLegacyMacroAutomationBank(
        *legacy,
        *second
    );
    assert(firstResult.migrated() && secondResult.migrated());
    assert(std::memcmp(first.get(), second.get(), sizeof(*first)) == 0);
    assert(first->automation.entryCount == 2U);
    assert(first->modulation.sourceCount == 1U);
    assert(first->modulation.outputBindingCount == 1U);
    assert(first->curves.pointCount == legacy->pointPool.used);

    // Stable address order wins over legacy array order.
    assert(first->automation.entries[0].destination.track == 0U);
    assert(first->automation.entries[1].destination.track == 2U);
    assert((first->automation.entries[1].flags &
            mod::PROJECT_AUTOMATION_CURVE_FLAG_ENABLED) == 0U);
    const auto& source = first->modulation.sources[0];
    assert(source.id.value == 1U);
    assert(std::strcmp(source.name.data(), "T03 P02 M4") == 0);
    assert(source.reach.kind == mod::ModulatorReachKind::MACRO);
    assert(source.reach.track == 2U && source.reach.page == 1U &&
           source.reach.macro == 3U);
    assert(first->modulation.outputBindings[0].amountQ15 == 16384);

    const auto* modulationCurve = mod::findProjectCurve(
        first->curves,
        source.parameters.recordedCurveId
    );
    assert(modulationCurve != nullptr);
    assert(modulationCurve->durationTicks == 768U);
    assert(modulationCurve->windowOffsetTicks == 192U);
    assert(modulationCurve->origin == mod::ProjectCurveOrigin::CONVERTED_MEAN);
    assert(first->curves.points[modulationCurve->pointOffset].value == -32767);
    const float legacyContribution = macro::macroAutomationUnpackValue(
        legacy->pointPool.points[2].value,
        true
    ) * legacy->entries[0].state.modulationDepth;
    const float migratedContribution =
        static_cast<float>(
            first->curves.points[modulationCurve->pointOffset].value
        ) / 32767.0f *
        static_cast<float>(first->modulation.outputBindings[0].amountQ15) /
            32767.0f;
    assert(std::fabs(legacyContribution - migratedContribution) <=
           2.0f / 32767.0f);

    const auto& firstAutomation = first->automation.entries[0];
    const auto* automationCurve = mod::findProjectCurve(
        first->curves,
        firstAutomation.curveId
    );
    assert(automationCurve != nullptr);
    // Legacy absolute unpacking already clamped -4000 to zero.
    assert(first->curves.points[automationCurve->pointOffset].value == 0);

    auto invalid = std::make_unique<macro::MacroAutomationBankState>(*legacy);
    --invalid->pointPool.used;
    auto stableOutput = makeTypicalDomain();
    auto stableCopy = std::make_unique<mod::ProjectControlDomainState>(
        *stableOutput
    );
    const auto failed = migration::liftLegacyMacroAutomationBank(
        *invalid,
        *stableOutput
    );
    assert(failed.status == migration::Status::INVALID_LEGACY_BANK);
    assert(std::memcmp(
        stableOutput.get(),
        stableCopy.get(),
        sizeof(*stableOutput)
    ) == 0);
}

void testLegacyV14V15DecodeAndAmbiguityPolicy() {
    auto legacy = makeLegacyBank();
    std::vector<uint8_t> payload(legacy_codec::MAX_PAYLOAD_SIZE);
    uint32_t payloadSize = 0;
    assert(legacy_codec::encodeV15(
        *legacy,
        payload.data(),
        static_cast<uint32_t>(payload.size()),
        payloadSize
    ));
    payload.resize(payloadSize);

    control::ChunkPayloadView v15{
        .present = true,
        .versionMajor = legacy_codec::CHUNK_VERSION_MAJOR,
        .versionMinor = legacy_codec::CHUNK_VERSION_MINOR_V15,
        .data = payload.data(),
        .size = payloadSize,
    };
    auto migrated = std::make_unique<mod::ProjectControlDomainState>();
    auto result = control::decodeProjectControlPayloads(v15, {}, *migrated);
    assert(result.decoded() && result.migratedLegacy);
    assert(result.automationStatus == control::ChunkStatus::MIGRATED_LEGACY);
    assert(result.modulationStatus == control::ChunkStatus::MIGRATED_LEGACY);
    assert(!result.partial && result.overwriteSafe);

    auto v14Bank = std::make_unique<macro::MacroAutomationBankState>(*legacy);
    for (uint8_t index = 0; index < v14Bank->entryCount; ++index) {
        auto& state = v14Bank->entries[index].state;
        state.automation.playbackState = macro::MacroCurvePlaybackState::ACTIVE;
        state.modulation.playbackState = macro::MacroCurvePlaybackState::ACTIVE;
        state.modulation.modulationOrigin = macro::MacroModulationOrigin::NATIVE;
    }
    payload.assign(legacy_codec::MAX_PAYLOAD_SIZE, 0U);
    assert(legacy_codec::encodeV15(
        *v14Bank,
        payload.data(),
        static_cast<uint32_t>(payload.size()),
        payloadSize
    ));
    payload.resize(payloadSize);
    control::ChunkPayloadView v14{
        .present = true,
        .versionMajor = legacy_codec::CHUNK_VERSION_MAJOR,
        .versionMinor = legacy_codec::CHUNK_VERSION_MINOR_V14,
        .data = payload.data(),
        .size = payloadSize,
    };
    result = control::decodeProjectControlPayloads(v14, {}, *migrated);
    assert(result.decoded() && result.migratedLegacy && !result.partial);

    auto current = makeTypicalDomain();
    std::vector<uint8_t> currentBytes;
    const auto currentEncoded = encodeDomain(*current, currentBytes);
    result = control::decodeProjectControlPayloads(
        v14,
        modulationView(currentBytes, currentEncoded),
        *migrated
    );
    assert(result.decoded() && result.migratedLegacy);
    assert(result.modulationStatus == control::ChunkStatus::IGNORED_AMBIGUOUS);
    assert(result.partial && !result.overwriteSafe);
    assert(migrated->modulation.sourceCount == 1U);
}

void testCurrentRoundTripSharingAndIndependentRecovery() {
    auto source = makeTypicalDomain();
    std::vector<uint8_t> bytes;
    const auto encoded = encodeDomain(*source, bytes);
    auto decoded = std::make_unique<mod::ProjectControlDomainState>();
    auto result = control::decodeProjectControlPayloads(
        automationView(bytes, encoded),
        modulationView(bytes, encoded),
        *decoded
    );
    assert(result.decoded() && !result.partial);
    assert(decoded->automation.entryCount == source->automation.entryCount);
    assert(decoded->modulation.sourceCount == source->modulation.sourceCount);
    assert(decoded->modulation.outputBindingCount ==
           source->modulation.outputBindingCount);
    assert(decoded->modulation.triggerBindingCount == 1U);
    assert(decoded->modulation.sources[0].parameters.recordedCurveId ==
           decoded->modulation.sources[1].parameters.recordedCurveId);
    const auto* shared = mod::findProjectCurve(
        decoded->curves,
        decoded->modulation.sources[0].parameters.recordedCurveId
    );
    assert(shared != nullptr && shared->referenceCount == 2U);

    std::vector<uint8_t> roundTrip;
    encodeDomain(*decoded, roundTrip);
    assert(roundTrip == bytes);

    auto corruptGraph = bytes;
    corruptGraph[encoded.modulationOffset + 20U] = 1U;
    result = control::decodeProjectControlPayloads(
        automationView(corruptGraph, encoded),
        modulationView(corruptGraph, encoded),
        *decoded
    );
    assert(result.automationStatus == control::ChunkStatus::CURRENT);
    assert(result.modulationStatus == control::ChunkStatus::INVALID_PAYLOAD);
    assert(result.partial && !result.overwriteSafe);
    assert(decoded->automation.entryCount == source->automation.entryCount);
    assert(decoded->modulation.sourceCount == 0U);

    auto corruptAutomation = bytes;
    corruptAutomation[8U] = 1U;
    result = control::decodeProjectControlPayloads(
        automationView(corruptAutomation, encoded),
        modulationView(corruptAutomation, encoded),
        *decoded
    );
    assert(result.automationStatus == control::ChunkStatus::INVALID_PAYLOAD);
    assert(result.modulationStatus == control::ChunkStatus::CURRENT);
    assert(decoded->automation.entryCount == 0U);
    assert(decoded->modulation.sourceCount == source->modulation.sourceCount);

    auto unknownKind = bytes;
    unknownKind[
        encoded.modulationOffset + control::PROJECT_CONTROL_CHUNK_HEADER_SIZE + 26U
    ] = 0xFFU;
    result = control::decodeProjectControlPayloads(
        automationView(unknownKind, encoded),
        modulationView(unknownKind, encoded),
        *decoded
    );
    assert(result.modulationStatus == control::ChunkStatus::INVALID_PAYLOAD);
    assert(decoded->modulation.sourceCount == 0U);

    auto unsupportedAutomation = automationView(bytes, encoded);
    ++unsupportedAutomation.versionMinor;
    result = control::decodeProjectControlPayloads(
        unsupportedAutomation,
        modulationView(bytes, encoded),
        *decoded
    );
    assert(result.automationStatus == control::ChunkStatus::UNSUPPORTED_VERSION);
    assert(result.modulationStatus == control::ChunkStatus::CURRENT);
    assert(result.partial && !result.overwriteSafe);

    auto flaggedGraph = modulationView(bytes, encoded);
    flaggedGraph.flags = 1U;
    result = control::decodeProjectControlPayloads(
        automationView(bytes, encoded),
        flaggedGraph,
        *decoded
    );
    assert(result.automationStatus == control::ChunkStatus::CURRENT);
    assert(result.modulationStatus == control::ChunkStatus::INVALID_PAYLOAD);

    auto trailing = bytes;
    trailing.push_back(0U);
    auto trailingGraph = modulationView(trailing, encoded);
    ++trailingGraph.size;
    result = control::decodeProjectControlPayloads(
        automationView(trailing, encoded),
        trailingGraph,
        *decoded
    );
    assert(result.modulationStatus == control::ChunkStatus::INVALID_PAYLOAD);

    auto invalidDomain = std::make_unique<mod::ProjectControlDomainState>(
        *source
    );
    invalidDomain->modulation.outputBindings[0].reserved[0] = 1U;
    std::vector<uint8_t> untouched(256U, 0x5AU);
    const auto untouchedCopy = untouched;
    const auto invalidEncode = control::encodeProjectControlPayloads(
        *invalidDomain,
        untouched.data(),
        static_cast<uint32_t>(untouched.size())
    );
    assert(invalidEncode.status == control::Status::INVALID_DOMAIN);
    assert(untouched == untouchedCopy);

    result = control::decodeProjectControlPayloads({}, {}, *decoded);
    assert(result.decoded() && !result.partial && result.overwriteSafe);
    assert(result.automationStatus == control::ChunkStatus::MISSING);
    assert(result.modulationStatus == control::ChunkStatus::MISSING);
    assert(decoded->automation.entryCount == 0U &&
           decoded->modulation.sourceCount == 0U);
}

void testCombinedCapacityConflictPreservesAutomation() {
    auto automation = makeAutomationOnlyDomain(
        mod::PROJECT_CURVE_POINT_CAPACITY
    );
    auto graph = makeTypicalDomain();
    std::vector<uint8_t> automationBytes;
    std::vector<uint8_t> graphBytes;
    const auto automationEncoded = encodeDomain(*automation, automationBytes);
    const auto graphEncoded = encodeDomain(*graph, graphBytes);

    auto decoded = std::make_unique<mod::ProjectControlDomainState>();
    const auto result = control::decodeProjectControlPayloads(
        automationView(automationBytes, automationEncoded),
        modulationView(graphBytes, graphEncoded),
        *decoded
    );
    assert(result.decoded());
    assert(result.automationStatus == control::ChunkStatus::CURRENT);
    assert(result.modulationStatus == control::ChunkStatus::CAPACITY_EXCEEDED);
    assert(result.partial && !result.overwriteSafe);
    assert(decoded->automation.entryCount == 1U);
    assert(decoded->curves.pointCount == mod::PROJECT_CURVE_POINT_CAPACITY);
    assert(decoded->modulation.sourceCount == 0U);
}

void testDenseMaximumAndCompleteProjectCeiling() {
    auto dense = makeDenseDomain();
    std::vector<uint8_t> bytes;
    const auto encoded = encodeDomain(*dense, bytes);
    assert(encoded.bytesWritten ==
           control::PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE);

    const project_file::ChunkView chunks[] = {
        {.size = project_state_codec::PROJECT_META_PAYLOAD_SIZE},
        {.size = project_state_codec::PROJECT_TRANSPORT_PAYLOAD_SIZE},
        {.size = project_state_codec::PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE},
        {.size = project_state_codec::PROJECT_ROUTING_PAYLOAD_SIZE},
        {.size = project_state_codec::PROJECT_EDITING_PAYLOAD_SIZE},
        {.size = macro_track_codec::MACRO_TRACK_BANK_PAYLOAD_SIZE},
        {.size = encoded.automationSize},
        {.size = encoded.modulationSize},
        {.size = sequencer_codec::MAX_PROJECT_SEQUENCER_ENVELOPE_PAYLOAD_SIZE},
    };
    const uint32_t completeSize = project_file::encodedSize(
        chunks,
        static_cast<uint16_t>(std::size(chunks))
    );
    assert(completeSize == 410113U);
    assert(completeSize <= core::persistence::PROJECT_FILE_MAX_SIZE);
    assert(core::persistence::PROJECT_FILE_MAX_SIZE - completeSize == 114175U);

    auto decoded = std::make_unique<mod::ProjectControlDomainState>();
    const auto result = control::decodeProjectControlPayloads(
        automationView(bytes, encoded),
        modulationView(bytes, encoded),
        *decoded
    );
    assert(result.decoded() && !result.partial);
    assert(decoded->automation.entryCount ==
           mod::PROJECT_AUTOMATION_ENTRY_CAPACITY);
    assert(decoded->modulation.sourceCount == mod::PROJECT_MODULATOR_CAPACITY);
    assert(decoded->modulation.outputBindingCount ==
           mod::PROJECT_MODULATION_BINDING_CAPACITY);
    assert(decoded->modulation.triggerBindingCount ==
           mod::PROJECT_MODULATION_TRIGGER_CAPACITY);
    assert(decoded->curves.recordCount == mod::PROJECT_CURVE_LIVE_CAPACITY);
    assert(decoded->curves.pointCount == mod::PROJECT_CURVE_POINT_CAPACITY);

    std::vector<uint8_t> roundTrip;
    const auto reencoded = encodeDomain(*decoded, roundTrip);
    assert(reencoded.bytesWritten == encoded.bytesWritten);
    assert(roundTrip == bytes);
}

}  // namespace

int main() {
    testExactLayoutEmptyRoundTripAndPreflightAtomicity();
    testLegacyLiftIsDeterministicCanonicalAndAtomic();
    testLegacyV14V15DecodeAndAmbiguityPolicy();
    testCurrentRoundTripSharingAndIndependentRecovery();
    testCombinedCapacityConflictPreservesAutomation();
    testDenseMaximumAndCompleteProjectCeiling();
    std::cout << "All Project control persistence tests passed.\n";
    return 0;
}

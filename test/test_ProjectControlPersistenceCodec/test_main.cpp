#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "persistence/ProjectControlPersistenceCodec.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace {

namespace codec = core::persistence::project_control_codec;
namespace mod = core::state::modulation;

using Domain = mod::ProjectControlDomainState;
using DomainPtr = std::unique_ptr<Domain>;

mod::ModulationDestination destination(uint16_t address) {
    constexpr uint16_t perTrack =
        mod::PROJECT_MODULATION_PAGE_COUNT *
        mod::PROJECT_MODULATION_MACRO_COUNT;
    return {
        mod::ModulationDestinationKind::MACRO_SLOT,
        static_cast<uint8_t>(address / perTrack),
        static_cast<uint8_t>(
            (address % perTrack) / mod::PROJECT_MODULATION_MACRO_COUNT
        ),
        static_cast<uint8_t>(address % mod::PROJECT_MODULATION_MACRO_COUNT),
    };
}

mod::ProjectCurveId appendCurve(
    Domain& domain,
    mod::ProjectCurveValueDomain valueDomain,
    const mod::ProjectPackedCurvePoint* points,
    uint16_t pointCount
) {
    assert(points != nullptr && pointCount > 0U);
    assert(domain.curves.recordCount < mod::PROJECT_CURVE_LIVE_CAPACITY);
    assert(static_cast<uint32_t>(domain.curves.pointCount) + pointCount <=
           mod::PROJECT_CURVE_POINT_CAPACITY);

    const mod::ProjectCurveId id{domain.curves.nextCurveId++};
    auto& record = domain.curves.records[domain.curves.recordCount++];
    record = {};
    record.id = id;
    record.pointOffset = domain.curves.pointCount;
    record.pointCount = pointCount;
    record.sourceDurationTicks = points[pointCount - 1U].tick;
    record.durationTicks = record.sourceDurationTicks;
    record.referenceCount = 1U;
    record.valueDomain = valueDomain;
    record.origin = mod::ProjectCurveOrigin::NATIVE;
    for (uint16_t index = 0U; index < pointCount; ++index) {
        domain.curves.points[domain.curves.pointCount++] = points[index];
    }
    return id;
}

void appendAutomation(Domain& domain) {
    const std::array<mod::ProjectPackedCurvePoint, 3U> points{{
        {0U, 4096},
        {96U, 24576},
        {192U, 12288},
    }};
    const auto curve = appendCurve(
        domain,
        mod::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR,
        points.data(),
        static_cast<uint16_t>(points.size())
    );
    domain.automation.entryCount = 1U;
    domain.automation.entries[0] = {
        .destination = destination(1U),
        .curveId = curve,
        .flags = mod::PROJECT_AUTOMATION_CURVE_FLAG_ENABLED,
    };
}

DomainPtr makeRichDomain() {
    auto domain = std::make_unique<Domain>();
    appendAutomation(*domain);

    const std::array<mod::ProjectPackedCurvePoint, 3U> shapePoints{{
        {0U, 0},
        {192U, 32767},
        {384U, 0},
    }};
    mod::RecordedShapeDraft shape{};
    shape.name = "Envelope";
    shape.curve.sourceDurationTicks = 384U;
    shape.curve.durationTicks = 768U;
    shape.curve.valueDomain =
        mod::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
    shape.points = shapePoints.data();
    shape.pointCount = static_cast<uint16_t>(shapePoints.size());
    const auto recorded = mod::createRecordedShapeModulator(
        domain->modulation,
        domain->curves,
        shape
    );
    assert(recorded.changed());

    mod::ModulatorLfoDraft lfo{};
    lfo.name = "Slow LFO";
    lfo.parameters.periodTicks = 1536U;
    lfo.parameters.freePeriodMs = 12345U;
    lfo.parameters.phaseQ15 = -4096;
    lfo.parameters.shape = mod::ModulatorLfoShape::TRIANGLE;
    lfo.parameters.timing = mod::ModulatorTimingMode::FREE;
    lfo.parameters.retrigger =
        mod::ModulatorRetriggerPolicy::EXPLICIT_TRIGGER;
    const auto createdLfo = mod::createLfoModulator(domain->modulation, lfo);
    assert(createdLfo.changed());

    mod::ModulatorAdsrDraft adsr{};
    adsr.name = "Note Envelope";
    adsr.parameters.delay = 5U;
    adsr.parameters.attack = 7U;
    adsr.parameters.hold = 11U;
    adsr.parameters.decay = 384U;
    adsr.parameters.release = 1536U;
    adsr.parameters.sustainQ15 = 24576U;
    adsr.parameters.smooth = 23U;
    adsr.parameters.traits = mod::makeModulatorAdsrTraits(
        mod::ModulatorTimingMode::SYNC,
        mod::ModulatorAdsrRetriggerMode::LEGATO,
        mod::ModulatorAdsrCurve::SMOOTH
    );
    const auto createdAdsr = mod::createAdsrModulator(
        domain->modulation,
        adsr
    );
    assert(createdAdsr.changed());

    const std::array<mod::ModulatorId, 3U> sourceIds{{
        recorded.sourceId,
        createdLfo.sourceId,
        createdAdsr.sourceId,
    }};
    for (uint8_t index = 0U; index < sourceIds.size(); ++index) {
        mod::ModulationBindingDraft binding{};
        binding.sourceId = sourceIds[index];
        binding.destination = destination(static_cast<uint16_t>(7U + index));
        binding.amountQ15 = static_cast<int16_t>(8192 + 1024 * index);
        binding.application = index == 0U
            ? mod::ModulationApplication::NATURAL
            : mod::ModulationApplication::FROM_BASE;
        binding.slewMs = static_cast<uint16_t>(100U + index);
        assert(mod::addProjectModulationBinding(
            domain->modulation,
            binding
        ).changed());
        assert(mod::setProjectModulationDestinationScale(
            domain->modulation,
            binding.destination,
            static_cast<uint16_t>(40000U + index)
        ).changed());
    }

    mod::ModulationTriggerDraft trigger{};
    trigger.sourceId = createdAdsr.sourceId;
    trigger.trigger.kind = mod::ModulationTriggerKind::TRACK_NOTE;
    trigger.trigger.track = 3U;
    trigger.trigger.noteMin = 36U;
    trigger.trigger.noteMax = 84U;
    trigger.velocityMin = 17U;
    trigger.velocityMax = 111U;
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

codec::EncodeResult encode(
    const Domain& domain,
    std::vector<uint8_t>& bytes
) {
    bytes.assign(codec::PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE, 0xCDU);
    const auto result = codec::encodeProjectControlPayloads(
        domain,
        bytes.data(),
        static_cast<uint32_t>(bytes.size())
    );
    assert(result.encoded());
    bytes.resize(result.bytesWritten);
    return result;
}

codec::ChunkPayloadView automationView(
    const std::vector<uint8_t>& bytes,
    const codec::EncodeResult& encoded
) {
    return {
        .present = true,
        .versionMajor = codec::PROJECT_CONTROL_CHUNK_VERSION_MAJOR,
        .versionMinor = codec::PROJECT_AUTOMATION_CHUNK_VERSION_MINOR,
        .flags = 0U,
        .data = bytes.data() + encoded.automationOffset,
        .size = encoded.automationSize,
    };
}

codec::ChunkPayloadView modulationView(
    const std::vector<uint8_t>& bytes,
    const codec::EncodeResult& encoded
) {
    return {
        .present = true,
        .versionMajor = codec::PROJECT_CONTROL_CHUNK_VERSION_MAJOR,
        .versionMinor = codec::PROJECT_MODULATION_GRAPH_CHUNK_VERSION_MINOR,
        .flags = 0U,
        .data = bytes.data() + encoded.modulationOffset,
        .size = encoded.modulationSize,
    };
}

void testEmptyRoundTripAndPreflightAtomicity() {
    static_assert(codec::PROJECT_AUTOMATION_MAX_PAYLOAD_SIZE == 134688U);
    static_assert(codec::PROJECT_MODULATION_GRAPH_MAX_PAYLOAD_SIZE == 154912U);
    static_assert(codec::PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE == 158528U);
    static_assert(codec::PROJECT_MODULATOR_SOURCE_DIRECTORY_SIZE == 30U);

    Domain empty{};
    std::vector<uint8_t> bytes;
    const auto encoded = encode(empty, bytes);
    assert(encoded.automationSize == codec::PROJECT_CONTROL_CHUNK_HEADER_SIZE);
    assert(encoded.modulationSize == codec::PROJECT_CONTROL_CHUNK_HEADER_SIZE);
    assert(encoded.bytesWritten == 64U);

    Domain decoded{};
    const auto result = codec::decodeProjectControlPayloads(
        automationView(bytes, encoded),
        modulationView(bytes, encoded),
        decoded
    );
    assert(result.decoded());
    assert(result.automationStatus == codec::ChunkStatus::CURRENT);
    assert(result.modulationStatus == codec::ChunkStatus::CURRENT);
    assert(!result.partial && result.overwriteSafe);
    assert(std::memcmp(&empty, &decoded, sizeof(empty)) == 0);

    std::vector<uint8_t> shortBuffer(63U, 0xA5U);
    const auto before = shortBuffer;
    const auto failed = codec::encodeProjectControlPayloads(
        empty,
        shortBuffer.data(),
        static_cast<uint32_t>(shortBuffer.size())
    );
    assert(failed.status == codec::Status::BUFFER_TOO_SMALL);
    assert(failed.bytesRequired == 64U);
    assert(shortBuffer == before);

    std::cout << "[PASS] current empty round-trip and atomic preflight\n";
}

void testRichCurrentRoundTripIsCanonical() {
    const auto source = makeRichDomain();
    std::vector<uint8_t> bytes;
    const auto encoded = encode(*source, bytes);

    Domain decoded{};
    const auto result = codec::decodeProjectControlPayloads(
        automationView(bytes, encoded),
        modulationView(bytes, encoded),
        decoded
    );
    assert(result.decoded() && !result.partial && result.overwriteSafe);
    assert(decoded.automation.entryCount == 1U);
    assert(decoded.modulation.sourceCount == 3U);
    assert(decoded.modulation.outputBindingCount == 3U);
    assert(decoded.modulation.triggerBindingCount == 1U);
    assert(decoded.modulation.destinationScaleCount == 3U);

    const auto& trigger = decoded.modulation.triggerBindings[0];
    assert(trigger.trigger.kind == mod::ModulationTriggerKind::TRACK_NOTE);
    assert(trigger.trigger.track == 3U);
    assert(trigger.trigger.noteMin == 36U);
    assert(trigger.trigger.noteMax == 84U);
    assert(trigger.velocityMin == 17U);
    assert(trigger.velocityMax == 111U);

    const auto* adsr = mod::findProjectModulator(
        decoded.modulation,
        source->modulation.sources[2].id
    );
    assert(adsr != nullptr && adsr->kind == mod::ModulatorKind::ADSR);
    assert(adsr->parameters.adsr.delay == 5U);
    assert(adsr->parameters.adsr.attack == 7U);
    assert(adsr->parameters.adsr.hold == 11U);
    assert(adsr->parameters.adsr.decay == 384U);
    assert(adsr->parameters.adsr.release == 1536U);
    assert(adsr->parameters.adsr.sustainQ15 == 24576U);
    assert(adsr->parameters.adsr.smooth == 23U);

    std::vector<uint8_t> canonical;
    const auto reencoded = encode(decoded, canonical);
    assert(reencoded.bytesWritten == encoded.bytesWritten);
    assert(canonical == bytes);

    std::cout << "[PASS] rich current round-trip is canonical\n";
}

void testPreviousVersionsAreRejectedStrictly() {
    const auto source = makeRichDomain();
    std::vector<uint8_t> bytes;
    const auto encoded = encode(*source, bytes);

    auto oldAutomation = automationView(bytes, encoded);
    --oldAutomation.versionMinor;
    Domain automationRejected{};
    const auto automationResult = codec::decodeProjectControlPayloads(
        oldAutomation,
        modulationView(bytes, encoded),
        automationRejected
    );
    assert(automationResult.decoded());
    assert(automationResult.automationStatus ==
           codec::ChunkStatus::UNSUPPORTED_VERSION);
    assert(automationResult.modulationStatus == codec::ChunkStatus::CURRENT);
    assert(automationResult.partial && !automationResult.overwriteSafe);
    assert(automationRejected.automation.entryCount == 0U);
    assert(automationRejected.modulation.sourceCount == 3U);

    auto oldModulation = modulationView(bytes, encoded);
    --oldModulation.versionMinor;
    Domain modulationRejected{};
    const auto modulationResult = codec::decodeProjectControlPayloads(
        automationView(bytes, encoded),
        oldModulation,
        modulationRejected
    );
    assert(modulationResult.decoded());
    assert(modulationResult.automationStatus == codec::ChunkStatus::CURRENT);
    assert(modulationResult.modulationStatus ==
           codec::ChunkStatus::UNSUPPORTED_VERSION);
    assert(modulationResult.partial && !modulationResult.overwriteSafe);
    assert(modulationRejected.automation.entryCount == 1U);
    assert(modulationRejected.modulation.sourceCount == 0U);

    std::cout << "[PASS] previous versions are rejected strictly\n";
}

void testInvalidCurrentChunkRecoversItsPeerOnly() {
    const auto source = makeRichDomain();
    std::vector<uint8_t> bytes;
    const auto encoded = encode(*source, bytes);

    const uint32_t sourceDirectory =
        encoded.modulationOffset + codec::PROJECT_CONTROL_CHUNK_HEADER_SIZE;
    constexpr uint32_t firstReservedByte = 26U;
    bytes[sourceDirectory + firstReservedByte] = 1U;

    Domain decoded{};
    const auto result = codec::decodeProjectControlPayloads(
        automationView(bytes, encoded),
        modulationView(bytes, encoded),
        decoded
    );
    assert(result.decoded());
    assert(result.automationStatus == codec::ChunkStatus::CURRENT);
    assert(result.modulationStatus == codec::ChunkStatus::INVALID_PAYLOAD);
    assert(result.partial && !result.overwriteSafe);
    assert(decoded.automation.entryCount == 1U);
    assert(decoded.modulation.sourceCount == 0U);
    assert(decoded.curves.recordCount == 1U);

    std::cout << "[PASS] invalid current chunk recovers its peer only\n";
}

void testMaximumAutomationPointBudgetRoundTrips() {
    auto domain = std::make_unique<Domain>();
    std::vector<mod::ProjectPackedCurvePoint> points(
        mod::PROJECT_CURVE_POINT_CAPACITY,
        {0U, 16000}
    );
    points.back().tick = 1U;
    const auto curve = appendCurve(
        *domain,
        mod::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR,
        points.data(),
        static_cast<uint16_t>(points.size())
    );
    domain->automation.entryCount = 1U;
    domain->automation.entries[0] = {
        .destination = destination(0U),
        .curveId = curve,
        .flags = mod::PROJECT_AUTOMATION_CURVE_FLAG_ENABLED,
    };
    assert(mod::validProjectModulationDomain(
        domain->modulation,
        domain->curves,
        &domain->automation
    ));

    std::vector<uint8_t> bytes;
    const auto encoded = encode(*domain, bytes);
    assert(encoded.automationSize <= codec::PROJECT_AUTOMATION_MAX_PAYLOAD_SIZE);
    assert(encoded.bytesWritten <= codec::PROJECT_CONTROL_COMBINED_MAX_PAYLOAD_SIZE);

    Domain decoded{};
    const auto result = codec::decodeProjectControlPayloads(
        automationView(bytes, encoded),
        modulationView(bytes, encoded),
        decoded
    );
    assert(result.decoded() && !result.partial);
    assert(decoded.curves.pointCount == mod::PROJECT_CURVE_POINT_CAPACITY);

    std::cout << "[PASS] maximum shared point budget round-trips\n";
}

}  // namespace

int main() {
    testEmptyRoundTripAndPreflightAtomicity();
    testRichCurrentRoundTripIsCanonical();
    testPreviousVersionsAreRejectedStrictly();
    testInvalidCurrentChunkRecoversItsPeerOnly();
    testMaximumAutomationPointBudgetRoundTrips();
    std::cout << "All ProjectControlPersistenceCodec tests passed\n";
    return 0;
}

#include "validation/project/ProjectModulationBenchmark.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "config/TimeCompat.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::validation::project {

namespace mod = core::state::modulation;

namespace {

constexpr uint16_t POINTS_PER_RECORDED_SOURCE =
    mod::PROJECT_CURVE_POINT_CAPACITY / mod::PROJECT_MODULATOR_CAPACITY;

static_assert(POINTS_PER_RECORDED_SOURCE == 256U);
static_assert(
    POINTS_PER_RECORDED_SOURCE * mod::PROJECT_MODULATOR_CAPACITY ==
    mod::PROJECT_CURVE_POINT_CAPACITY
);

constexpr bool isDahdsrBenchmarkCase(
    ProjectModulationBenchmarkCase benchmarkCase
) {
    return benchmarkCase == ProjectModulationBenchmarkCase::ADSR ||
        benchmarkCase ==
            ProjectModulationBenchmarkCase::DAHDSR_SHARED_TRACK;
}

mod::ModulationDestination benchmarkDestination(uint16_t stableAddress) {
    return {
        mod::ModulationDestinationKind::MACRO_SLOT,
        static_cast<uint8_t>(
            stableAddress / mod::PROJECT_MODULATION_MACRO_COUNT
        ),
        0U,
        static_cast<uint8_t>(
            stableAddress % mod::PROJECT_MODULATION_MACRO_COUNT
        ),
    };
}

mod::ProjectModulationCompileContext benchmarkCompileContext() {
    mod::ProjectModulationCompileContext context{};
    context.enabledTrackMask = 0xFFFFU;
    context.activePage.fill(0U);
    context.activeMacroMask.fill(0xFFU);
    return context;
}

void populateRecordedCurve(
    mod::ProjectCurveArena& arena,
    uint16_t sourceIndex
) {
    const uint16_t pointOffset = static_cast<uint16_t>(
        sourceIndex * POINTS_PER_RECORDED_SOURCE
    );
    arena.records[sourceIndex] = {
        .id = {static_cast<uint32_t>(sourceIndex + 1U)},
        .pointOffset = pointOffset,
        .pointCount = POINTS_PER_RECORDED_SOURCE,
        .sourceDurationTicks = std::numeric_limits<uint16_t>::max(),
        .durationTicks = std::numeric_limits<uint16_t>::max(),
        .windowOffsetTicks = 0U,
        .referenceCount = 1U,
        .interpolation = mod::ProjectCurveInterpolation::LINEAR,
        .valueDomain = mod::ProjectCurveValueDomain::BIPOLAR,
        .flags = 0U,
        .origin = mod::ProjectCurveOrigin::NATIVE,
    };
    for (uint16_t point = 0; point < POINTS_PER_RECORDED_SOURCE; ++point) {
        const uint16_t target = static_cast<uint16_t>(pointOffset + point);
        arena.points[target] = {
            static_cast<uint16_t>(point * 257U),
            static_cast<int16_t>(
                -32767 + (65534L * static_cast<int32_t>(point)) /
                    static_cast<int32_t>(POINTS_PER_RECORDED_SOURCE - 1U)
            ),
        };
    }
}

void populateSource(
    mod::ProjectControlDomainState& domain,
    uint16_t sourceIndex,
    ProjectModulationBenchmarkCase benchmarkCase
) {
    auto& source = domain.modulation.sources[sourceIndex];
    source.id = {static_cast<uint32_t>(sourceIndex + 1U)};
    source.name[0] = 'B';
    source.name[1] = 'e';
    source.name[2] = 'n';
    source.name[3] = 'c';
    source.name[4] = 'h';
    source.flags = mod::PROJECT_MODULATOR_FLAG_ENABLED;
    source.schemaVersion = 1U;
    source.parameters.raw.fill(0U);

    if (benchmarkCase == ProjectModulationBenchmarkCase::LFO) {
        source.kind = mod::ModulatorKind::LFO;
        source.parameters.lfo = mod::ModulatorLfoParameters{};
        // Exercise both legal UI extremes in the same maximum graph. Phase is
        // analytic, so rate changes must not allocate or increase frame cost.
        source.parameters.lfo.periodTicks = sourceIndex % 4U == 0U
            ? 12U       // 1/64.
            : 24576U;   // 32 bars.
        source.parameters.lfo.freePeriodMs = sourceIndex % 4U == 1U
            ? 8U
            : 32000U;
        source.parameters.lfo.phaseQ15 = static_cast<int16_t>(sourceIndex * 127U);
        source.parameters.lfo.shape = static_cast<mod::ModulatorLfoShape>(
            sourceIndex % 5U
        );
        source.parameters.lfo.retrigger =
            sourceIndex % 2U == 0U
                ? mod::ModulatorRetriggerPolicy::FREE_RUNNING
                : mod::ModulatorRetriggerPolicy::TRANSPORT;
        source.parameters.lfo.timing = sourceIndex % 2U == 0U
            ? mod::ModulatorTimingMode::SYNC
            : mod::ModulatorTimingMode::FREE;
        return;
    }

    if (isDahdsrBenchmarkCase(benchmarkCase)) {
        source.kind = mod::ModulatorKind::ADSR;
        source.schemaVersion = mod::PROJECT_MODULATOR_ADSR_SCHEMA_VERSION;
        source.parameters.adsr = mod::ModulatorAdsrParameters{};
        const bool free = sourceIndex % 2U == 0U;
        source.parameters.adsr.delay = free
            ? static_cast<uint16_t>(4U + sourceIndex % 8U)
            : 12U;
        source.parameters.adsr.attack = free
            ? static_cast<uint16_t>(8U + sourceIndex % 16U)
            : 24U;
        source.parameters.adsr.hold = free
            ? static_cast<uint16_t>(16U + sourceIndex % 32U)
            : 48U;
        source.parameters.adsr.decay = free
            ? static_cast<uint16_t>(96U + sourceIndex)
            : 96U;
        source.parameters.adsr.release = free
            ? static_cast<uint16_t>(192U + sourceIndex)
            : 192U;
        source.parameters.adsr.smooth = free
            ? static_cast<uint16_t>(32U + sourceIndex % 64U)
            : 384U;
        source.parameters.adsr.sustainQ15 = static_cast<uint16_t>(
            8192U + sourceIndex * 128U
        );
        source.parameters.adsr.traits = mod::makeModulatorAdsrTraits(
            free
                ? mod::ModulatorTimingMode::FREE
                : mod::ModulatorTimingMode::SYNC,
            free
                ? mod::ModulatorAdsrRetriggerMode::RETRIGGER
                : mod::ModulatorAdsrRetriggerMode::LEGATO,
            static_cast<mod::ModulatorAdsrCurve>(sourceIndex % 3U)
        );
        constexpr mod::ModulatorEnvelopeTimeParameter PARAMETERS[]{
            mod::ModulatorEnvelopeTimeParameter::DELAY,
            mod::ModulatorEnvelopeTimeParameter::ATTACK,
            mod::ModulatorEnvelopeTimeParameter::HOLD,
            mod::ModulatorEnvelopeTimeParameter::DECAY,
            mod::ModulatorEnvelopeTimeParameter::RELEASE,
            mod::ModulatorEnvelopeTimeParameter::SMOOTH,
        };
        for (uint8_t parameter = 0U; parameter < 6U; ++parameter) {
            source.parameters.adsr.traits = mod::withModulatorAdsrFeel(
                source.parameters.adsr.traits,
                PARAMETERS[parameter],
                static_cast<mod::ModulatorEnvelopeFeel>(
                    (sourceIndex + parameter) % 3U
                )
            );
        }

        auto& trigger = domain.modulation.triggerBindings[sourceIndex];
        trigger.id = {
            static_cast<uint32_t>(
                mod::PROJECT_MODULATION_BINDING_CAPACITY + sourceIndex + 1U
            )
        };
        trigger.sourceId = source.id;
        trigger.trigger = {
            mod::ModulationTriggerKind::TRACK_NOTE,
            benchmarkCase ==
                    ProjectModulationBenchmarkCase::DAHDSR_SHARED_TRACK
                ? static_cast<uint8_t>(0U)
                : static_cast<uint8_t>(
                    sourceIndex % mod::PROJECT_MODULATION_TRACK_COUNT
                ),
            static_cast<uint8_t>(sourceIndex % 128U),
            static_cast<uint8_t>(sourceIndex % 128U),
        };
        trigger.velocityMin = 20U;
        trigger.velocityMax = 110U;
        trigger.flags = mod::PROJECT_MODULATION_TRIGGER_FLAG_ENABLED;
        return;
    }

    source.kind = mod::ModulatorKind::RECORDED_SHAPE;
    source.parameters.recordedCurveId = {
        static_cast<uint32_t>(sourceIndex + 1U)
    };
    populateRecordedCurve(domain.curves, sourceIndex);
}

void populateBinding(
    mod::ProjectControlDomainState& domain,
    uint16_t sourceIndex,
    uint8_t edge
) {
    const uint16_t bindingIndex = static_cast<uint16_t>(sourceIndex * 4U + edge);
    const uint16_t stableAddress = static_cast<uint16_t>(
        (sourceIndex + edge) % mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY
    );
    auto& binding = domain.modulation.outputBindings[bindingIndex];
    binding.id = {static_cast<uint32_t>(bindingIndex + 1U)};
    binding.sourceId = {static_cast<uint32_t>(sourceIndex + 1U)};
    binding.destination = benchmarkDestination(stableAddress);
    binding.amountQ15 = static_cast<int16_t>(8192 + edge * 2048);
    binding.application = static_cast<mod::ModulationApplication>(edge % 3U);
    binding.transfer = mod::ModulationTransfer::LINEAR;
    binding.slewMs = static_cast<uint16_t>(8U + edge);
    binding.flags = mod::PROJECT_MODULATION_BINDING_FLAG_ENABLED;
}

struct EvaluationContext {
    const ProjectModulationBenchmarkWorkspace* workspace = nullptr;
    uint16_t sinkCount = 0;
    uint32_t checksum = 2166136261U;
};

bool provideBenchmarkBase(
    void* rawContext,
    uint16_t destinationIndex,
    const mod::ModulationDestination&,
    mod::ProjectLogicalMacroBaseInput& out
) {
    auto& context = *static_cast<EvaluationContext*>(rawContext);
    if (context.workspace == nullptr ||
        destinationIndex >= context.workspace->bases.size()) {
        return false;
    }
    out = context.workspace->bases[destinationIndex];
    return true;
}

void captureBenchmarkDestination(
    void* rawContext,
    uint16_t,
    const mod::ProjectLogicalMacroRuntimeValue& value
) {
    auto& context = *static_cast<EvaluationContext*>(rawContext);
    ++context.sinkCount;
    const auto quantized = static_cast<uint32_t>(
        std::clamp(value.value, 0.0f, 1.0f) * 65535.0f
    );
    context.checksum = (context.checksum ^ quantized) * 16777619U;
}

bool evaluateFrame(
    ProjectModulationBenchmarkWorkspace& workspace,
    uint32_t ordinal,
    EvaluationContext& context
) {
    context.sinkCount = 0U;
    mod::ProjectControlTimeSnapshot time{};
    time.musicalTick = 97U + ordinal * 17U;
    time.musicalTickFractionQ16 = static_cast<uint16_t>(ordinal * 811U);
    time.monotonicMs = 1000U + ordinal * 4U;
    time.transportGeneration = 1U;
    time.transportStartMusicalTick = 11U;
    time.transportStartMonotonicMs = 100U;
    time.playing = true;
    const auto result = mod::evaluateProjectControlRuntimeWithBaseProvider(
        workspace.plan,
        workspace.domain.curves,
        time,
        &workspace.triggers,
        provideBenchmarkBase,
        &context,
        workspace.runtime,
        workspace.sourceValues.data(),
        static_cast<uint16_t>(workspace.sourceValues.size()),
        captureBenchmarkDestination,
        &context
    );
    return result.evaluated() &&
        result.sourceEvaluationCount == mod::PROJECT_MODULATOR_CAPACITY &&
        result.contributionCount == mod::PROJECT_MODULATION_BINDING_CAPACITY &&
        result.destinationEvaluationCount ==
            mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY &&
        context.sinkCount == mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY;
}

uint32_t triggerTestsPerFrame(
    const mod::ProjectModulationRuntimePlan& plan,
    const mod::ProjectModulationTriggerFrame& triggers
) {
    uint32_t testCount = 0U;
    for (uint16_t eventIndex = 0U;
         eventIndex < triggers.count;
         ++eventIndex) {
        const auto& trigger = triggers.events[eventIndex].trigger;
        if (trigger.track >= mod::PROJECT_MODULATION_TRACK_COUNT ||
            trigger.channel >= 16U ||
            static_cast<uint8_t>(trigger.kind) >=
                mod::PROJECT_MODULATION_TRIGGER_KIND_COUNT) {
            continue;
        }
        const uint16_t bucket = mod::projectModulationTriggerBucketIndex(
            trigger
        );
        const uint16_t start = plan.triggerBucketOffset[bucket];
        const uint16_t end = plan.triggerBucketOffset[bucket + 1U];
        if (start > end || end > plan.triggerRouteCount) return 0U;
        testCount += static_cast<uint32_t>(end - start);
    }
    return testCount;
}

}  // namespace

const char* projectModulationBenchmarkCaseLabel(
    ProjectModulationBenchmarkCase benchmarkCase
) {
    switch (benchmarkCase) {
        case ProjectModulationBenchmarkCase::RECORDED_SHAPE:
            return "recorded-shape-128x512";
        case ProjectModulationBenchmarkCase::ADSR:
            return "dahdsr-128x512-256-events";
        case ProjectModulationBenchmarkCase::DAHDSR_SHARED_TRACK:
            return "dahdsr-shared-track-128x512-32768-tests";
        case ProjectModulationBenchmarkCase::LFO:
        default:
            return "lfo-128x512";
    }
}

bool prepareProjectModulationBenchmark(
    ProjectModulationBenchmarkWorkspace& workspace,
    ProjectModulationBenchmarkCase benchmarkCase
) {
    // Avoid a 185 kB aggregate temporary on the embedded stack. Every member
    // is trivially copyable and uses an all-zero empty representation; required
    // non-zero domain defaults are restored explicitly below.
    std::fill_n(
        reinterpret_cast<unsigned char*>(&workspace),
        sizeof(workspace),
        static_cast<unsigned char>(0U)
    );
    auto& domain = workspace.domain;
    domain.modulation.nextSourceId = mod::PROJECT_MODULATOR_CAPACITY + 1U;
    domain.modulation.nextBindingId = isDahdsrBenchmarkCase(benchmarkCase)
        ? mod::PROJECT_MODULATION_BINDING_CAPACITY +
            mod::PROJECT_MODULATION_TRIGGER_CAPACITY + 1U
        : mod::PROJECT_MODULATION_BINDING_CAPACITY + 1U;
    domain.modulation.sourceCount = mod::PROJECT_MODULATOR_CAPACITY;
    domain.modulation.outputBindingCount = mod::PROJECT_MODULATION_BINDING_CAPACITY;
    domain.modulation.triggerBindingCount = isDahdsrBenchmarkCase(benchmarkCase)
        ? mod::PROJECT_MODULATION_TRIGGER_CAPACITY
        : 0U;
    domain.modulation.destinationScaleCount =
        mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY;
    domain.curves.nextCurveId = 1U;

    if (benchmarkCase == ProjectModulationBenchmarkCase::RECORDED_SHAPE) {
        domain.curves.nextCurveId = mod::PROJECT_MODULATOR_CAPACITY + 1U;
        domain.curves.recordCount = mod::PROJECT_MODULATOR_CAPACITY;
        domain.curves.pointCount = mod::PROJECT_CURVE_POINT_CAPACITY;
    }

    for (uint16_t source = 0; source < mod::PROJECT_MODULATOR_CAPACITY; ++source) {
        populateSource(domain, source, benchmarkCase);
        for (uint8_t edge = 0; edge < 4U; ++edge) {
            populateBinding(domain, source, edge);
        }
    }
    for (uint16_t destination = 0;
         destination < mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY;
         ++destination) {
        domain.modulation.destinationScales[destination] = {
            .destination = benchmarkDestination(destination),
            .scaleQ15 = 24576U,
        };
        workspace.bases[destination].staticValue = 0.5f;
    }
    if (isDahdsrBenchmarkCase(benchmarkCase)) {
        workspace.triggers.count =
            mod::PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY;
        for (uint16_t source = 0U;
             source < mod::PROJECT_MODULATOR_CAPACITY;
             ++source) {
            const auto filter = domain.modulation.triggerBindings[source].trigger;
            const mod::ModulationTriggerRef trigger{
                .kind = filter.kind,
                .track = filter.track,
                .channel = static_cast<uint8_t>(source % 16U),
                .data = static_cast<uint8_t>(source % 128U),
            };
            workspace.triggers.events[source * 2U] = {
                .trigger = trigger,
                .edge = mod::ProjectModulationTriggerEdge::GATE_ON,
                .velocity = 100U,
            };
            workspace.triggers.events[source * 2U + 1U] = {
                .trigger = trigger,
                .edge = mod::ProjectModulationTriggerEdge::GATE_OFF,
                .velocity = 0U,
            };
        }
    }

    if (!mod::validProjectModulationDomain(
            domain.modulation,
            domain.curves,
            &domain.automation
        )) {
        return false;
    }
    const auto compiled = mod::compileProjectControlRuntimePlan(
        domain,
        benchmarkCompileContext(),
        workspace.plan
    );
    if (!compiled.compiled() ||
        workspace.plan.sourceCount != mod::PROJECT_MODULATOR_CAPACITY ||
        workspace.plan.bindingCount != mod::PROJECT_MODULATION_BINDING_CAPACITY ||
        workspace.plan.destinationCount !=
            mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY) {
        return false;
    }

    mod::ProjectControlTimeSnapshot activation{};
    mod::resetProjectControlRuntimeState(workspace.runtime, activation);
    return mod::synchronizeProjectControlRuntimeState(
        workspace.runtime,
        workspace.plan,
        activation
    ) == mod::ProjectControlRuntimeStatus::OK;
}

ProjectModulationBenchmarkResult runProjectModulationBenchmark(
    ProjectModulationBenchmarkWorkspace& workspace,
    ProjectModulationBenchmarkCase benchmarkCase,
    uint32_t warmupFrames,
    uint32_t measuredFrames
) {
    ProjectModulationBenchmarkResult result{};
    result.benchmarkCase = benchmarkCase;
    result.iterations = measuredFrames;
    result.sourceCount = workspace.plan.sourceCount;
    result.bindingCount = workspace.plan.bindingCount;
    result.destinationCount = workspace.plan.destinationCount;
    result.triggerEventCount = workspace.triggers.count;
    result.triggerRouteCount = workspace.plan.triggerRouteCount;
    result.triggerTestsPerFrame = triggerTestsPerFrame(
        workspace.plan,
        workspace.triggers
    );
    result.prepared =
        measuredFrames > 0U &&
        result.sourceCount == mod::PROJECT_MODULATOR_CAPACITY &&
        result.bindingCount == mod::PROJECT_MODULATION_BINDING_CAPACITY &&
        result.destinationCount ==
            mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY &&
        result.workloadMatchesCase();
    if (!result.prepared) return result;

    EvaluationContext context{.workspace = &workspace};
    for (uint32_t frame = 0; frame < warmupFrames; ++frame) {
        if (!evaluateFrame(workspace, frame, context)) return result;
    }

    uint64_t totalUs = 0U;
    for (uint32_t frame = 0; frame < measuredFrames; ++frame) {
        const uint32_t startUs = core::time_compat::platformMicros();
        if (!evaluateFrame(workspace, warmupFrames + frame, context)) {
            return result;
        }
        const uint32_t elapsedUs =
            core::time_compat::platformMicros() - startUs;
        totalUs += elapsedUs;
        result.maximumUs = std::max(result.maximumUs, elapsedUs);
    }
    result.averageUs = static_cast<uint32_t>(totalUs / measuredFrames);
    result.checksum = context.checksum;
    result.evaluated = true;
    return result;
}

}  // namespace core::validation::project

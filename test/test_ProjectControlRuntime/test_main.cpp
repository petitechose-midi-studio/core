#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace {

namespace mod = core::state::modulation;

bool near(float lhs, float rhs, float epsilon = 0.002f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

mod::ModulationDestination destination(uint8_t track,
                                       uint8_t page,
                                       uint8_t macro) {
    return {
        mod::ModulationDestinationKind::MACRO_SLOT,
        track,
        page,
        macro,
    };
}

mod::ProjectModulationCompileContext activeContext() {
    mod::ProjectModulationCompileContext context{};
    context.enabledTrackMask = 0xFFFFU;
    context.activePage.fill(0U);
    context.activeMacroMask.fill(0xFFU);
    return context;
}

mod::ModulatorId addLfo(
    mod::ProjectControlDomainState& domain,
    mod::ModulatorLfoShape shape,
    uint32_t periodTicks = 192U,
    mod::ModulatorTimingMode timing = mod::ModulatorTimingMode::SYNC,
    uint32_t freePeriodMs = 1000U,
    mod::ModulatorRetriggerPolicy retrigger =
        mod::ModulatorRetriggerPolicy::FREE_RUNNING
) {
    mod::ModulatorLfoDraft draft{};
    draft.name = "LFO";
    draft.parameters.shape = shape;
    draft.parameters.periodTicks = periodTicks;
    draft.parameters.freePeriodMs = freePeriodMs;
    draft.parameters.timing = timing;
    draft.parameters.retrigger = retrigger;
    const auto result = mod::createLfoModulator(domain.modulation, draft);
    assert(result.changed());
    return result.sourceId;
}

mod::ModulatorId addAdsr(
    mod::ProjectControlDomainState& domain,
    const mod::ModulatorAdsrParameters& parameters,
    uint8_t track = 2U,
    uint8_t noteMin = 0U,
    uint8_t noteMax = 127U,
    uint8_t velocityMin = 0U,
    uint8_t velocityMax = 127U
) {
    mod::ModulatorAdsrDraft draft{};
    draft.name = "ADSR";
    draft.parameters = parameters;
    const auto created = mod::createAdsrModulator(domain.modulation, draft);
    assert(created.changed());

    mod::ModulationTriggerDraft trigger{};
    trigger.sourceId = created.sourceId;
    trigger.trigger = {
        mod::ModulationTriggerKind::TRACK_NOTE,
        track,
        noteMin,
        noteMax,
    };
    trigger.velocityMin = velocityMin;
    trigger.velocityMax = velocityMax;
    assert(mod::addProjectModulationTrigger(
        domain.modulation,
        trigger
    ).changed());
    return created.sourceId;
}

mod::ProjectModulationTriggerEvent noteEdge(
    uint8_t track,
    uint8_t channel,
    uint8_t note,
    mod::ProjectModulationTriggerEdge edge,
    uint8_t velocity = 100U
) {
    return {
        .trigger = {
            mod::ModulationTriggerKind::TRACK_NOTE,
            track,
            channel,
            note,
        },
        .edge = edge,
        .velocity = velocity,
    };
}

mod::ModulationBindingId addBinding(
    mod::ProjectControlDomainState& domain,
    mod::ModulatorId source,
    const mod::ModulationDestination& target,
    int16_t amountQ15 = 32767,
    mod::ModulationApplication application = mod::ModulationApplication::NATURAL,
    uint16_t slewMs = 0U
) {
    mod::ModulationBindingDraft draft{};
    draft.sourceId = source;
    draft.destination = target;
    draft.amountQ15 = amountQ15;
    draft.application = application;
    draft.slewMs = slewMs;
    const auto result = mod::addProjectModulationBinding(
        domain.modulation,
        draft
    );
    assert(result.changed());
    return result.bindingId;
}

void addAbsoluteAutomation(mod::ProjectControlDomainState& domain,
                           const mod::ModulationDestination& target) {
    assert(domain.curves.recordCount == 0U);
    domain.curves.nextCurveId = 2U;
    domain.curves.recordCount = 1U;
    domain.curves.pointCount = 2U;
    domain.curves.records[0] = {
        .id = {1U},
        .pointOffset = 0U,
        .pointCount = 2U,
        .sourceDurationTicks = 192U,
        .durationTicks = 192U,
        .windowOffsetTicks = 0U,
        .referenceCount = 1U,
        .interpolation = mod::ProjectCurveInterpolation::LINEAR,
        .valueDomain = mod::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR,
        .flags = 0U,
        .origin = mod::ProjectCurveOrigin::NATIVE,
    };
    domain.curves.points[0] = {0U, 0};
    domain.curves.points[1] = {192U, 32767};
    domain.automation.entryCount = 1U;
    domain.automation.entries[0] = {
        .destination = target,
        .curveId = {1U},
        .flags = mod::PROJECT_AUTOMATION_CURVE_FLAG_ENABLED,
    };
}

struct RuntimeFixture {
    std::unique_ptr<mod::ProjectControlRuntimeState> state =
        std::make_unique<mod::ProjectControlRuntimeState>();
    std::unique_ptr<mod::ProjectControlRuntimeFrame> frame =
        std::make_unique<mod::ProjectControlRuntimeFrame>();
    std::array<
        mod::ProjectLogicalMacroBaseInput,
        mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY
    > bases{};
    mod::ProjectModulationTriggerFrame triggers{};

    void activate(const mod::ProjectModulationRuntimePlan& plan,
                  const mod::ProjectControlTimeSnapshot& time = {}) {
        mod::resetProjectControlRuntimeState(*state, time);
        assert(mod::synchronizeProjectControlRuntimeState(
            *state,
            plan,
            time
        ) == mod::ProjectControlRuntimeStatus::OK);
    }

    mod::ProjectControlRuntimeResult evaluate(
        const mod::ProjectModulationRuntimePlan& plan,
        const mod::ProjectCurveArena& arena,
        const mod::ProjectControlTimeSnapshot& time
    ) {
        return mod::evaluateProjectControlRuntimeFrame(
            plan,
            arena,
            time,
            triggers,
            bases.data(),
            plan.destinationCount,
            *state,
            *frame
        );
    }
};

void testFiveCanonicalLfoShapes() {
    assert(near(mod::evaluateProjectLfoShape(
        mod::ModulatorLfoShape::SINE,
        0.0f
    ), 0.0f));
    assert(near(mod::evaluateProjectLfoShape(
        mod::ModulatorLfoShape::SINE,
        0.25f
    ), 1.0f));
    assert(near(mod::evaluateProjectLfoShape(
        mod::ModulatorLfoShape::SINE,
        0.75f
    ), -1.0f));

    assert(near(mod::evaluateProjectLfoShape(
        mod::ModulatorLfoShape::TRIANGLE,
        0.0f
    ), 0.0f));
    assert(near(mod::evaluateProjectLfoShape(
        mod::ModulatorLfoShape::TRIANGLE,
        0.25f
    ), 1.0f));
    assert(near(mod::evaluateProjectLfoShape(
        mod::ModulatorLfoShape::TRIANGLE,
        0.75f
    ), -1.0f));

    assert(near(mod::evaluateProjectLfoShape(
        mod::ModulatorLfoShape::SAW_UP,
        0.25f
    ), -0.5f));
    assert(near(mod::evaluateProjectLfoShape(
        mod::ModulatorLfoShape::SAW_DOWN,
        0.25f
    ), 0.5f));
    assert(near(mod::evaluateProjectLfoShape(
        mod::ModulatorLfoShape::SQUARE,
        0.25f
    ), 1.0f));
    assert(near(mod::evaluateProjectLfoShape(
        mod::ModulatorLfoShape::SQUARE,
        0.75f
    ), -1.0f));
}

void testCanonicalAdsrProgressCurves() {
    assert(near(mod::evaluateProjectAdsrProgress(
        mod::ModulatorAdsrCurve::LINEAR,
        0.5f
    ), 0.5f));
    assert(near(mod::evaluateProjectAdsrProgress(
        mod::ModulatorAdsrCurve::SMOOTH,
        0.25f
    ), 0.15625f));
    assert(near(mod::evaluateProjectAdsrProgress(
        mod::ModulatorAdsrCurve::EXPONENTIAL,
        0.5f
    ), 0.25f));
    assert(near(mod::evaluateProjectAdsrProgress(
        mod::ModulatorAdsrCurve::EXPONENTIAL,
        -1.0f
    ), 0.0f));
    assert(near(mod::evaluateProjectAdsrProgress(
        mod::ModulatorAdsrCurve::SMOOTH,
        2.0f
    ), 1.0f));
}

void testFreeAdsrTrackTriggerRetriggerAndReleaseFromCurrentLevel() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    mod::ModulatorAdsrParameters parameters{};
    parameters.attack = 100U;
    parameters.decay = 100U;
    parameters.sustainQ15 = 16384U;
    parameters.release = 100U;
    parameters.traits = mod::makeModulatorAdsrTraits(
        mod::ModulatorTimingMode::FREE,
        mod::ModulatorAdsrRetriggerMode::RETRIGGER,
        mod::ModulatorAdsrCurve::LINEAR
    );
    const auto source = addAdsr(*domain, parameters);
    addBinding(*domain, source, destination(0U, 0U, 0U));

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);

    mod::ProjectControlTimeSnapshot time{};
    runtime.triggers.count = 2U;
    runtime.triggers.events[0] = noteEdge(
        3U,
        7U,
        60U,
        mod::ProjectModulationTriggerEdge::GATE_ON
    );
    runtime.triggers.events[1] = noteEdge(
        2U,
        7U,
        60U,
        mod::ProjectModulationTriggerEdge::GATE_ON
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.0f));
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 1U);

    time.monotonicMs = 50U;
    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        2U,
        12U,
        61U,
        mod::ProjectModulationTriggerEdge::GATE_ON
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.5f));
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 2U);

    time.monotonicMs = 75U;
    runtime.triggers.count = 0U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.625f));

    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        2U,
        7U,
        60U,
        mod::ProjectModulationTriggerEdge::GATE_OFF,
        0U
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 1U);
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::ATTACK);

    runtime.triggers.events[0] = noteEdge(
        2U,
        12U,
        61U,
        mod::ProjectModulationTriggerEdge::GATE_OFF,
        0U
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 0U);
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::RELEASE);
    assert(near(runtime.frame->sourceValues[0], 0.625f));

    runtime.triggers.count = 0U;
    time.monotonicMs = 125U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.3125f));
    time.monotonicMs = 175U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.0f));
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::IDLE);
}

void testAdsrLegatoAndZeroDurationTransitions() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    mod::ModulatorAdsrParameters parameters{};
    parameters.attack = 100U;
    parameters.decay = 0U;
    parameters.sustainQ15 = 16384U;
    parameters.release = 0U;
    parameters.traits = mod::makeModulatorAdsrTraits(
        mod::ModulatorTimingMode::FREE,
        mod::ModulatorAdsrRetriggerMode::LEGATO,
        mod::ModulatorAdsrCurve::LINEAR
    );
    const auto source = addAdsr(*domain, parameters, 1U, 64U, 64U);
    addBinding(*domain, source, destination(0U, 0U, 0U));

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};

    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        1U,
        4U,
        64U,
        mod::ProjectModulationTriggerEdge::GATE_ON
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    time.monotonicMs = 50U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.5f));
    time.monotonicMs = 75U;
    runtime.triggers.count = 0U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.75f));

    runtime.triggers.count = 2U;
    runtime.triggers.events[0] = noteEdge(
        1U,
        4U,
        64U,
        mod::ProjectModulationTriggerEdge::GATE_OFF,
        0U
    );
    runtime.triggers.events[1] = runtime.triggers.events[0];
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.0f));

    auto zeroDomain = std::make_unique<mod::ProjectControlDomainState>();
    parameters.attack = 0U;
    parameters.decay = 0U;
    parameters.release = 0U;
    parameters.sustainQ15 = 8192U;
    const auto zeroSource = addAdsr(*zeroDomain, parameters, 0U, 60U, 60U);
    addBinding(*zeroDomain, zeroSource, destination(0U, 0U, 0U));
    auto zeroPlan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *zeroDomain,
        activeContext(),
        *zeroPlan
    ).compiled());
    RuntimeFixture zeroRuntime;
    zeroRuntime.activate(*zeroPlan);
    zeroRuntime.triggers.count = 1U;
    zeroRuntime.triggers.events[0] = noteEdge(
        0U,
        0U,
        60U,
        mod::ProjectModulationTriggerEdge::GATE_ON
    );
    time = {};
    assert(zeroRuntime.evaluate(*zeroPlan, zeroDomain->curves, time).evaluated());
    assert(near(zeroRuntime.frame->sourceValues[0], 0.25f));
    zeroRuntime.triggers.events[0].edge =
        mod::ProjectModulationTriggerEdge::GATE_OFF;
    assert(zeroRuntime.evaluate(*zeroPlan, zeroDomain->curves, time).evaluated());
    assert(near(zeroRuntime.frame->sourceValues[0], 0.0f));
}

void testExactMaximumTriggerBucketFansOutWithoutTruncation() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    mod::ModulatorAdsrParameters parameters{};
    parameters.attack = 0U;
    parameters.decay = 0U;
    parameters.sustainQ15 =
        mod::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15;
    parameters.release = 0U;
    parameters.traits = mod::withModulatorAdsrCurve(
        parameters.traits,
        mod::ModulatorAdsrCurve::LINEAR
    );
    for (uint16_t index = 0U;
         index < mod::PROJECT_MODULATOR_CAPACITY;
         ++index) {
        (void)addAdsr(*domain, parameters, 4U);
    }

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    assert(plan->sourceCount == mod::PROJECT_MODULATOR_CAPACITY);
    assert(plan->triggerRouteCount == mod::PROJECT_MODULATOR_CAPACITY);
    const mod::ModulationTriggerRef incoming{
        mod::ModulationTriggerKind::TRACK_NOTE,
        4U,
        7U,
        60U,
    };
    const uint16_t bucket = mod::projectModulationTriggerBucketIndex(incoming);
    const uint16_t start = plan->triggerBucketOffset[bucket];
    const uint16_t end = plan->triggerBucketOffset[bucket + 1U];
    assert(start == 0U);
    assert(end - start == mod::PROJECT_MODULATOR_CAPACITY);
    for (uint16_t route = start; route < end; ++route) {
        assert(plan->triggerSourceOrder[route] == route);
    }

    RuntimeFixture runtime;
    runtime.activate(*plan);
    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = {
        .trigger = incoming,
        .edge = mod::ProjectModulationTriggerEdge::GATE_ON,
        .velocity = 100U,
    };
    mod::ProjectControlTimeSnapshot time{};
    time.monotonicMs = 1U;
    const auto evaluated = runtime.evaluate(*plan, domain->curves, time);
    assert(evaluated.evaluated());
    assert(evaluated.sourceEvaluationCount == mod::PROJECT_MODULATOR_CAPACITY);
    for (uint16_t index = 0U; index < plan->sourceCount; ++index) {
        assert(near(runtime.frame->sourceValues[index], 1.0f));
    }
    std::cout << "[PASS] exact 128-source trigger bucket fans out once\n";
}

void testSyncAdsrUsesFractionalMusicalTime() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    mod::ModulatorAdsrParameters parameters{};
    parameters.attack = 96U;
    parameters.decay = 0U;
    parameters.sustainQ15 = 16384U;
    parameters.release = 96U;
    parameters.traits = mod::makeModulatorAdsrTraits(
        mod::ModulatorTimingMode::SYNC,
        mod::ModulatorAdsrRetriggerMode::RETRIGGER,
        mod::ModulatorAdsrCurve::LINEAR
    );
    const auto source = addAdsr(*domain, parameters, 0U, 60U, 60U);
    addBinding(*domain, source, destination(0U, 0U, 0U));
    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());

    mod::ProjectControlTimeSnapshot time{};
    time.musicalTick = 10U;
    time.musicalTickFractionQ16 = 32768U;
    RuntimeFixture runtime;
    runtime.activate(*plan, time);
    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        0U,
        0U,
        60U,
        mod::ProjectModulationTriggerEdge::GATE_ON
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());

    runtime.triggers.count = 0U;
    time.musicalTick = 58U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.5f));

    runtime.triggers.count = 1U;
    runtime.triggers.events[0].edge =
        mod::ProjectModulationTriggerEdge::GATE_OFF;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    runtime.triggers.count = 0U;
    time.musicalTick = 106U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.25f));
    time.musicalTick = 154U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.0f));
}

void testDahdsrFreeStagesAndZeroDurationTransitions() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    mod::ModulatorAdsrParameters parameters{};
    parameters.delay = 10U;
    parameters.attack = 20U;
    parameters.hold = 30U;
    parameters.decay = 40U;
    parameters.sustainQ15 = 16384U;
    parameters.release = 50U;
    parameters.smooth = 0U;
    parameters.traits = mod::makeModulatorAdsrTraits(
        mod::ModulatorTimingMode::FREE,
        mod::ModulatorAdsrRetriggerMode::RETRIGGER,
        mod::ModulatorAdsrCurve::LINEAR
    );
    const auto source = addAdsr(*domain, parameters, 0U, 60U, 60U);
    addBinding(*domain, source, destination(0U, 0U, 0U));

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};
    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        0U,
        9U,
        60U,
        mod::ProjectModulationTriggerEdge::GATE_ON
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::DELAY);
    assert(near(runtime.frame->sourceValues[0], 0.0f));

    runtime.triggers.count = 0U;
    time.monotonicMs = 10U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::ATTACK);
    assert(near(runtime.frame->sourceValues[0], 0.0f));
    time.monotonicMs = 20U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.5f));
    time.monotonicMs = 30U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::HOLD);
    assert(near(runtime.frame->sourceValues[0], 1.0f));
    time.monotonicMs = 60U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::DECAY);
    assert(near(runtime.frame->sourceValues[0], 1.0f));
    time.monotonicMs = 80U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.75f));
    time.monotonicMs = 100U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::SUSTAIN);
    assert(near(runtime.frame->sourceValues[0], 0.5f));

    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        0U,
        1U,
        60U,
        mod::ProjectModulationTriggerEdge::GATE_OFF,
        0U
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::RELEASE);
    runtime.triggers.count = 0U;
    time.monotonicMs = 125U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.25f));
    time.monotonicMs = 150U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::IDLE);
    assert(near(runtime.frame->sourceValues[0], 0.0f));

    parameters.delay = 0U;
    parameters.attack = 0U;
    parameters.hold = 0U;
    parameters.decay = 0U;
    parameters.release = 0U;
    parameters.sustainQ15 = 8192U;
    auto zeroDomain = std::make_unique<mod::ProjectControlDomainState>();
    const auto zeroSource = addAdsr(
        *zeroDomain,
        parameters,
        0U,
        60U,
        60U
    );
    addBinding(*zeroDomain, zeroSource, destination(0U, 0U, 0U));
    auto zeroPlan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *zeroDomain,
        activeContext(),
        *zeroPlan
    ).compiled());
    RuntimeFixture zeroRuntime;
    zeroRuntime.activate(*zeroPlan);
    zeroRuntime.triggers.count = 1U;
    zeroRuntime.triggers.events[0] = noteEdge(
        0U,
        0U,
        60U,
        mod::ProjectModulationTriggerEdge::GATE_ON
    );
    time = {};
    assert(zeroRuntime.evaluate(
        *zeroPlan,
        zeroDomain->curves,
        time
    ).evaluated());
    assert(zeroRuntime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::SUSTAIN);
    assert(near(zeroRuntime.frame->sourceValues[0], 0.25f));
    std::cout << "[PASS] DAHDSR free stages and zero-duration chain are exact\n";
}

void testSyncDahdsrUsesIndependentSegmentFeel() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    mod::ModulatorAdsrParameters parameters{};
    parameters.delay = 12U;
    parameters.attack = 24U;
    parameters.hold = 12U;
    parameters.decay = 0U;
    parameters.sustainQ15 = 16384U;
    parameters.release = 0U;
    parameters.smooth = 0U;
    parameters.traits = mod::makeModulatorAdsrTraits(
        mod::ModulatorTimingMode::SYNC,
        mod::ModulatorAdsrRetriggerMode::RETRIGGER,
        mod::ModulatorAdsrCurve::LINEAR
    );
    parameters.traits = mod::withModulatorAdsrFeel(
        parameters.traits,
        mod::ModulatorEnvelopeTimeParameter::DELAY,
        mod::ModulatorEnvelopeFeel::TRIPLET
    );
    parameters.traits = mod::withModulatorAdsrFeel(
        parameters.traits,
        mod::ModulatorEnvelopeTimeParameter::ATTACK,
        mod::ModulatorEnvelopeFeel::DOTTED
    );
    const auto source = addAdsr(*domain, parameters, 0U, 60U, 60U);
    addBinding(*domain, source, destination(0U, 0U, 0U));
    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};
    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        0U,
        0U,
        60U,
        mod::ProjectModulationTriggerEdge::GATE_ON
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    runtime.triggers.count = 0U;

    time.musicalTick = 7U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::DELAY);
    time.musicalTick = 8U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::ATTACK);
    time.musicalTick = 26U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.5f));
    time.musicalTick = 44U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::HOLD);
    time.musicalTick = 56U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::SUSTAIN);
    assert(near(runtime.frame->sourceValues[0], 0.5f));
    std::cout << "[PASS] synchronized DAHDSR resolves Feel per segment\n";
}

void testAdsrTrackNoteVelocityFilterAndAcceptedNoteRelease() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    mod::ModulatorAdsrParameters parameters{};
    parameters.delay = 0U;
    parameters.attack = 0U;
    parameters.hold = 0U;
    parameters.decay = 0U;
    parameters.sustainQ15 =
        mod::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15;
    parameters.release = 100U;
    parameters.smooth = 0U;
    parameters.traits = mod::makeModulatorAdsrTraits(
        mod::ModulatorTimingMode::FREE,
        mod::ModulatorAdsrRetriggerMode::RETRIGGER,
        mod::ModulatorAdsrCurve::LINEAR
    );
    const auto source = addAdsr(
        *domain,
        parameters,
        3U,
        60U,
        62U,
        40U,
        80U
    );
    addBinding(*domain, source, destination(0U, 0U, 0U));
    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};

    runtime.triggers.count = 5U;
    runtime.triggers.events[0] = noteEdge(
        2U, 0U, 60U, mod::ProjectModulationTriggerEdge::GATE_ON, 40U
    );
    runtime.triggers.events[1] = noteEdge(
        3U, 1U, 59U, mod::ProjectModulationTriggerEdge::GATE_ON, 40U
    );
    runtime.triggers.events[2] = noteEdge(
        3U, 2U, 61U, mod::ProjectModulationTriggerEdge::GATE_ON, 81U
    );
    runtime.triggers.events[3] = noteEdge(
        3U, 15U, 60U, mod::ProjectModulationTriggerEdge::GATE_ON, 40U
    );
    runtime.triggers.events[4] = noteEdge(
        3U, 0U, 62U, mod::ProjectModulationTriggerEdge::GATE_ON, 80U
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 2U);
    assert(near(runtime.frame->sourceValues[0], 1.0f));

    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        3U, 4U, 60U, mod::ProjectModulationTriggerEdge::GATE_ON, 40U
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 2U);

    runtime.triggers.events[0] = noteEdge(
        3U, 7U, 61U, mod::ProjectModulationTriggerEdge::GATE_OFF, 0U
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 2U);
    runtime.triggers.events[0] = noteEdge(
        3U, 7U, 60U, mod::ProjectModulationTriggerEdge::GATE_OFF, 0U
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 1U);
    runtime.triggers.events[0] = noteEdge(
        3U, 9U, 62U, mod::ProjectModulationTriggerEdge::GATE_OFF, 0U
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 0U);
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::RELEASE);
    std::cout
        << "[PASS] Track trigger filters note/velocity and releases accepted notes across channels\n";
}

void testDroppedTriggerFrameFailsSafeToRelease() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    mod::ModulatorAdsrParameters parameters{};
    parameters.delay = 0U;
    parameters.attack = 0U;
    parameters.hold = 0U;
    parameters.decay = 0U;
    parameters.sustainQ15 =
        mod::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15;
    parameters.release = 100U;
    parameters.smooth = 0U;
    parameters.traits = mod::makeModulatorAdsrTraits(
        mod::ModulatorTimingMode::FREE,
        mod::ModulatorAdsrRetriggerMode::RETRIGGER,
        mod::ModulatorAdsrCurve::LINEAR
    );
    const auto source = addAdsr(*domain, parameters, 0U, 60U, 60U);
    addBinding(*domain, source, destination(0U, 0U, 0U));
    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};
    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        0U, 0U, 60U, mod::ProjectModulationTriggerEdge::GATE_ON
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 1U);
    assert(near(runtime.frame->sourceValues[0], 1.0f));

    runtime.triggers.count = 0U;
    runtime.triggers.droppedEventCount = 1U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 0U);
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::RELEASE);
    runtime.triggers.droppedEventCount = 0U;
    time.monotonicMs = 50U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.5f));
    std::cout << "[PASS] dropped trigger frames fail safe to envelope release\n";
}

void testAdsrSmoothZeroIsExactAndNonzeroExposesRawProjection() {
    auto makeDomain = [](uint16_t smooth) {
        auto domain = std::make_unique<mod::ProjectControlDomainState>();
        mod::ModulatorAdsrParameters parameters{};
        parameters.delay = 0U;
        parameters.attack = 0U;
        parameters.hold = 0U;
        parameters.decay = 0U;
        parameters.sustainQ15 =
            mod::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15;
        parameters.release = 0U;
        parameters.smooth = smooth;
        parameters.traits = mod::makeModulatorAdsrTraits(
            mod::ModulatorTimingMode::FREE,
            mod::ModulatorAdsrRetriggerMode::RETRIGGER,
            mod::ModulatorAdsrCurve::LINEAR
        );
        const auto source = addAdsr(
            *domain,
            parameters,
            0U,
            60U,
            60U
        );
        addBinding(*domain, source, destination(0U, 0U, 0U));
        return std::pair{std::move(domain), source};
    };

    auto [exactDomain, exactSource] = makeDomain(0U);
    auto exactPlan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *exactDomain,
        activeContext(),
        *exactPlan
    ).compiled());
    RuntimeFixture exactRuntime;
    exactRuntime.activate(*exactPlan);
    exactRuntime.triggers.count = 1U;
    exactRuntime.triggers.events[0] = noteEdge(
        0U, 0U, 60U, mod::ProjectModulationTriggerEdge::GATE_ON
    );
    mod::ProjectControlTimeSnapshot time{};
    assert(exactRuntime.evaluate(
        *exactPlan,
        exactDomain->curves,
        time
    ).evaluated());
    assert(near(exactRuntime.frame->sourceValues[0], 1.0f));
    mod::ProjectModulatorRuntimeProjection projection{};
    assert(mod::projectModulatorRuntimeProjection(
        *exactPlan,
        exactDomain->curves,
        *exactRuntime.state,
        time,
        exactSource,
        projection
    ));
    assert(near(projection.rawValue, 1.0f));
    assert(near(projection.value, projection.rawValue));

    auto [smoothDomain, smoothSource] = makeDomain(100U);
    auto smoothPlan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *smoothDomain,
        activeContext(),
        *smoothPlan
    ).compiled());
    RuntimeFixture smoothRuntime;
    smoothRuntime.activate(*smoothPlan);
    smoothRuntime.triggers.count = 1U;
    smoothRuntime.triggers.events[0] = noteEdge(
        0U, 0U, 60U, mod::ProjectModulationTriggerEdge::GATE_ON
    );
    assert(smoothRuntime.evaluate(
        *smoothPlan,
        smoothDomain->curves,
        time
    ).evaluated());
    assert(near(smoothRuntime.frame->sourceValues[0], 0.0f));
    assert(mod::projectModulatorRuntimeProjection(
        *smoothPlan,
        smoothDomain->curves,
        *smoothRuntime.state,
        time,
        smoothSource,
        projection
    ));
    assert(near(projection.rawValue, 1.0f));
    assert(near(projection.value, 0.0f));

    smoothRuntime.triggers.count = 0U;
    time.monotonicMs = 50U;
    assert(smoothRuntime.evaluate(
        *smoothPlan,
        smoothDomain->curves,
        time
    ).evaluated());
    assert(near(smoothRuntime.frame->sourceValues[0], 1.0f / 3.0f));
    assert(mod::projectModulatorRuntimeProjection(
        *smoothPlan,
        smoothDomain->curves,
        *smoothRuntime.state,
        time,
        smoothSource,
        projection
    ));
    assert(near(projection.rawValue, 1.0f));
    assert(near(projection.value, 1.0f / 3.0f));

    smoothRuntime.triggers.count = 1U;
    smoothRuntime.triggers.events[0] = noteEdge(
        0U,
        11U,
        60U,
        mod::ProjectModulationTriggerEdge::GATE_OFF,
        0U
    );
    assert(smoothRuntime.evaluate(
        *smoothPlan,
        smoothDomain->curves,
        time
    ).evaluated());
    assert(smoothRuntime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::IDLE);
    assert(near(smoothRuntime.frame->sourceValues[0], 1.0f / 3.0f));
    assert(mod::projectModulatorRuntimeProjection(
        *smoothPlan,
        smoothDomain->curves,
        *smoothRuntime.state,
        time,
        smoothSource,
        projection
    ));
    assert(near(projection.rawValue, 0.0f));
    assert(near(projection.value, 1.0f / 3.0f));

    smoothRuntime.triggers.count = 0U;
    time.monotonicMs = 100U;
    assert(smoothRuntime.evaluate(
        *smoothPlan,
        smoothDomain->curves,
        time
    ).evaluated());
    assert(smoothRuntime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::IDLE);
    assert(near(smoothRuntime.frame->sourceValues[0], 2.0f / 9.0f));
    std::cout
        << "[PASS] envelope Smooth has an exact zero bypass, raw projection, and Idle tail\n";
}

void testSyncEnvelopeSmoothUsesItsOwnFeel() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    mod::ModulatorAdsrParameters parameters{};
    parameters.delay = 0U;
    parameters.attack = 0U;
    parameters.hold = 0U;
    parameters.decay = 0U;
    parameters.sustainQ15 =
        mod::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15;
    parameters.release = 0U;
    parameters.smooth = 12U;
    parameters.traits = mod::makeModulatorAdsrTraits(
        mod::ModulatorTimingMode::SYNC,
        mod::ModulatorAdsrRetriggerMode::RETRIGGER,
        mod::ModulatorAdsrCurve::LINEAR
    );
    parameters.traits = mod::withModulatorAdsrFeel(
        parameters.traits,
        mod::ModulatorEnvelopeTimeParameter::SMOOTH,
        mod::ModulatorEnvelopeFeel::DOTTED
    );
    const auto source = addAdsr(*domain, parameters, 0U, 60U, 60U);
    addBinding(*domain, source, destination(0U, 0U, 0U));
    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};
    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        0U, 0U, 60U, mod::ProjectModulationTriggerEdge::GATE_ON
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.0f));

    runtime.triggers.count = 0U;
    time.musicalTick = 9U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 1.0f / 3.0f));
    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        0U,
        13U,
        60U,
        mod::ProjectModulationTriggerEdge::GATE_OFF,
        0U
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::IDLE);
    assert(near(runtime.frame->sourceValues[0], 1.0f / 3.0f));
    runtime.triggers.count = 0U;
    time.musicalTick = 18U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 2.0f / 9.0f));
    std::cout << "[PASS] synchronized Smooth resolves its independent Feel\n";
}

void testGateOffReleasesFromCurrentDelayHoldAndDecayLevels() {
    struct ReleaseCase {
        mod::ProjectModulationAdsrStage stage;
        uint16_t delay;
        uint16_t attack;
        uint16_t hold;
        uint16_t decay;
        uint32_t retriggerAtMs;
        uint32_t gateOffAtMs;
        float currentLevel;
    };
    constexpr uint32_t NO_RETRIGGER = std::numeric_limits<uint32_t>::max();
    const std::array<ReleaseCase, 3U> cases{{
        {
            mod::ProjectModulationAdsrStage::DELAY,
            100U,
            100U,
            0U,
            0U,
            150U,
            175U,
            0.5f,
        },
        {
            mod::ProjectModulationAdsrStage::HOLD,
            0U,
            0U,
            100U,
            0U,
            NO_RETRIGGER,
            25U,
            1.0f,
        },
        {
            mod::ProjectModulationAdsrStage::DECAY,
            0U,
            0U,
            0U,
            100U,
            NO_RETRIGGER,
            50U,
            0.75f,
        },
    }};

    for (const auto& releaseCase : cases) {
        auto domain = std::make_unique<mod::ProjectControlDomainState>();
        mod::ModulatorAdsrParameters parameters{};
        parameters.delay = releaseCase.delay;
        parameters.attack = releaseCase.attack;
        parameters.hold = releaseCase.hold;
        parameters.decay = releaseCase.decay;
        parameters.sustainQ15 = 16384U;
        parameters.release = 100U;
        parameters.smooth = 0U;
        parameters.traits = mod::makeModulatorAdsrTraits(
            mod::ModulatorTimingMode::FREE,
            mod::ModulatorAdsrRetriggerMode::RETRIGGER,
            mod::ModulatorAdsrCurve::LINEAR
        );
        const auto source = addAdsr(*domain, parameters, 0U, 60U, 60U);
        addBinding(*domain, source, destination(0U, 0U, 0U));
        auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
        assert(mod::compileProjectControlRuntimePlan(
            *domain,
            activeContext(),
            *plan
        ).compiled());
        RuntimeFixture runtime;
        runtime.activate(*plan);
        mod::ProjectControlTimeSnapshot time{};
        runtime.triggers.count = 1U;
        runtime.triggers.events[0] = noteEdge(
            0U, 0U, 60U, mod::ProjectModulationTriggerEdge::GATE_ON
        );
        assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
        runtime.triggers.count = 0U;

        if (releaseCase.retriggerAtMs != NO_RETRIGGER) {
            time.monotonicMs = releaseCase.retriggerAtMs;
            assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
            assert(near(runtime.frame->sourceValues[0], 0.5f));
            runtime.triggers.count = 1U;
            runtime.triggers.events[0] = noteEdge(
                0U, 5U, 60U, mod::ProjectModulationTriggerEdge::GATE_ON
            );
            assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
            runtime.triggers.count = 0U;
        }

        time.monotonicMs = releaseCase.gateOffAtMs;
        assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
        assert(runtime.state->sources[0].payload.adsr.stage ==
               releaseCase.stage);
        assert(near(
            runtime.frame->sourceValues[0],
            releaseCase.currentLevel
        ));
        runtime.triggers.count = 1U;
        runtime.triggers.events[0] = noteEdge(
            0U,
            12U,
            60U,
            mod::ProjectModulationTriggerEdge::GATE_OFF,
            0U
        );
        assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
        assert(runtime.state->sources[0].payload.adsr.stage ==
               mod::ProjectModulationAdsrStage::RELEASE);
        assert(near(
            runtime.frame->sourceValues[0],
            releaseCase.currentLevel
        ));
        runtime.triggers.count = 0U;
        time.monotonicMs += 50U;
        assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
        assert(near(
            runtime.frame->sourceValues[0],
            releaseCase.currentLevel * 0.5f
        ));
    }
    std::cout << "[PASS] Gate Off releases from current Delay/Hold/Decay levels\n";
}

void testAdsrRouteRecompilePreservesHarmlessEditsAndResetsRangeEdits() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    mod::ModulatorAdsrParameters parameters{};
    parameters.delay = 0U;
    parameters.attack = 0U;
    parameters.hold = 0U;
    parameters.decay = 0U;
    parameters.sustainQ15 = 16384U;
    parameters.release = 100U;
    parameters.smooth = 0U;
    parameters.traits = mod::makeModulatorAdsrTraits(
        mod::ModulatorTimingMode::FREE,
        mod::ModulatorAdsrRetriggerMode::RETRIGGER,
        mod::ModulatorAdsrCurve::LINEAR
    );
    const auto source = addAdsr(
        *domain,
        parameters,
        0U,
        60U,
        62U,
        40U,
        80U
    );
    addBinding(*domain, source, destination(0U, 0U, 0U));
    auto firstPlan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *firstPlan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*firstPlan);
    mod::ProjectControlTimeSnapshot time{};
    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        0U, 0U, 60U, mod::ProjectModulationTriggerEdge::GATE_ON, 50U
    );
    assert(runtime.evaluate(*firstPlan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 1U);

    auto* authored = mod::findProjectModulator(domain->modulation, source);
    assert(authored != nullptr);
    authored->parameters.adsr.sustainQ15 = 24576U;
    auto harmlessPlan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *harmlessPlan
    ).compiled());
    assert(mod::synchronizeProjectControlRuntimeState(
        *runtime.state,
        *harmlessPlan,
        time
    ) == mod::ProjectControlRuntimeStatus::OK);
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 1U);
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::SUSTAIN);

    const mod::ModulationTriggerFilter narrowedNotes{
        mod::ModulationTriggerKind::TRACK_NOTE,
        0U,
        61U,
        62U,
    };
    assert(mod::setProjectModulationTrigger(
        domain->modulation,
        source,
        narrowedNotes,
        true,
        40U,
        80U
    ).changed());
    auto notePlan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *notePlan
    ).compiled());
    assert(mod::synchronizeProjectControlRuntimeState(
        *runtime.state,
        *notePlan,
        time
    ) == mod::ProjectControlRuntimeStatus::OK);
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 0U);
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::IDLE);

    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        0U, 7U, 61U, mod::ProjectModulationTriggerEdge::GATE_ON, 50U
    );
    assert(runtime.evaluate(*notePlan, domain->curves, time).evaluated());
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 1U);
    assert(mod::setProjectModulationTrigger(
        domain->modulation,
        source,
        narrowedNotes,
        true,
        51U,
        80U
    ).changed());
    auto velocityPlan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *velocityPlan
    ).compiled());
    assert(mod::synchronizeProjectControlRuntimeState(
        *runtime.state,
        *velocityPlan,
        time
    ) == mod::ProjectControlRuntimeStatus::OK);
    assert(runtime.state->sources[0].payload.adsr.heldNoteCount == 0U);
    assert(runtime.state->sources[0].payload.adsr.stage ==
           mod::ProjectModulationAdsrStage::IDLE);
    std::cout
        << "[PASS] ADSR recompilation preserves harmless edits and resets range edits\n";
}

void testSyncEnvelopeFollowsMusicalTicksAcrossTempoChanges() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    mod::ModulatorAdsrParameters parameters{};
    parameters.delay = 0U;
    parameters.attack = 96U;
    parameters.hold = 0U;
    parameters.decay = 0U;
    parameters.sustainQ15 = 16384U;
    parameters.release = 0U;
    parameters.smooth = 0U;
    parameters.traits = mod::makeModulatorAdsrTraits(
        mod::ModulatorTimingMode::SYNC,
        mod::ModulatorAdsrRetriggerMode::RETRIGGER,
        mod::ModulatorAdsrCurve::LINEAR
    );
    const auto source = addAdsr(*domain, parameters, 0U, 60U, 60U);
    addBinding(*domain, source, destination(0U, 0U, 0U));
    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());

    mod::ProjectControlTimeSnapshot time{};
    time.musicalTick = 100U;
    time.musicalTickFractionQ16 = 32768U;
    time.monotonicMs = 1000U;
    RuntimeFixture runtime;
    runtime.activate(*plan, time);
    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = noteEdge(
        0U, 0U, 60U, mod::ProjectModulationTriggerEdge::GATE_ON
    );
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    runtime.triggers.count = 0U;

    time.monotonicMs = 5000U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.0f));
    time.musicalTick = 124U;
    time.monotonicMs = 5010U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.25f));
    time.musicalTick = 148U;
    time.monotonicMs = 5011U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.5f));
    time.monotonicMs = 20000U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.5f));
    std::cout
        << "[PASS] synchronized envelope remains continuous across tempo-rate changes\n";
}

void testLogicalBaseAutomationManualAndSharedSource() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    const auto first = destination(0U, 0U, 0U);
    const auto second = destination(3U, 0U, 1U);
    addAbsoluteAutomation(*domain, first);
    const auto source = addLfo(
        *domain,
        mod::ModulatorLfoShape::SINE,
        192U
    );
    addBinding(*domain, source, first, 8192);
    addBinding(*domain, source, second, 16384);

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    const auto compiled = mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    );
    assert(compiled.compiled());
    assert(plan->sourceCount == 1U);
    assert(plan->destinationCount == 2U);
    assert(plan->automationCount == 1U);

    RuntimeFixture runtime;
    runtime.bases[0].staticValue = 0.1f;
    runtime.bases[1].staticValue = 0.4f;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};
    time.musicalTick = 48U;
    const auto result = runtime.evaluate(*plan, domain->curves, time);
    assert(result.evaluated());
    assert(result.sourceEvaluationCount == 1U);
    assert(result.contributionCount == 2U);
    assert(near(runtime.frame->sourceValues[0], 1.0f));
    assert(near(runtime.frame->destinations[0].base, 0.25f));
    assert(near(runtime.frame->destinations[0].underlyingBase, 0.25f));
    assert(near(runtime.frame->destinations[0].modulation, 0.25f));
    assert(near(runtime.frame->destinations[0].value, 0.50f));
    assert((runtime.frame->destinations[0].flags &
            mod::PROJECT_LOGICAL_MACRO_FLAG_AUTOMATION_ACTIVE) != 0U);
    assert(near(runtime.frame->destinations[1].value, 0.90f));

    runtime.bases[0].manualOverride = true;
    runtime.bases[0].manualValue = 0.2f;
    time.monotonicMs = 20U;
    const auto manualResult = runtime.evaluate(*plan, domain->curves, time);
    assert(manualResult.evaluated());
    assert(near(runtime.frame->destinations[0].underlyingBase, 0.25f));
    assert(near(runtime.frame->destinations[0].base, 0.2f));
    assert(near(runtime.frame->destinations[0].value, 0.45f));
    assert((runtime.frame->destinations[0].flags &
            mod::PROJECT_LOGICAL_MACRO_FLAG_MANUAL_OVERRIDE) != 0U);
    assert((runtime.frame->destinations[0].flags &
            mod::PROJECT_LOGICAL_MACRO_FLAG_AUTOMATION_ACTIVE) != 0U);

    // Automation keeps following the shared musical phase while Manual owns
    // the audible value; releasing Manual therefore resumes in place.
    time.musicalTick = 96U;
    time.monotonicMs = 40U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->destinations[0].underlyingBase, 0.5f));
    assert(near(runtime.frame->destinations[0].base, 0.2f));
    runtime.bases[0].manualOverride = false;
    time.monotonicMs = 41U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->destinations[0].base, 0.5f));
    assert(near(runtime.frame->destinations[0].underlyingBase, 0.5f));
}

void testDestinationScaleAppliesOnceAfterSummingContributions() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    const auto target = destination(0U, 0U, 0U);
    const auto first = addLfo(*domain, mod::ModulatorLfoShape::SQUARE, 192U);
    const auto second = addLfo(*domain, mod::ModulatorLfoShape::SQUARE, 192U);
    addBinding(*domain, first, target, 8192);
    addBinding(*domain, second, target, 8192);
    assert(mod::setProjectModulationDestinationScale(
        domain->modulation,
        target,
        16384U
    ).changed());

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    assert(plan->destinationCount == 1U);
    assert(plan->destinations[0].destinationScaleQ15 == 16384U);

    RuntimeFixture runtime;
    runtime.bases[0].staticValue = 0.5f;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};
    time.musicalTick = 48U;
    auto result = runtime.evaluate(*plan, domain->curves, time);
    assert(result.evaluated() && result.contributionCount == 2U);
    assert(near(runtime.frame->destinations[0].modulation, 0.25f));
    assert(near(runtime.frame->destinations[0].value, 0.75f));

    assert(mod::setProjectModulationDestinationScale(
        domain->modulation,
        target,
        0U
    ).changed());
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    runtime.activate(*plan);
    result = runtime.evaluate(*plan, domain->curves, time);
    assert(result.evaluated() && result.contributionCount == 2U);
    assert(near(runtime.frame->destinations[0].modulation, 0.0f));
    assert(near(runtime.frame->destinations[0].value, 0.5f));
    std::cout << "[PASS] destination scale applies once after the source sum\n";
}

void testSyncFreeTransportAndExplicitRetrigger() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    const auto syncSource = addLfo(
        *domain,
        mod::ModulatorLfoShape::SAW_UP,
        100U
    );
    const auto freeSource = addLfo(
        *domain,
        mod::ModulatorLfoShape::SAW_UP,
        100U,
        mod::ModulatorTimingMode::FREE,
        1000U
    );
    const auto transportSource = addLfo(
        *domain,
        mod::ModulatorLfoShape::SAW_UP,
        100U,
        mod::ModulatorTimingMode::SYNC,
        1000U,
        mod::ModulatorRetriggerPolicy::TRANSPORT
    );
    const auto explicitSource = addLfo(
        *domain,
        mod::ModulatorLfoShape::SAW_UP,
        100U,
        mod::ModulatorTimingMode::SYNC,
        1000U,
        mod::ModulatorRetriggerPolicy::EXPLICIT_TRIGGER
    );
    addBinding(*domain, syncSource, destination(0U, 0U, 0U));
    addBinding(*domain, freeSource, destination(0U, 0U, 1U));
    addBinding(*domain, transportSource, destination(0U, 0U, 2U));
    addBinding(*domain, explicitSource, destination(0U, 0U, 3U));
    mod::ModulationTriggerDraft trigger{};
    trigger.sourceId = explicitSource;
    trigger.trigger = {
        mod::ModulationTriggerKind::MANUAL,
        0U,
        0U,
        7U,
    };
    assert(mod::addProjectModulationTrigger(
        domain->modulation,
        trigger
    ).changed());

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);

    mod::ProjectControlTimeSnapshot time{};
    time.musicalTick = 50U;
    time.monotonicMs = 250U;
    time.transportGeneration = 1U;
    time.transportStartMusicalTick = 25U;
    time.transportStartMonotonicMs = 100U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.0f));
    assert(near(runtime.frame->sourceValues[1], -0.5f));
    assert(near(runtime.frame->sourceValues[2], -0.5f));
    assert(near(runtime.frame->sourceValues[3], 0.0f));

    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = {
        .trigger = {
            mod::ModulationTriggerKind::MANUAL,
            0U,
            0U,
            7U,
        },
        .edge = mod::ProjectModulationTriggerEdge::PULSE,
    };
    time.monotonicMs = 260U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[3], -1.0f));
    runtime.triggers.count = 1U;
    runtime.triggers.events[0].edge =
        mod::ProjectModulationTriggerEdge::GATE_OFF;
    time.musicalTick = 75U;
    time.monotonicMs = 280U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[3], -0.5f));
}

void testFractionalRecordedCurveAndFromBaseBinding() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    const std::array<mod::ProjectPackedCurvePoint, 2> points{{
        {0U, -32767},
        {1U, 32767},
    }};
    mod::RecordedShapeDraft shape{};
    shape.name = "Motion";
    shape.curve.sourceDurationTicks = 2U;
    shape.curve.durationTicks = 2U;
    shape.curve.valueDomain = mod::ProjectCurveValueDomain::BIPOLAR;
    shape.points = points.data();
    shape.pointCount = static_cast<uint16_t>(points.size());
    const auto source = mod::createRecordedShapeModulator(
        domain->modulation,
        domain->curves,
        shape
    );
    assert(source.changed());
    addBinding(
        *domain,
        source.sourceId,
        destination(0U, 0U, 0U),
        32767,
        mod::ModulationApplication::FROM_BASE
    );

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};
    time.musicalTickFractionQ16 = 32768U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.0f));
    assert(near(runtime.frame->destinations[0].modulation, 0.5f));
}

void testPositiveRecordedCurveUsesNaturalAndExplicitAroundBase() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    const std::array<mod::ProjectPackedCurvePoint, 2> points{{
        {0U, 0},
        {1U, 32767},
    }};
    mod::RecordedShapeDraft shape{};
    shape.name = "Envelope";
    shape.curve.sourceDurationTicks = 2U;
    shape.curve.durationTicks = 2U;
    shape.curve.valueDomain =
        mod::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
    shape.points = points.data();
    shape.pointCount = static_cast<uint16_t>(points.size());
    const auto source = mod::createRecordedShapeModulator(
        domain->modulation,
        domain->curves,
        shape
    );
    assert(source.changed());
    addBinding(
        *domain,
        source.sourceId,
        destination(0U, 0U, 0U),
        32767,
        mod::ModulationApplication::NATURAL
    );
    addBinding(
        *domain,
        source.sourceId,
        destination(0U, 0U, 1U),
        32767,
        mod::ModulationApplication::AROUND_BASE
    );
    addBinding(
        *domain,
        source.sourceId,
        destination(0U, 0U, 2U),
        -32767,
        mod::ModulationApplication::NATURAL
    );

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};
    time.musicalTickFractionQ16 = 32768U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.5f));
    assert(near(runtime.frame->destinations[0].modulation, 0.5f));
    assert(near(runtime.frame->destinations[1].modulation, 0.0f));
    assert(near(runtime.frame->destinations[2].modulation, -0.5f));
}

void testRecordedShapeKeepsRelativeExcursionUntilFinalDestinationClamp() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    // Forty percent of the bipolar Source domain projects to twenty
    // destination percentage points at nominal 100% Depth. The authored value
    // is deliberately independent from every destination Base below.
    constexpr int16_t SOURCE_40_PERCENT_Q15 = 13107;
    const std::array<mod::ProjectPackedCurvePoint, 2> points{{
        {0U, SOURCE_40_PERCENT_Q15},
        {192U, SOURCE_40_PERCENT_Q15},
    }};
    mod::RecordedShapeDraft shape{};
    shape.name = "Relative";
    shape.curve.sourceDurationTicks = 192U;
    shape.curve.durationTicks = 192U;
    shape.curve.valueDomain = mod::ProjectCurveValueDomain::BIPOLAR;
    shape.points = points.data();
    shape.pointCount = static_cast<uint16_t>(points.size());
    const auto source = mod::createRecordedShapeModulator(
        domain->modulation,
        domain->curves,
        shape
    );
    assert(source.changed());

    const auto high = destination(0U, 0U, 0U);
    const auto middle = destination(0U, 0U, 1U);
    const auto low = destination(0U, 0U, 2U);
    const auto fullScaleDestination = destination(0U, 0U, 3U);
    // Recorded Shape Depth uses a wider authored range than the other Source
    // kinds: 100% is half Q15 and 200% is the persisted full-Q15 ceiling.
    addBinding(*domain, source.sourceId, high, 16384);
    addBinding(*domain, source.sourceId, middle, 16384);
    addBinding(*domain, source.sourceId, low, -16384);
    addBinding(*domain, source.sourceId, fullScaleDestination, 32767);

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.bases[0].staticValue = 0.99f;
    runtime.bases[1].staticValue = 0.50f;
    runtime.bases[2].staticValue = 0.01f;
    runtime.bases[3].staticValue = 0.25f;
    runtime.activate(*plan);

    const mod::ProjectControlTimeSnapshot time{};
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.40f));
    assert(near(runtime.frame->destinations[0].modulation, 0.20f));
    assert(near(runtime.frame->destinations[1].modulation, 0.20f));
    assert(near(runtime.frame->destinations[2].modulation, -0.20f));
    assert(near(runtime.frame->destinations[0].value, 1.0f));
    assert(near(runtime.frame->destinations[1].value, 0.70f));
    assert(near(runtime.frame->destinations[2].value, 0.0f));
    assert(near(runtime.frame->destinations[3].modulation, 0.40f));
    assert(near(runtime.frame->destinations[3].value, 0.65f));
    assert((runtime.frame->destinations[0].flags &
            mod::PROJECT_LOGICAL_MACRO_FLAG_CLIPPED) != 0U);
    assert((runtime.frame->destinations[1].flags &
            mod::PROJECT_LOGICAL_MACRO_FLAG_CLIPPED) == 0U);
    assert((runtime.frame->destinations[2].flags &
            mod::PROJECT_LOGICAL_MACRO_FLAG_CLIPPED) != 0U);

    // The canonical Source curve is unchanged by either clamp. Moving the
    // Base later can therefore reveal the previously out-of-range excursion.
    assert(domain->curves.points[0].value == SOURCE_40_PERCENT_Q15);
    assert(domain->curves.points[1].value == SOURCE_40_PERCENT_Q15);

    // Explicit Source overdub replaces the hot sample for every existing
    // destination without mutating or recompiling the authored curve.
    assert(mod::setProjectRecordedShapeSourceAudition(
        *runtime.state,
        source.sourceId,
        -6553
    ));
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], -0.20f));
    assert(near(runtime.frame->destinations[1].modulation, -0.10f));
    assert(near(runtime.frame->destinations[1].value, 0.40f));

    mod::clearProjectRecordedShapeRuntimeAudition(*runtime.state);
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.40f));

    // A Macro-originated take can audition its not-yet-committed edge on an
    // already-published destination. Existing contributions remain intact.
    assert(mod::setProjectRecordedShapeDestinationAudition(
        *runtime.state,
        fullScaleDestination,
        16384,
        -SOURCE_40_PERCENT_Q15
    ));
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->destinations[3].modulation, 0.20f));
    assert(near(runtime.frame->destinations[3].value, 0.45f));
    assert(!mod::setProjectRecordedShapeDestinationAudition(
        *runtime.state,
        fullScaleDestination,
        std::numeric_limits<int16_t>::min(),
        0
    ));
    auto invalidDestination = fullScaleDestination;
    invalidDestination.track = mod::PROJECT_MODULATION_TRACK_COUNT;
    assert(!mod::setProjectRecordedShapeDestinationAudition(
        *runtime.state,
        invalidDestination,
        16384,
        0
    ));
    mod::clearProjectRecordedShapeRuntimeAudition(*runtime.state);
}

void testRecordedShapeAuditionPublishesAStaticDestinationAbsentFromPlan() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    assert(plan->destinationCount == 0U);

    RuntimeFixture runtime;
    runtime.activate(*plan);
    const auto target = destination(0U, 0U, 4U);
    assert(mod::setProjectRecordedShapeDestinationAudition(
        *runtime.state,
        target,
        16384,
        32767,
        16384
    ));
    runtime.bases[0].staticValue = 0.40f;
    const mod::ProjectControlTimeSnapshot time{};
    const auto evaluated = mod::evaluateProjectControlRuntimeFrame(
        *plan,
        domain->curves,
        time,
        runtime.triggers,
        runtime.bases.data(),
        1U,
        *runtime.state,
        *runtime.frame
    );
    assert(evaluated.evaluated());
    assert(evaluated.destinationEvaluationCount == 1U);
    assert(evaluated.contributionCount == 1U);
    assert(runtime.frame->destinationCount == 1U);
    assert(runtime.frame->destinations[0].destination == target);
    assert(near(runtime.frame->destinations[0].base, 0.40f));
    assert(near(runtime.frame->destinations[0].modulation, 0.25f));
    assert(near(runtime.frame->destinations[0].value, 0.65f));
    assert((runtime.frame->destinations[0].flags &
            mod::PROJECT_LOGICAL_MACRO_FLAG_MODULATION_ACTIVE) != 0U);

    mod::clearProjectRecordedShapeRuntimeAudition(*runtime.state);
    std::cout << "[PASS] provisional Recorded Shape resolves a static destination\n";
}

void testRecordedCurveHintPreservesJumpsWrapAndDuplicateTicks() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    const std::array<mod::ProjectPackedCurvePoint, 7> points{{
        {0U, -32767},
        {2U, -24576},
        {4U, -16384},
        {4U, -8192},
        {8U, 0},
        {12U, 16384},
        {15U, 32767},
    }};
    mod::RecordedShapeDraft shape{};
    shape.name = "Hint";
    shape.curve.sourceDurationTicks = 16U;
    shape.curve.durationTicks = 16U;
    shape.curve.valueDomain = mod::ProjectCurveValueDomain::BIPOLAR;
    shape.points = points.data();
    shape.pointCount = static_cast<uint16_t>(points.size());
    const auto source = mod::createRecordedShapeModulator(
        domain->modulation,
        domain->curves,
        shape
    );
    assert(source.changed());
    addBinding(*domain, source.sourceId, destination(0U, 0U, 0U));

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};

    time.musicalTick = 10U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.25f));
    assert(runtime.state->sources[0].payload.recordedCurve.segmentHint == 5U);
    assert(runtime.state->sources[0].payload.recordedCurve.segmentValid);
    assert(runtime.state->sources[0].payload.recordedCurve.leftTick == 8U);
    assert(runtime.state->sources[0].payload.recordedCurve.rightTick == 12U);

    time.musicalTick = 4U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], -0.5f));
    assert(runtime.state->sources[0].payload.recordedCurve.segmentHint == 2U);

    time.musicalTick = 14U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], 0.8333f));
    assert(runtime.state->sources[0].payload.recordedCurve.segmentHint == 6U);

    time.musicalTick = 20U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], -0.5f));
    assert(runtime.state->sources[0].payload.recordedCurve.segmentHint == 2U);

    assert(mod::synchronizeProjectControlRuntimeState(
        *runtime.state,
        *plan,
        time
    ) == mod::ProjectControlRuntimeStatus::OK);
    assert(!runtime.state->sources[0].payload.recordedCurve.segmentValid);
    assert(runtime.state->sources[0].payload.recordedCurve.segmentHint == 1U);
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[0], -0.5f));
    std::cout << "[PASS] recorded curve hint preserves exact discontinuities\n";
}

void testExplicitPerBindingSlewAndFailureAtomicity() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    const auto source = addLfo(
        *domain,
        mod::ModulatorLfoShape::SQUARE,
        100U
    );
    addBinding(
        *domain,
        source,
        destination(0U, 0U, 0U),
        32767,
        mod::ModulationApplication::AROUND_BASE,
        100U
    );
    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);

    mod::ProjectControlTimeSnapshot time{};
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->destinations[0].modulation, 0.0f));
    time.monotonicMs = 50U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->destinations[0].modulation, 0.5f));
    time.monotonicMs = 100U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->destinations[0].modulation, 1.0f));

    const auto frameBefore = *runtime.frame;
    const auto stateBefore = *runtime.state;
    const auto rejected = mod::evaluateProjectControlRuntimeFrame(
        *plan,
        domain->curves,
        time,
        runtime.triggers,
        runtime.bases.data(),
        0U,
        *runtime.state,
        *runtime.frame
    );
    assert(rejected.status == mod::ProjectControlRuntimeStatus::INVALID_ARGUMENT);
    assert(std::memcmp(
        runtime.frame.get(),
        &frameBefore,
        sizeof(frameBefore)
    ) == 0);
    assert(std::memcmp(
        runtime.state.get(),
        &stateBefore,
        sizeof(stateBefore)
    ) == 0);
}

void testRuntimeStateSurvivesStableIdReordering() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    const auto removed = addLfo(
        *domain,
        mod::ModulatorLfoShape::SINE,
        100U
    );
    const auto retained = addLfo(
        *domain,
        mod::ModulatorLfoShape::SAW_UP,
        100U,
        mod::ModulatorTimingMode::SYNC,
        1000U,
        mod::ModulatorRetriggerPolicy::EXPLICIT_TRIGGER
    );
    addBinding(*domain, removed, destination(0U, 0U, 0U));
    addBinding(*domain, retained, destination(0U, 0U, 1U), 32767,
               mod::ModulationApplication::AROUND_BASE, 100U);
    mod::ModulationTriggerDraft trigger{};
    trigger.sourceId = retained;
    trigger.trigger = {
        mod::ModulationTriggerKind::MANUAL,
        0U,
        0U,
        0U,
    };
    assert(mod::addProjectModulationTrigger(
        domain->modulation,
        trigger
    ).changed());

    auto firstPlan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *firstPlan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*firstPlan);
    runtime.triggers.count = 1U;
    runtime.triggers.events[0] = {
        .trigger = {
            mod::ModulationTriggerKind::MANUAL,
            0U,
            0U,
            9U,
        },
        .edge = mod::ProjectModulationTriggerEdge::PULSE,
    };
    mod::ProjectControlTimeSnapshot time{};
    time.musicalTick = 40U;
    time.monotonicMs = 40U;
    assert(runtime.evaluate(*firstPlan, domain->curves, time).evaluated());
    runtime.triggers.count = 0U;
    const uint32_t retainedAnchor = runtime.state->sources[1]
        .payload.lfo.explicitMusicalAnchorTick;

    assert(mod::deleteProjectModulator(
        domain->modulation,
        domain->curves,
        removed
    ).changed());
    auto secondPlan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *secondPlan
    ).compiled());
    assert(mod::synchronizeProjectControlRuntimeState(
        *runtime.state,
        *secondPlan,
        time
    ) == mod::ProjectControlRuntimeStatus::OK);
    assert(runtime.state->sources[0].id == retained);
    assert(runtime.state->sources[0].payload.lfo.explicitMusicalAnchorTick ==
           retainedAnchor);
}

void testExactMaximumGraphEvaluatesEverySourceAndBinding() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    std::array<mod::ModulatorId, mod::PROJECT_MODULATOR_CAPACITY> sources{};
    for (uint16_t index = 0; index < sources.size(); ++index) {
        sources[index] = addLfo(
            *domain,
            static_cast<mod::ModulatorLfoShape>(
                index % (static_cast<uint8_t>(mod::ModulatorLfoShape::SQUARE) + 1U)
            ),
            static_cast<uint32_t>(96U + index)
        );
    }
    for (uint16_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
        for (uint8_t edge = 0; edge < 4U; ++edge) {
            const uint16_t stableAddress = static_cast<uint16_t>(
                (sourceIndex + edge) %
                mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY
            );
            addBinding(
                *domain,
                sources[sourceIndex],
                destination(
                    static_cast<uint8_t>(stableAddress /
                        mod::PROJECT_MODULATION_MACRO_COUNT),
                    0U,
                    static_cast<uint8_t>(stableAddress %
                        mod::PROJECT_MODULATION_MACRO_COUNT)
                ),
                static_cast<int16_t>(4096 + edge)
            );
        }
    }
    assert(domain->modulation.sourceCount == mod::PROJECT_MODULATOR_CAPACITY);
    assert(domain->modulation.outputBindingCount ==
           mod::PROJECT_MODULATION_BINDING_CAPACITY);

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    const auto compiled = mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    );
    assert(compiled.compiled());
    assert(plan->sourceCount == mod::PROJECT_MODULATOR_CAPACITY);
    assert(plan->bindingCount == mod::PROJECT_MODULATION_BINDING_CAPACITY);
    assert(plan->destinationCount ==
           mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY);

    RuntimeFixture runtime;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};
    time.musicalTick = 1234U;
    time.monotonicMs = 5678U;
    const auto result = runtime.evaluate(*plan, domain->curves, time);
    assert(result.evaluated());
    assert(result.sourceEvaluationCount == mod::PROJECT_MODULATOR_CAPACITY);
    assert(result.destinationEvaluationCount ==
           mod::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY);
    assert(result.contributionCount ==
           mod::PROJECT_MODULATION_BINDING_CAPACITY);
}

void testUiTimeTelemetryExtrapolatesBoundedly() {
    mod::ProjectControlTimeTelemetry telemetry{};
    mod::ProjectControlTimeSnapshot first{};
    first.musicalTick = 100U;
    first.monotonicMs = 1000U;
    first.transportGeneration = 3U;
    first.playing = true;
    mod::publishProjectControlTimeTelemetry(telemetry, first);
    auto second = first;
    second.musicalTick = 104U;
    second.monotonicMs = 1010U;
    mod::publishProjectControlTimeTelemetry(telemetry, second);
    const auto extrapolated = mod::extrapolateProjectControlTime(
        telemetry,
        1015U
    );
    assert(extrapolated.musicalTick == 106U);
    assert(extrapolated.musicalTickFractionQ16 == 0U);
    assert(extrapolated.monotonicMs == 1015U);

    const auto bounded = mod::extrapolateProjectControlTime(telemetry, 2010U);
    assert(bounded.musicalTick == 144U);
    std::cout << "[PASS] UI clock extrapolation is coherent and bounded\n";
}

void testUiSourceProjectionReusesRuntimePhaseAuthority() {
    auto domain = std::make_unique<mod::ProjectControlDomainState>();
    const auto source = addLfo(
        *domain,
        mod::ModulatorLfoShape::TRIANGLE,
        192U
    );
    auto* authored = mod::findProjectModulator(domain->modulation, source);
    assert(authored != nullptr);
    authored->parameters.lfo.phaseQ15 = 8192;
    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectControlRuntimePlan(
        *domain,
        activeContext(),
        *plan
    ).compiled());
    RuntimeFixture runtime;
    runtime.activate(*plan);
    mod::ProjectControlTimeSnapshot time{};
    time.musicalTick = 48U;
    time.monotonicMs = 250U;
    mod::ProjectModulatorRuntimeProjection projection{};
    assert(mod::projectModulatorRuntimeProjection(
        *plan,
        domain->curves,
        *runtime.state,
        time,
        source,
        projection
    ));
    assert(projection.positionKnown);
    assert(std::abs(static_cast<int>(projection.positionQ16) - 32768) <= 2);
    const auto previewPosition = mod::projectLfoPreviewPositionQ16(
        projection.positionQ16,
        authored->parameters.lfo.phaseQ15
    );
    assert(std::abs(static_cast<int>(previewPosition) - 16384) <= 2);
    const auto shapePosition = mod::projectLfoShapePositionQ16(
        previewPosition,
        authored->parameters.lfo.phaseQ15
    );
    assert(shapePosition == projection.positionQ16);
    for (const int16_t phase : {
             static_cast<int16_t>(-32767),
             static_cast<int16_t>(-16384),
             static_cast<int16_t>(0),
             static_cast<int16_t>(16384),
             static_cast<int16_t>(32767),
         }) {
        for (const uint16_t position : {
                 static_cast<uint16_t>(0U),
                 static_cast<uint16_t>(1U),
                 static_cast<uint16_t>(32768U),
                 static_cast<uint16_t>(65535U),
             }) {
            assert(mod::projectLfoPreviewPositionQ16(
                mod::projectLfoShapePositionQ16(position, phase),
                phase
            ) == position);
        }
    }
    std::cout
        << "[PASS] UI source marker shares runtime phase without double-shifting preview\n";
}

}  // namespace

int main() {
    testFiveCanonicalLfoShapes();
    testCanonicalAdsrProgressCurves();
    testFreeAdsrTrackTriggerRetriggerAndReleaseFromCurrentLevel();
    testAdsrLegatoAndZeroDurationTransitions();
    testExactMaximumTriggerBucketFansOutWithoutTruncation();
    testSyncAdsrUsesFractionalMusicalTime();
    testDahdsrFreeStagesAndZeroDurationTransitions();
    testSyncDahdsrUsesIndependentSegmentFeel();
    testAdsrTrackNoteVelocityFilterAndAcceptedNoteRelease();
    testDroppedTriggerFrameFailsSafeToRelease();
    testAdsrSmoothZeroIsExactAndNonzeroExposesRawProjection();
    testSyncEnvelopeSmoothUsesItsOwnFeel();
    testGateOffReleasesFromCurrentDelayHoldAndDecayLevels();
    testAdsrRouteRecompilePreservesHarmlessEditsAndResetsRangeEdits();
    testSyncEnvelopeFollowsMusicalTicksAcrossTempoChanges();
    testLogicalBaseAutomationManualAndSharedSource();
    testDestinationScaleAppliesOnceAfterSummingContributions();
    testSyncFreeTransportAndExplicitRetrigger();
    testFractionalRecordedCurveAndFromBaseBinding();
    testPositiveRecordedCurveUsesNaturalAndExplicitAroundBase();
    testRecordedShapeKeepsRelativeExcursionUntilFinalDestinationClamp();
    testRecordedShapeAuditionPublishesAStaticDestinationAbsentFromPlan();
    testRecordedCurveHintPreservesJumpsWrapAndDuplicateTicks();
    testExplicitPerBindingSlewAndFailureAtomicity();
    testRuntimeStateSurvivesStableIdReordering();
    testExactMaximumGraphEvaluatesEverySourceAndBinding();
    testUiTimeTelemetryExtrapolatesBoundedly();
    testUiSourceProjectionReusesRuntimePhaseAuthority();
    std::cout << "Project control runtime tests passed\n";
    return 0;
}

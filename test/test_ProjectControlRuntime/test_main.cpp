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

mod::ModulatorReach projectReach() {
    mod::ModulatorReach reach{};
    reach.kind = mod::ModulatorReachKind::PROJECT;
    return reach;
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
    draft.reach = projectReach();
    draft.parameters.shape = shape;
    draft.parameters.periodTicks = periodTicks;
    draft.parameters.freePeriodMs = freePeriodMs;
    draft.parameters.timing = timing;
    draft.parameters.retrigger = retrigger;
    const auto result = mod::createLfoModulator(domain.modulation, draft);
    assert(result.changed());
    return result.sourceId;
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
    assert(near(runtime.frame->destinations[0].base, 0.2f));
    assert(near(runtime.frame->destinations[0].value, 0.45f));
    assert((runtime.frame->destinations[0].flags &
            mod::PROJECT_LOGICAL_MACRO_FLAG_MANUAL_OVERRIDE) != 0U);
    assert((runtime.frame->destinations[0].flags &
            mod::PROJECT_LOGICAL_MACRO_FLAG_AUTOMATION_ACTIVE) == 0U);
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
        mod::ModulationTriggerKind::MANUAL,
        0U,
        0U,
        7U,
    };
    time.monotonicMs = 260U;
    assert(runtime.evaluate(*plan, domain->curves, time).evaluated());
    assert(near(runtime.frame->sourceValues[3], -1.0f));
    runtime.triggers.count = 0U;
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
    shape.reach = projectReach();
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
    shape.reach = projectReach();
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
        9U,
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
    runtime.triggers.events[0] = trigger.trigger;
    mod::ProjectControlTimeSnapshot time{};
    time.musicalTick = 40U;
    time.monotonicMs = 40U;
    assert(runtime.evaluate(*firstPlan, domain->curves, time).evaluated());
    runtime.triggers.count = 0U;
    const uint32_t retainedAnchor = runtime.state->sources[1]
        .explicitMusicalAnchorTick;

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
    assert(runtime.state->sources[0].explicitMusicalAnchorTick == retainedAnchor);
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

}  // namespace

int main() {
    testFiveCanonicalLfoShapes();
    testLogicalBaseAutomationManualAndSharedSource();
    testSyncFreeTransportAndExplicitRetrigger();
    testFractionalRecordedCurveAndFromBaseBinding();
    testPositiveRecordedCurveUsesNaturalAndExplicitAroundBase();
    testExplicitPerBindingSlewAndFailureAtomicity();
    testRuntimeStateSurvivesStableIdReordering();
    testExactMaximumGraphEvaluatesEverySourceAndBinding();
    std::cout << "Project control runtime tests passed\n";
    return 0;
}

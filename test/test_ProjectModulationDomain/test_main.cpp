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

#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/modulation/ProjectModulationRuntimePlan.hpp"

namespace {

namespace mod = core::state::modulation;

struct Fixture {
    std::unique_ptr<mod::ProjectModulationState> state =
        std::make_unique<mod::ProjectModulationState>();
    std::unique_ptr<mod::ProjectCurveArena> arena =
        std::make_unique<mod::ProjectCurveArena>();
};

mod::ModulatorReach projectReach() {
    mod::ModulatorReach reach{};
    reach.kind = mod::ModulatorReachKind::PROJECT;
    return reach;
}

mod::ModulatorReach macroReach(uint8_t track, uint8_t page, uint8_t macro) {
    mod::ModulatorReach reach{};
    reach.kind = mod::ModulatorReachKind::MACRO;
    reach.track = track;
    reach.page = page;
    reach.macro = macro;
    return reach;
}

mod::ModulatorReach trackReach(uint16_t trackMask) {
    mod::ModulatorReach reach{};
    reach.kind = mod::ModulatorReachKind::TRACK_SET;
    reach.trackMask = trackMask;
    return reach;
}

mod::ModulationDestination destination(
    uint8_t track,
    uint8_t page,
    uint8_t macro
) {
    return {
        mod::ModulationDestinationKind::MACRO_SLOT,
        track,
        page,
        macro,
    };
}

mod::ModulatorId addLfo(
    Fixture& fixture,
    const mod::ModulatorReach& reach = projectReach(),
    const char* name = "LFO"
) {
    mod::ModulatorLfoDraft draft{};
    draft.name = name;
    draft.reach = reach;
    const auto created = mod::createLfoModulator(*fixture.state, draft);
    assert(created.changed());
    return created.sourceId;
}

mod::ModulatorId addAdsr(
    Fixture& fixture,
    const mod::ModulatorReach& reach = projectReach(),
    const char* name = "ADSR"
) {
    mod::ModulatorAdsrDraft draft{};
    draft.name = name;
    draft.reach = reach;
    const auto created = mod::createAdsrModulator(*fixture.state, draft);
    assert(created.changed());
    return created.sourceId;
}

mod::ModulationBindingId addBinding(
    Fixture& fixture,
    mod::ModulatorId source,
    const mod::ModulationDestination& target,
    int16_t amount = 16384,
    mod::ModulationApplication application = mod::ModulationApplication::NATURAL,
    bool enabled = true
) {
    mod::ModulationBindingDraft draft{};
    draft.sourceId = source;
    draft.destination = target;
    draft.amountQ15 = amount;
    draft.application = application;
    draft.enabled = enabled;
    const auto created = mod::addProjectModulationBinding(*fixture.state, draft);
    assert(created.changed());
    return created.bindingId;
}

std::vector<mod::ProjectPackedCurvePoint> linearPoints(uint16_t count) {
    std::vector<mod::ProjectPackedCurvePoint> points(count);
    for (uint16_t index = 0; index < count; ++index) {
        points[index].tick = index;
        points[index].value = static_cast<int16_t>(
            static_cast<int32_t>(index % 32767U) - 16383
        );
    }
    return points;
}

mod::ProjectCurveSpec curveSpec(
    uint16_t duration,
    mod::ProjectCurveValueDomain valueDomain =
        mod::ProjectCurveValueDomain::BIPOLAR
) {
    mod::ProjectCurveSpec spec{};
    spec.sourceDurationTicks = duration;
    spec.durationTicks = duration;
    spec.valueDomain = valueDomain;
    return spec;
}

mod::ModulatorId addRecorded(
    Fixture& fixture,
    const std::vector<mod::ProjectPackedCurvePoint>& points,
    const mod::ModulatorReach& reach = projectReach(),
    const char* name = "Shape",
    mod::ProjectCurveValueDomain valueDomain =
        mod::ProjectCurveValueDomain::BIPOLAR
) {
    mod::RecordedShapeDraft draft{};
    draft.name = name;
    draft.reach = reach;
    draft.curve = curveSpec(
        static_cast<uint16_t>(points.empty() ? 1U : points.size()),
        valueDomain
    );
    draft.points = points.data();
    draft.pointCount = static_cast<uint16_t>(points.size());
    const auto created = mod::createRecordedShapeModulator(
        *fixture.state,
        *fixture.arena,
        draft
    );
    assert(created.changed());
    return created.sourceId;
}

mod::ProjectModulationCompileContext allMacrosOnPage(uint8_t page) {
    mod::ProjectModulationCompileContext context{};
    context.enabledTrackMask = 0xFFFFU;
    context.activePage.fill(page);
    context.activeMacroMask.fill(0xFFU);
    return context;
}

bool near(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

void testExactMemoryContract() {
    assert(sizeof(mod::ModulationDestinationScaleState) == 6U);
    assert(sizeof(mod::ModulatorAdsrParameters) == 12U);
    assert(sizeof(mod::ModulatorParameters) == 16U);
    assert(sizeof(mod::ModulatorSourceState) == 48U);
    assert(sizeof(mod::ProjectModulationState) == 21520U);
    assert(sizeof(mod::ProjectAutomationCurveDirectory) == 1540U);
    assert(sizeof(mod::ProjectCurveArena) == 137480U);
    assert(sizeof(mod::ProjectModulationRuntimePlan) == 15888U);
    assert(sizeof(mod::ProjectModulationState) +
               sizeof(mod::ProjectAutomationCurveDirectory) +
               sizeof(mod::ProjectCurveArena) ==
           160540U);
}

void testAdsrDomainIsPositiveCompactAndStrict() {
    Fixture fixture;
    const auto sourceId = addAdsr(fixture);
    const auto* source = mod::findProjectModulator(*fixture.state, sourceId);
    assert(source != nullptr);
    assert(source->kind == mod::ModulatorKind::ADSR);
    assert(source->parameters.adsr.attack == 16U);
    assert(source->parameters.adsr.decay == 250U);
    assert(source->parameters.adsr.release == 500U);
    assert(source->parameters.adsr.sustainQ15 ==
           mod::PROJECT_MODULATOR_ADSR_DEFAULT_SUSTAIN_Q15);
    assert(source->parameters.adsr.timing == mod::ModulatorTimingMode::FREE);
    assert(source->parameters.adsr.retrigger ==
           mod::ModulatorAdsrRetriggerMode::RETRIGGER);
    assert(source->parameters.adsr.curve ==
           mod::ModulatorAdsrCurve::EXPONENTIAL);

    mod::ModulatorNaturalDomain domain{};
    assert(mod::projectModulatorNaturalDomain(
        *source,
        *fixture.arena,
        domain
    ));
    assert(domain == mod::ModulatorNaturalDomain::POSITIVE);
    mod::ResolvedModulationMapping mapping{};
    assert(mod::resolveModulationApplication(
        mod::ModulationApplication::NATURAL,
        domain,
        mapping
    ));
    assert(mapping == mod::ResolvedModulationMapping::IDENTITY);
    assert(mod::resolveModulationApplication(
        mod::ModulationApplication::AROUND_BASE,
        domain,
        mapping
    ));
    assert(mapping == mod::ResolvedModulationMapping::POSITIVE_TO_CENTERED);

    auto parameters = source->parameters.adsr;
    parameters.attack = 0U;
    parameters.decay = 384U;
    parameters.release = 768U;
    parameters.sustainQ15 = mod::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15;
    parameters.timing = mod::ModulatorTimingMode::SYNC;
    parameters.retrigger = mod::ModulatorAdsrRetriggerMode::LEGATO;
    parameters.curve = mod::ModulatorAdsrCurve::SMOOTH;
    assert(mod::setProjectAdsrParameters(
        *fixture.state,
        sourceId,
        parameters
    ).changed());
    assert(mod::setProjectAdsrParameters(
        *fixture.state,
        sourceId,
        parameters
    ).status == mod::ProjectModulationStatus::NO_CHANGE);
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));

    const auto stable = *fixture.state;
    parameters.sustainQ15 = static_cast<uint16_t>(
        mod::PROJECT_MODULATOR_ADSR_SUSTAIN_ONE_Q15 + 1U
    );
    assert(mod::setProjectAdsrParameters(
        *fixture.state,
        sourceId,
        parameters
    ).status == mod::ProjectModulationStatus::INVALID_ARGUMENT);
    assert(std::memcmp(fixture.state.get(), &stable, sizeof(stable)) == 0);

    mod::ModulationTriggerDraft trigger{};
    trigger.sourceId = sourceId;
    trigger.trigger = {
        mod::ModulationTriggerKind::TRACK_NOTE,
        3U,
        mod::PROJECT_MODULATION_TRIGGER_ANY_CHANNEL,
        mod::PROJECT_MODULATION_TRIGGER_ANY_NOTE,
    };
    assert(mod::addProjectModulationTrigger(*fixture.state, trigger).changed());
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
    const auto* originalTrigger = mod::findProjectModulationTriggerForSource(
        *fixture.state,
        sourceId
    );
    assert(originalTrigger != nullptr);
    auto editedTrigger = originalTrigger->trigger;
    editedTrigger.track = 4U;
    assert(mod::setProjectModulationTrigger(
        *fixture.state,
        sourceId,
        editedTrigger,
        false
    ).changed());
    assert(mod::setProjectModulationTrigger(
        *fixture.state,
        sourceId,
        editedTrigger,
        false
    ).status == mod::ProjectModulationStatus::NO_CHANGE);
    const auto triggerStable = *fixture.state;
    editedTrigger.track = mod::PROJECT_MODULATION_TRACK_COUNT;
    assert(mod::setProjectModulationTrigger(
        *fixture.state,
        sourceId,
        editedTrigger,
        true
    ).status == mod::ProjectModulationStatus::INVALID_ARGUMENT);
    assert(std::memcmp(
        fixture.state.get(),
        &triggerStable,
        sizeof(triggerStable)
    ) == 0);

    const auto duplicate = mod::duplicateProjectModulator(
        *fixture.state,
        *fixture.arena,
        sourceId,
        "ADSR 2"
    );
    assert(duplicate.changed());
    assert(fixture.state->triggerBindingCount == 2U);
    const auto* clone = mod::findProjectModulator(
        *fixture.state,
        duplicate.sourceId
    );
    assert(clone != nullptr && clone->kind == mod::ModulatorKind::ADSR);
    assert(std::memcmp(
        &clone->parameters.adsr,
        &stable.sources[0].parameters.adsr,
        sizeof(mod::ModulatorAdsrParameters)
    ) == 0);
    const auto* clonedTrigger = mod::findProjectModulationTriggerForSource(
        *fixture.state,
        duplicate.sourceId
    );
    assert(clonedTrigger != nullptr);
    assert(clonedTrigger->id !=
           fixture.state->triggerBindings[0].id);
    assert(clonedTrigger->trigger ==
           fixture.state->triggerBindings[0].trigger);
    assert(clonedTrigger->flags == fixture.state->triggerBindings[0].flags);

    auto* bytes = reinterpret_cast<uint8_t*>(
        &fixture.state->sources[0].parameters
    );
    bytes[sizeof(mod::ModulatorAdsrParameters)] = 1U;
    assert(!mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
}

void testDestinationScaleIsSparseOrderedAndPrunedWithLastBinding() {
    Fixture fixture;
    const auto source = addLfo(fixture);
    const auto firstDestination = destination(0, 0, 0);
    const auto secondDestination = destination(0, 0, 1);

    const auto empty = std::make_unique<mod::ProjectModulationState>(
        *fixture.state
    );
    assert(mod::setProjectModulationDestinationScale(
        *fixture.state,
        firstDestination,
        0U
    ).status == mod::ProjectModulationStatus::INVALID_ARGUMENT);
    assert(std::memcmp(
        fixture.state.get(),
        empty.get(),
        sizeof(*fixture.state)
    ) == 0);

    const auto firstBinding = addBinding(fixture, source, firstDestination);
    const auto secondBinding = addBinding(fixture, source, secondDestination);
    assert(mod::projectModulationDestinationScaleQ15(
        *fixture.state,
        firstDestination
    ) == mod::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15);
    assert(mod::setProjectModulationDestinationScale(
        *fixture.state,
        secondDestination,
        65535U
    ).changed());
    assert(mod::setProjectModulationDestinationScale(
        *fixture.state,
        firstDestination,
        0U
    ).changed());
    assert(fixture.state->destinationScaleCount == 2U);
    assert(fixture.state->destinationScales[0].destination == firstDestination);
    assert(fixture.state->destinationScales[0].scaleQ15 == 0U);
    assert(fixture.state->destinationScales[1].destination == secondDestination);
    assert(fixture.state->destinationScales[1].scaleQ15 == 65535U);
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));

    assert(mod::setProjectModulationDestinationScale(
        *fixture.state,
        firstDestination,
        mod::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15
    ).changed());
    assert(fixture.state->destinationScaleCount == 1U);
    assert(mod::removeProjectModulationBinding(
        *fixture.state,
        secondBinding
    ).changed());
    assert(fixture.state->destinationScaleCount == 0U);
    assert(mod::removeProjectModulationBinding(
        *fixture.state,
        firstBinding
    ).changed());
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
}

void testDestinationScaleValidatorRejectsOrphanUnityAndUnsortedEntries() {
    Fixture fixture;
    const auto source = addLfo(fixture);
    const auto firstDestination = destination(0, 0, 0);
    const auto secondDestination = destination(0, 0, 1);
    addBinding(fixture, source, firstDestination);
    addBinding(fixture, source, secondDestination);
    assert(mod::setProjectModulationDestinationScale(
        *fixture.state,
        firstDestination,
        1000U
    ).changed());
    assert(mod::setProjectModulationDestinationScale(
        *fixture.state,
        secondDestination,
        2000U
    ).changed());

    fixture.state->destinationScales[0].scaleQ15 =
        mod::PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    assert(!mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
    fixture.state->destinationScales[0].scaleQ15 = 1000U;
    std::swap(
        fixture.state->destinationScales[0],
        fixture.state->destinationScales[1]
    );
    assert(!mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
    std::swap(
        fixture.state->destinationScales[0],
        fixture.state->destinationScales[1]
    );
    fixture.state->destinationScales[0].destination = destination(0, 1, 0);
    assert(!mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
}

void testDestinationScaleCompilesAndAppliesOnceBeforeFinalClamp() {
    Fixture fixture;
    const auto target = destination(0, 0, 0);
    const auto first = addLfo(fixture);
    const auto second = addLfo(fixture);
    addBinding(fixture, first, target, 8192);
    addBinding(fixture, second, target, 8192);
    assert(mod::setProjectModulationDestinationScale(
        *fixture.state,
        target,
        16384U
    ).changed());

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectModulationRuntimePlan(
        *fixture.state,
        *fixture.arena,
        allMacrosOnPage(0),
        *plan
    ).compiled());
    assert(plan->destinationCount == 1U);
    assert(plan->destinations[0].destinationScaleQ15 == 16384U);
    const std::array<float, 2> values{{1.0f, 1.0f}};
    auto resolved = mod::resolveProjectModulationDestination(
        *plan,
        0U,
        values.data(),
        0.25f
    );
    assert(resolved.valid && !resolved.clipped);
    assert(resolved.contributionCount == 2U);
    assert(near(resolved.modulation, 0.25f, 0.0001f));
    assert(near(resolved.value, 0.5f, 0.0001f));

    assert(mod::setProjectModulationDestinationScale(
        *fixture.state,
        target,
        0U
    ).changed());
    assert(mod::compileProjectModulationRuntimePlan(
        *fixture.state,
        *fixture.arena,
        allMacrosOnPage(0),
        *plan
    ).compiled());
    resolved = mod::resolveProjectModulationDestination(
        *plan,
        0U,
        values.data(),
        0.25f
    );
    assert(resolved.valid && near(resolved.modulation, 0.0f));
    assert(near(resolved.value, 0.25f));
    assert(fixture.state->outputBindings[0].amountQ15 == 8192);
    assert(fixture.state->outputBindings[1].amountQ15 == 8192);
}

void testCurveContractPreservesLegacyLoopWindowsAndSameTickPoints() {
    const std::array<mod::ProjectPackedCurvePoint, 3> points{{
        {0U, -12000},
        {0U, -8000},
        {192U, 12000},
    }};
    mod::ProjectCurveSpec spec{};
    spec.sourceDurationTicks = 192U;
    spec.durationTicks = 768U;
    spec.windowOffsetTicks = 192U;
    spec.valueDomain = mod::ProjectCurveValueDomain::BIPOLAR;
    assert(mod::validProjectCurveSpec(
        spec,
        points.data(),
        static_cast<uint16_t>(points.size())
    ));

    spec.windowOffsetTicks = 193U;
    assert(!mod::validProjectCurveSpec(
        spec,
        points.data(),
        static_cast<uint16_t>(points.size())
    ));
}

void testStableIdsDuplicateAndDelete() {
    Fixture fixture;
    const auto first = addLfo(fixture, projectReach(), "First");
    const auto keeper = addLfo(fixture, projectReach(), "Keeper");
    assert(first.value == 1U && keeper.value == 2U);

    mod::ModulationTriggerDraft trigger{};
    trigger.sourceId = keeper;
    trigger.trigger.kind = mod::ModulationTriggerKind::TRANSPORT_START;
    assert(mod::addProjectModulationTrigger(*fixture.state, trigger).changed());

    assert(mod::deleteProjectModulator(
        *fixture.state,
        *fixture.arena,
        first
    ).changed());
    assert(mod::findProjectModulator(*fixture.state, keeper) != nullptr);

    const auto third = addLfo(fixture, projectReach(), "Third");
    assert(third.value == 3U);
    const auto clone = mod::duplicateProjectModulator(
        *fixture.state,
        *fixture.arena,
        keeper,
        "Clone"
    );
    assert(clone.changed() && clone.sourceId.value == 4U);
    // A typed trigger is part of the portable source definition; output
    // destinations remain intentionally absent from a root duplicate.
    assert(fixture.state->triggerBindingCount == 2U);
    assert(fixture.state->triggerBindings[1].sourceId == clone.sourceId);
    assert(fixture.state->triggerBindings[1].trigger ==
           fixture.state->triggerBindings[0].trigger);
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
}

void testReachAndDuplicateBindingAreStrictAndAtomic() {
    Fixture fixture;
    const auto source = addLfo(fixture, macroReach(2, 3, 4));
    addBinding(fixture, source, destination(2, 3, 4));

    const auto stable = std::make_unique<mod::ProjectModulationState>(
        *fixture.state
    );
    mod::ModulationBindingDraft outside{};
    outside.sourceId = source;
    outside.destination = destination(2, 3, 5);
    outside.amountQ15 = 1000;
    assert(mod::addProjectModulationBinding(*fixture.state, outside).status ==
           mod::ProjectModulationStatus::REACH_VIOLATION);
    assert(std::memcmp(
        fixture.state.get(),
        stable.get(),
        sizeof(*fixture.state)
    ) == 0);

    mod::ModulationBindingDraft duplicate{};
    duplicate.sourceId = source;
    duplicate.destination = destination(2, 3, 4);
    duplicate.amountQ15 = -8000;
    assert(mod::addProjectModulationBinding(*fixture.state, duplicate).status ==
           mod::ProjectModulationStatus::DUPLICATE_BINDING);
    assert(std::memcmp(
        fixture.state.get(),
        stable.get(),
        sizeof(*fixture.state)
    ) == 0);

    assert(mod::setProjectModulatorReach(
        *fixture.state,
        source,
        macroReach(1, 0, 0)
    ).status == mod::ProjectModulationStatus::REACH_VIOLATION);
    assert(std::memcmp(
        fixture.state.get(),
        stable.get(),
        sizeof(*fixture.state)
    ) == 0);
}

void testAdvertised128SourcesAnd512BindingsCompileWithoutTruncation() {
    Fixture fixture;
    for (uint16_t sourceIndex = 0;
         sourceIndex < mod::PROJECT_MODULATOR_CAPACITY;
         ++sourceIndex) {
        const auto source = addLfo(fixture);
        for (uint8_t offset = 0; offset < 4U; ++offset) {
            // Every source contributes to Macro 1, proving that 512 is a
            // project-wide budget rather than an implicit per-Macro cap. The
            // remaining edges cover every other live destination.
            const uint16_t address = offset == 0U
                ? 0U
                : static_cast<uint16_t>(
                    1U + (sourceIndex * 3U + offset - 1U) % 127U
                );
            addBinding(
                fixture,
                source,
                destination(
                    static_cast<uint8_t>(address / 8U),
                    0,
                    static_cast<uint8_t>(address % 8U)
                )
            );
        }
    }
    assert(fixture.state->sourceCount == 128U);
    assert(fixture.state->outputBindingCount == 512U);
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));

    const auto fullState = std::make_unique<mod::ProjectModulationState>(
        *fixture.state
    );
    mod::ModulatorLfoDraft overflowSource{};
    overflowSource.reach = projectReach();
    assert(mod::createLfoModulator(*fixture.state, overflowSource).status ==
           mod::ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED);
    assert(std::memcmp(
        fixture.state.get(),
        fullState.get(),
        sizeof(*fixture.state)
    ) == 0);

    mod::ModulationBindingDraft overflowBinding{};
    overflowBinding.sourceId = fixture.state->sources[0].id;
    overflowBinding.destination = destination(0, 1, 0);
    overflowBinding.amountQ15 = 1000;
    assert(mod::addProjectModulationBinding(
        *fixture.state,
        overflowBinding
    ).status == mod::ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED);
    assert(std::memcmp(
        fixture.state.get(),
        fullState.get(),
        sizeof(*fixture.state)
    ) == 0);

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    const auto compiled = mod::compileProjectModulationRuntimePlan(
        *fixture.state,
        *fixture.arena,
        allMacrosOnPage(0),
        *plan
    );
    assert(compiled.compiled());
    assert(plan->sourceCount == 128U);
    assert(plan->bindingCount == 512U);
    assert(plan->destinationCount == 128U);
    assert(plan->inactiveBindingCount == 0U);
    uint16_t compiledBindingTotal = 0;
    for (uint16_t index = 0; index < plan->destinationCount; ++index) {
        compiledBindingTotal = static_cast<uint16_t>(
            compiledBindingTotal + plan->destinations[index].bindingCount
        );
    }
    assert(compiledBindingTotal == 512U);
    assert(plan->destinations[0].stableAddress == 0U);
    assert(plan->destinations[0].bindingCount == 128U);

    std::array<float, mod::PROJECT_MODULATOR_CAPACITY> values{};
    values.fill(1.0f);
    const auto resolved = mod::resolveProjectModulationDestination(
        *plan,
        0,
        values.data(),
        0.0f
    );
    assert(resolved.valid && resolved.clipped);
    assert(resolved.contributionCount == 128U);
    assert(near(resolved.value, 1.0f));
}

void testInactivePagesAreExplicitlyExcluded() {
    Fixture fixture;
    const auto source = addLfo(fixture);
    addBinding(fixture, source, destination(0, 0, 0));
    addBinding(fixture, source, destination(0, 1, 1));

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    auto context = allMacrosOnPage(0);
    auto compiled = mod::compileProjectModulationRuntimePlan(
        *fixture.state,
        *fixture.arena,
        context,
        *plan
    );
    assert(compiled.compiled());
    assert(plan->sourceCount == 1U);
    assert(plan->bindingCount == 1U);
    assert(plan->destinationCount == 1U);
    assert(plan->inactiveBindingCount == 1U);

    context.activePage[0] = 1;
    compiled = mod::compileProjectModulationRuntimePlan(
        *fixture.state,
        *fixture.arena,
        context,
        *plan
    );
    assert(compiled.compiled());
    assert(plan->bindingCount == 1U && plan->inactiveBindingCount == 1U);
    assert(plan->destinations[0].destination.page == 1U);
}

void testRuntimeCompilationFailureDoesNotPublishPartialPlan() {
    Fixture fixture;
    const auto source = addLfo(fixture);
    addBinding(fixture, source, destination(0, 0, 0));
    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectModulationRuntimePlan(
        *fixture.state,
        *fixture.arena,
        allMacrosOnPage(0),
        *plan
    ).compiled());
    const auto stablePlan = std::make_unique<mod::ProjectModulationRuntimePlan>(
        *plan
    );

    auto invalidContext = allMacrosOnPage(0);
    invalidContext.activePage[0] = mod::PROJECT_MODULATION_PAGE_COUNT;
    assert(mod::compileProjectModulationRuntimePlan(
        *fixture.state,
        *fixture.arena,
        invalidContext,
        *plan
    ).status == mod::ProjectModulationCompileStatus::INVALID_CONTEXT);
    assert(std::memcmp(plan.get(), stablePlan.get(), sizeof(*plan)) == 0);

    fixture.state->outputBindings[0].sourceId = {9999U};
    assert(mod::compileProjectModulationRuntimePlan(
        *fixture.state,
        *fixture.arena,
        allMacrosOnPage(0),
        *plan
    ).status == mod::ProjectModulationCompileStatus::INVALID_DOMAIN);
    assert(std::memcmp(plan.get(), stablePlan.get(), sizeof(*plan)) == 0);
}

void testSplitSharesAFullCurveWithoutCopyingPoints() {
    Fixture fixture;
    const auto points = linearPoints(mod::PROJECT_CURVE_POINT_CAPACITY);
    const auto source = addRecorded(fixture, points);
    const auto retainedBinding = addBinding(
        fixture,
        source,
        destination(0, 0, 0)
    );
    const auto movedBinding = addBinding(
        fixture,
        source,
        destination(1, 0, 0)
    );
    (void)retainedBinding;
    mod::ModulationTriggerDraft trigger{};
    trigger.sourceId = source;
    assert(mod::addProjectModulationTrigger(*fixture.state, trigger).changed());

    mod::ModulatorSplitRequest split{};
    split.sourceId = source;
    split.cloneName = "Track 2 Shape";
    split.retainedReach = trackReach(1U << 0U);
    split.cloneReach = trackReach(1U << 1U);
    split.bindingIdsToMove = &movedBinding;
    split.bindingCountToMove = 1;
    const auto created = mod::splitProjectModulator(
        *fixture.state,
        *fixture.arena,
        split
    );
    assert(created.changed());
    assert(fixture.state->sourceCount == 2U);
    assert(fixture.arena->recordCount == 1U);
    assert(fixture.arena->pointCount == mod::PROJECT_CURVE_POINT_CAPACITY);
    assert(fixture.arena->records[0].referenceCount == 2U);
    assert(fixture.state->triggerBindingCount == 2U);
    assert(fixture.state->triggerBindings[0].id !=
           fixture.state->triggerBindings[1].id);
    assert(fixture.state->sources[0].parameters.recordedCurveId ==
           fixture.state->sources[1].parameters.recordedCurveId);
    assert(fixture.state->outputBindings[1].sourceId == created.sourceId);
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
}

void testSharedCurveEditUsesCopyOnWrite() {
    Fixture fixture;
    const auto originalPoints = linearPoints(3);
    const auto original = addRecorded(fixture, originalPoints);
    const auto clone = mod::duplicateProjectModulator(
        *fixture.state,
        *fixture.arena,
        original,
        "Variant"
    );
    assert(clone.changed());
    const auto originalCurve = fixture.state->sources[0].parameters.recordedCurveId;
    assert(fixture.arena->records[0].referenceCount == 2U);

    auto replacement = linearPoints(2);
    replacement[1].value = 12345;
    const auto replaced = mod::replaceRecordedShapeCurve(
        *fixture.state,
        *fixture.arena,
        clone.sourceId,
        curveSpec(2),
        replacement.data(),
        static_cast<uint16_t>(replacement.size())
    );
    assert(replaced.changed());
    assert(replaced.curveId != originalCurve);
    assert(fixture.arena->recordCount == 2U);
    assert(fixture.arena->pointCount == 5U);
    assert(mod::findProjectCurve(*fixture.arena, originalCurve)->referenceCount == 1U);
    assert(mod::findProjectCurve(*fixture.arena, replaced.curveId)->referenceCount == 1U);
    assert(fixture.arena->points[2].value == originalPoints[2].value);
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
}

void testDeleteRemovesEdgesAndCompactsCurveArenaWithStableIds() {
    Fixture fixture;
    auto firstPoints = linearPoints(3);
    auto survivorPoints = linearPoints(2);
    survivorPoints[0].value = -1234;
    survivorPoints[1].value = 5678;
    const auto removed = addRecorded(fixture, firstPoints, projectReach(), "A");
    const auto survivor = addRecorded(
        fixture,
        survivorPoints,
        projectReach(),
        "B"
    );
    const auto survivorCurve =
        fixture.state->sources[1].parameters.recordedCurveId;
    addBinding(fixture, removed, destination(0, 0, 0));
    mod::ModulationTriggerDraft trigger{};
    trigger.sourceId = removed;
    assert(mod::addProjectModulationTrigger(*fixture.state, trigger).changed());

    assert(mod::deleteProjectModulator(
        *fixture.state,
        *fixture.arena,
        removed
    ).changed());
    assert(fixture.state->sourceCount == 1U);
    assert(fixture.state->sources[0].id == survivor);
    assert(fixture.state->outputBindingCount == 0U);
    assert(fixture.state->triggerBindingCount == 0U);
    assert(fixture.arena->recordCount == 1U);
    assert(fixture.arena->pointCount == survivorPoints.size());
    assert(fixture.arena->records[0].id == survivorCurve);
    assert(fixture.arena->records[0].pointOffset == 0U);
    assert(std::memcmp(
        fixture.arena->points.data(),
        survivorPoints.data(),
        survivorPoints.size() * sizeof(mod::ProjectPackedCurvePoint)
    ) == 0);
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
}

void testFullSharedCurveCowFailureIsAtomic() {
    Fixture fixture;
    const auto points = linearPoints(mod::PROJECT_CURVE_POINT_CAPACITY);
    const auto original = addRecorded(fixture, points);
    const auto clone = mod::duplicateProjectModulator(
        *fixture.state,
        *fixture.arena,
        original,
        "Variant"
    );
    assert(clone.changed());
    const auto stableState = std::make_unique<mod::ProjectModulationState>(
        *fixture.state
    );
    const auto stableArena = std::make_unique<mod::ProjectCurveArena>(
        *fixture.arena
    );

    const std::array<mod::ProjectPackedCurvePoint, 2> replacement{{
        {0, -1000},
        {1, 1000},
    }};
    const auto failed = mod::replaceRecordedShapeCurve(
        *fixture.state,
        *fixture.arena,
        clone.sourceId,
        curveSpec(2),
        replacement.data(),
        static_cast<uint16_t>(replacement.size())
    );
    assert(failed.status ==
           mod::ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED);
    assert(std::memcmp(
        fixture.state.get(),
        stableState.get(),
        sizeof(*fixture.state)
    ) == 0);
    assert(std::memcmp(
        fixture.arena.get(),
        stableArena.get(),
        sizeof(*fixture.arena)
    ) == 0);
}

void testUniqueCurveCanReclaimItsFullPoolRange() {
    Fixture fixture;
    const auto original = linearPoints(mod::PROJECT_CURVE_POINT_CAPACITY);
    const auto source = addRecorded(fixture, original);
    const auto curveId = fixture.state->sources[0].parameters.recordedCurveId;
    auto replacement = linearPoints(mod::PROJECT_CURVE_POINT_CAPACITY - 1U);
    replacement.back().value = 16000;
    const auto replaced = mod::replaceRecordedShapeCurve(
        *fixture.state,
        *fixture.arena,
        source,
        curveSpec(static_cast<uint16_t>(replacement.size())),
        replacement.data(),
        static_cast<uint16_t>(replacement.size())
    );
    assert(replaced.changed() && replaced.curveId == curveId);
    assert(fixture.arena->recordCount == 1U);
    assert(fixture.arena->pointCount == replacement.size());
    assert(fixture.arena->points[fixture.arena->pointCount - 1U].value == 16000);
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
}

void testFailedSplitLeavesDomainUntouched() {
    Fixture fixture;
    const auto source = addLfo(fixture);
    addBinding(fixture, source, destination(0, 0, 0));
    const auto moved = addBinding(fixture, source, destination(1, 0, 0));
    const auto stableState = std::make_unique<mod::ProjectModulationState>(
        *fixture.state
    );
    const auto stableArena = std::make_unique<mod::ProjectCurveArena>(
        *fixture.arena
    );

    mod::ModulatorSplitRequest split{};
    split.sourceId = source;
    split.retainedReach = macroReach(0, 0, 0);
    split.cloneReach = macroReach(2, 0, 0);  // moved binding actually targets T2
    split.bindingIdsToMove = &moved;
    split.bindingCountToMove = 1;
    assert(mod::splitProjectModulator(
        *fixture.state,
        *fixture.arena,
        split
    ).status == mod::ProjectModulationStatus::REACH_VIOLATION);
    assert(std::memcmp(
        fixture.state.get(),
        stableState.get(),
        sizeof(*fixture.state)
    ) == 0);
    assert(std::memcmp(
        fixture.arena.get(),
        stableArena.get(),
        sizeof(*fixture.arena)
    ) == 0);
}

void testRuntimeSumClampOrderingAndEnableFlags() {
    Fixture fixture;
    const auto destination0 = destination(0, 0, 0);
    const auto centered = addLfo(fixture);
    const auto fromBase = addLfo(fixture);
    const auto dormant = addLfo(fixture);
    addBinding(fixture, centered, destination0, 16384);
    addBinding(
        fixture,
        fromBase,
        destination0,
        16384,
        mod::ModulationApplication::FROM_BASE
    );
    addBinding(fixture, dormant, destination0, -16384, mod::ModulationApplication::AROUND_BASE, false);

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectModulationRuntimePlan(
        *fixture.state,
        *fixture.arena,
        allMacrosOnPage(0),
        *plan
    ).compiled());
    assert(plan->destinationCount == 1U && plan->bindingCount == 3U);
    assert(plan->bindings[plan->bindingOrder[0]].id.value <
           plan->bindings[plan->bindingOrder[1]].id.value);

    std::array<float, mod::PROJECT_MODULATOR_CAPACITY> values{};
    values[0] = 1.0f;
    values[1] = 0.0f;  // centered-to-positive projection is 0.5
    values[2] = 1.0f;
    auto resolved = mod::resolveProjectModulationDestination(
        *plan,
        0,
        values.data(),
        0.5f
    );
    assert(resolved.valid && resolved.clipped);
    assert(resolved.contributionCount == 2U);
    assert(near(resolved.modulation, 0.75002f, 0.0001f));
    assert(near(resolved.value, 1.0f));

    plan->sources[0].flags = 0;
    resolved = mod::resolveProjectModulationDestination(
        *plan,
        0,
        values.data(),
        0.5f
    );
    assert(resolved.valid && !resolved.clipped);
    assert(resolved.contributionCount == 1U);
    assert(near(resolved.value, 0.75001f, 0.0001f));

    plan->bindings[1].flags = 0;
    resolved = mod::resolveProjectModulationDestination(
        *plan,
        0,
        values.data(),
        0.5f
    );
    assert(resolved.contributionCount == 0U);
    assert(near(resolved.value, 0.5f));

    assert(mod::setProjectModulatorEnabled(
        *fixture.state,
        centered,
        false
    ).changed());
    assert(mod::updateProjectModulationBinding(
        *fixture.state,
        fixture.state->outputBindings[1].id,
        16384,
        mod::ModulationApplication::FROM_BASE,
        mod::ModulationTransfer::LINEAR,
        false,
        321U
    ).changed());
    assert(fixture.state->outputBindings[1].slewMs == 321U);
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
}

void testNaturalApplicationResolvesSourceDomainBeforeTheHotLoop() {
    Fixture fixture;
    const auto lfo = addLfo(fixture);
    const std::vector<mod::ProjectPackedCurvePoint> positivePoints{
        {0U, 8192},
        {1U, 24576},
    };
    const auto positive = addRecorded(
        fixture,
        positivePoints,
        projectReach(),
        "Envelope",
        mod::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR
    );

    addBinding(
        fixture,
        lfo,
        destination(0, 0, 0),
        16384,
        mod::ModulationApplication::NATURAL
    );
    addBinding(
        fixture,
        lfo,
        destination(0, 0, 1),
        16384,
        mod::ModulationApplication::FROM_BASE
    );
    addBinding(
        fixture,
        positive,
        destination(0, 0, 2),
        16384,
        mod::ModulationApplication::NATURAL
    );
    addBinding(
        fixture,
        positive,
        destination(0, 0, 3),
        16384,
        mod::ModulationApplication::AROUND_BASE
    );
    addBinding(
        fixture,
        lfo,
        destination(0, 0, 4),
        26214,
        mod::ModulationApplication::NATURAL
    );
    addBinding(
        fixture,
        positive,
        destination(0, 0, 4),
        -22937,
        mod::ModulationApplication::AROUND_BASE
    );

    auto plan = std::make_unique<mod::ProjectModulationRuntimePlan>();
    assert(mod::compileProjectModulationRuntimePlan(
        *fixture.state,
        *fixture.arena,
        allMacrosOnPage(0),
        *plan
    ).compiled());
    assert(plan->bindings[0].mapping ==
           mod::ResolvedModulationMapping::IDENTITY);
    assert(plan->bindings[1].mapping ==
           mod::ResolvedModulationMapping::CENTERED_TO_POSITIVE);
    assert(plan->bindings[2].mapping ==
           mod::ResolvedModulationMapping::IDENTITY);
    assert(plan->bindings[3].mapping ==
           mod::ResolvedModulationMapping::POSITIVE_TO_CENTERED);

    std::array<float, mod::PROJECT_MODULATOR_CAPACITY> values{};
    values[0] = -1.0f;
    values[1] = 0.25f;
    auto resolved = mod::resolveProjectModulationDestination(
        *plan,
        0U,
        values.data(),
        0.5f
    );
    assert(resolved.valid && resolved.clipped && near(resolved.value, 0.0f));

    resolved = mod::resolveProjectModulationDestination(
        *plan,
        1U,
        values.data(),
        0.5f
    );
    assert(resolved.valid && near(resolved.value, 0.5f));
    resolved = mod::resolveProjectModulationDestination(
        *plan,
        2U,
        values.data(),
        0.5f
    );
    assert(resolved.valid && near(resolved.value, 0.625f, 0.0002f));
    resolved = mod::resolveProjectModulationDestination(
        *plan,
        3U,
        values.data(),
        0.5f
    );
    assert(resolved.valid && near(resolved.value, 0.25f, 0.0002f));

    values[0] = 1.0f;
    values[1] = 1.0f;
    resolved = mod::resolveProjectModulationDestination(
        *plan,
        4U,
        values.data(),
        0.8f
    );
    assert(resolved.valid && !resolved.clipped);
    assert(near(resolved.modulation, 0.1f, 0.0002f));
    assert(near(resolved.value, 0.9f, 0.0002f));
}

void testUnknownApplicationIsRejectedAtomically() {
    Fixture fixture;
    const auto source = addLfo(fixture);
    const auto stable = std::make_unique<mod::ProjectModulationState>(
        *fixture.state
    );
    mod::ModulationBindingDraft draft{};
    draft.sourceId = source;
    draft.destination = destination(0, 0, 0);
    draft.amountQ15 = 16384;
    draft.application = static_cast<mod::ModulationApplication>(3U);
    assert(mod::addProjectModulationBinding(*fixture.state, draft).status ==
           mod::ProjectModulationStatus::INVALID_ARGUMENT);
    assert(std::memcmp(
        fixture.state.get(),
        stable.get(),
        sizeof(*fixture.state)
    ) == 0);
}

void testSourceRenameIsBoundedAndPreservesStableGraphReferences() {
    Fixture fixture;
    const auto source = addLfo(fixture, projectReach(), "Original");
    const auto binding = addBinding(fixture, source, destination(1, 2, 3));
    const auto stableBinding = fixture.state->outputBindings[0];
    const auto stableReach = fixture.state->sources[0].reach;

    assert(mod::setProjectModulatorName(
        *fixture.state,
        source,
        "12345678901234567890"
    ).changed());
    assert(std::strcmp(
        fixture.state->sources[0].name.data(),
        "123456789012345"
    ) == 0);
    assert(fixture.state->sources[0].id == source);
    assert(std::memcmp(
        &fixture.state->sources[0].reach,
        &stableReach,
        sizeof(stableReach)
    ) == 0);
    assert(fixture.state->outputBindingCount == 1U);
    assert(fixture.state->outputBindings[0].id == binding);
    assert(std::memcmp(
        &fixture.state->outputBindings[0],
        &stableBinding,
        sizeof(stableBinding)
    ) == 0);

    const auto stable = std::make_unique<mod::ProjectModulationState>(
        *fixture.state
    );
    assert(mod::setProjectModulatorName(*fixture.state, source, nullptr).status ==
           mod::ProjectModulationStatus::INVALID_ARGUMENT);
    assert(mod::setProjectModulatorName(*fixture.state, source, "").status ==
           mod::ProjectModulationStatus::INVALID_ARGUMENT);
    assert(mod::setProjectModulatorName(
        *fixture.state,
        source,
        "123456789012345"
    ).status == mod::ProjectModulationStatus::NO_CHANGE);
    assert(std::memcmp(
        fixture.state.get(),
        stable.get(),
        sizeof(*fixture.state)
    ) == 0);
}

void testValidatorRejectsDanglingDuplicateAndBadReferenceCount() {
    Fixture fixture;
    const auto first = addLfo(fixture);
    const auto second = addLfo(fixture);
    addBinding(fixture, first, destination(0, 0, 0));
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));

    const auto secondId = fixture.state->sources[1].id;
    fixture.state->sources[1].id = first;
    assert(!mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
    fixture.state->sources[1].id = secondId;

    fixture.state->outputBindings[0].sourceId = {9999U};
    assert(!mod::validProjectModulationDomain(*fixture.state, *fixture.arena));
    fixture.state->outputBindings[0].sourceId = first;
    assert(mod::validProjectModulationDomain(*fixture.state, *fixture.arena));

    Fixture curveFixture;
    const auto points = linearPoints(2);
    const auto recorded = addRecorded(curveFixture, points);
    ++curveFixture.arena->records[0].referenceCount;
    assert(!mod::validProjectModulationDomain(
        *curveFixture.state,
        *curveFixture.arena
    ));
    --curveFixture.arena->records[0].referenceCount;

    const auto stableArena = std::make_unique<mod::ProjectCurveArena>(
        *curveFixture.arena
    );
    curveFixture.state->sources[0].parameters.recordedCurveId = {9999U};
    const auto malformedState = std::make_unique<mod::ProjectModulationState>(
        *curveFixture.state
    );
    assert(mod::deleteProjectModulator(
        *curveFixture.state,
        *curveFixture.arena,
        recorded
    ).status == mod::ProjectModulationStatus::INVARIANT_VIOLATION);
    assert(std::memcmp(
        curveFixture.state.get(),
        malformedState.get(),
        sizeof(*curveFixture.state)
    ) == 0);
    assert(std::memcmp(
        curveFixture.arena.get(),
        stableArena.get(),
        sizeof(*curveFixture.arena)
    ) == 0);

    Fixture crossChunkFixture;
    const std::vector<mod::ProjectPackedCurvePoint> positivePoints{
        {0U, 0},
        {1U, 32767},
    };
    addRecorded(
        crossChunkFixture,
        positivePoints,
        projectReach(),
        "Positive",
        mod::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR
    );
    mod::ProjectAutomationCurveDirectory automation{};
    automation.entryCount = 1U;
    automation.entries[0].destination = destination(0, 0, 0);
    automation.entries[0].curveId =
        crossChunkFixture.arena->records[0].id;
    automation.entries[0].flags =
        mod::PROJECT_AUTOMATION_CURVE_FLAG_ENABLED;
    ++crossChunkFixture.arena->records[0].referenceCount;
    assert(!mod::validProjectModulationDomain(
        *crossChunkFixture.state,
        *crossChunkFixture.arena,
        &automation
    ));
    (void)second;
}

}  // namespace

int main() {
    testExactMemoryContract();
    testAdsrDomainIsPositiveCompactAndStrict();
    testDestinationScaleIsSparseOrderedAndPrunedWithLastBinding();
    testDestinationScaleValidatorRejectsOrphanUnityAndUnsortedEntries();
    testDestinationScaleCompilesAndAppliesOnceBeforeFinalClamp();
    testCurveContractPreservesLegacyLoopWindowsAndSameTickPoints();
    testStableIdsDuplicateAndDelete();
    testReachAndDuplicateBindingAreStrictAndAtomic();
    testAdvertised128SourcesAnd512BindingsCompileWithoutTruncation();
    testInactivePagesAreExplicitlyExcluded();
    testRuntimeCompilationFailureDoesNotPublishPartialPlan();
    testSplitSharesAFullCurveWithoutCopyingPoints();
    testSharedCurveEditUsesCopyOnWrite();
    testDeleteRemovesEdgesAndCompactsCurveArenaWithStableIds();
    testFullSharedCurveCowFailureIsAtomic();
    testUniqueCurveCanReclaimItsFullPoolRange();
    testFailedSplitLeavesDomainUntouched();
    testRuntimeSumClampOrderingAndEnableFlags();
    testNaturalApplicationResolvesSourceDomainBeforeTheHotLoop();
    testUnknownApplicationIsRejectedAtomically();
    testSourceRenameIsBoundedAndPreservesStableGraphReferences();
    testValidatorRejectsDanglingDuplicateAndBadReferenceCount();
    std::cout << "All Project modulation domain tests passed.\n";
    return 0;
}

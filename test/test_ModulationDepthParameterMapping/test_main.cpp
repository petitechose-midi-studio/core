#include <cassert>
#include <iostream>

#include "state/modulation/ModulationDepthParameterMapping.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace {

namespace mod = core::state::modulation;
namespace depth = core::state::modulation::depth;

void test_standard_depth_keeps_existing_scale() {
    assert(depth::maximumPercent(depth::Scale::STANDARD) == 100);
    assert(depth::amountQ15ToPercent(32767, depth::Scale::STANDARD) == 100);
    assert(depth::percentToAmountQ15(100, depth::Scale::STANDARD) == 32767);
    assert(depth::amountQ15ToPercent(-32767, depth::Scale::STANDARD) == -100);
}

void test_bipolar_recorded_shape_uses_two_hundred_percent_range() {
    assert(depth::maximumPercent(depth::Scale::RECORDED_SHAPE_BIPOLAR) == 200);
    assert(depth::stepCount(depth::Scale::RECORDED_SHAPE_BIPOLAR) == 401);
    assert(depth::percentToAmountQ15(
        100,
        depth::Scale::RECORDED_SHAPE_BIPOLAR
    ) == 16384);
    assert(depth::amountQ15ToPercent(
        16384,
        depth::Scale::RECORDED_SHAPE_BIPOLAR
    ) == 100);
    assert(depth::percentToAmountQ15(
        200,
        depth::Scale::RECORDED_SHAPE_BIPOLAR
    ) == 32767);
    assert(depth::amountQ15ToPercent(
        32767,
        depth::Scale::RECORDED_SHAPE_BIPOLAR
    ) == 200);
}

void test_normalized_mapping_clamps_at_authored_bounds() {
    assert(depth::amountQ15AtNormalized(
        -1.0f,
        depth::Scale::STANDARD
    ) == -32767);
    assert(depth::amountQ15AtNormalized(
        2.0f,
        depth::Scale::STANDARD
    ) == 32767);
    assert(depth::percentAtNormalized(
        0.5f,
        depth::Scale::RECORDED_SHAPE_BIPOLAR
    ) == 0);
}

void test_scale_is_derived_from_the_canonical_source_curve() {
    mod::ProjectModulationState graph{};
    mod::ProjectCurveArena curves{};
    const mod::ProjectPackedCurvePoint bipolarPoints[]{
        {0U, -32767},
        {192U, 32767},
    };
    mod::RecordedShapeDraft bipolar{};
    bipolar.name = "Gesture";
    bipolar.curve.sourceDurationTicks = 192U;
    bipolar.curve.durationTicks = 192U;
    bipolar.curve.valueDomain = mod::ProjectCurveValueDomain::BIPOLAR;
    bipolar.points = bipolarPoints;
    bipolar.pointCount = 2U;
    const auto source = mod::createRecordedShapeModulator(
        graph,
        curves,
        bipolar
    );
    assert(source.changed());

    mod::ModulationBindingDraft binding{};
    binding.sourceId = source.sourceId;
    binding.destination = {
        mod::ModulationDestinationKind::MACRO_SLOT,
        0U,
        0U,
        0U,
    };
    binding.amountQ15 = 32767;
    const auto assigned = mod::addProjectModulationBinding(graph, binding);
    assert(assigned.changed());
    const auto* live = mod::findProjectModulationBinding(
        graph,
        assigned.bindingId
    );
    assert(live != nullptr);
    assert(depth::scaleFor(graph, curves, *live) ==
           depth::Scale::RECORDED_SHAPE_BIPOLAR);
}

void test_non_bipolar_sources_keep_the_standard_scale() {
    mod::ProjectModulationState graph{};
    mod::ProjectCurveArena curves{};

    mod::ModulatorLfoDraft lfo{};
    const auto lfoSource = mod::createLfoModulator(graph, lfo);
    assert(lfoSource.changed());
    const auto* liveLfo = mod::findProjectModulator(
        graph,
        lfoSource.sourceId
    );
    assert(liveLfo != nullptr);
    assert(depth::scaleFor(*liveLfo, curves) == depth::Scale::STANDARD);

    mod::ModulatorAdsrDraft adsr{};
    const auto adsrSource = mod::createAdsrModulator(graph, adsr);
    assert(adsrSource.changed());
    const auto* liveAdsr = mod::findProjectModulator(
        graph,
        adsrSource.sourceId
    );
    assert(liveAdsr != nullptr);
    assert(depth::scaleFor(*liveAdsr, curves) == depth::Scale::STANDARD);

    const mod::ProjectPackedCurvePoint unipolarPoints[]{
        {0U, 0},
        {192U, 32767},
    };
    mod::RecordedShapeDraft unipolar{};
    unipolar.name = "Positive";
    unipolar.curve.sourceDurationTicks = 192U;
    unipolar.curve.durationTicks = 192U;
    unipolar.curve.valueDomain =
        mod::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
    unipolar.points = unipolarPoints;
    unipolar.pointCount = 2U;
    const auto recorded = mod::createRecordedShapeModulator(
        graph,
        curves,
        unipolar
    );
    assert(recorded.changed());
    const auto* liveRecorded = mod::findProjectModulator(
        graph,
        recorded.sourceId
    );
    assert(liveRecorded != nullptr);
    assert(depth::scaleFor(*liveRecorded, curves) == depth::Scale::STANDARD);
}

}  // namespace

int main() {
    test_standard_depth_keeps_existing_scale();
    test_bipolar_recorded_shape_uses_two_hundred_percent_range();
    test_normalized_mapping_clamps_at_authored_bounds();
    test_scale_is_derived_from_the_canonical_source_curve();
    test_non_bipolar_sources_keep_the_standard_scale();
    std::cout << "ModulationDepthParameterMapping tests passed.\n";
    return 0;
}

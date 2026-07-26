#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>

#include "state/modulation/ProjectRecordedShapeTake.hpp"

namespace {

using core::state::modulation::ProjectCurveOrigin;
using core::state::modulation::ProjectCurveSpec;
using core::state::modulation::ProjectCurveValueDomain;
using core::state::modulation::ProjectPackedCurvePoint;
using core::state::modulation::ProjectRecordedShapeTake;
using core::state::modulation::ProjectRecordedShapeTakePhase;

static_assert(std::is_trivially_copyable_v<ProjectRecordedShapeTake>);

void test_new_take_is_zero_centred_and_no_op_until_moved() {
    ProjectRecordedShapeTake take{};
    assert(take.begin(8U, 0U));
    assert(take.phase == ProjectRecordedShapeTakePhase::RECORDING);
    assert(take.sampleCount == 9U);
    for (uint16_t index = 0U; index < take.sampleCount; ++index) {
        assert(take.values[index] == 0);
    }

    ProjectCurveSpec spec{};
    std::array<ProjectPackedCurvePoint, 8U> points{};
    uint16_t written = 99U;
    assert(!take.buildPackedCurve(spec, points.data(), points.size(), written));
    assert(written == 0U);
    assert(take.touchDelta(0, 2U));
    assert(!take.buildPackedCurve(spec, points.data(), points.size(), written));
    assert(written == 0U);
    std::cout << "[PASS] zero-centred no-op\n";
}

void test_project_phase_places_and_advances_the_write_head() {
    ProjectRecordedShapeTake take{};
    assert(take.begin(8U, 2U));
    assert(take.touchDelta(1000, 0U));
    assert(take.values[2U] == 1000);
    assert(take.values[0U] == 0);
    assert(take.sample(2U));
    assert(take.values[3U] == 1000);
    assert(take.values[4U] == 1000);

    uint16_t positionQ16 = 0U;
    assert(take.writePositionQ16(positionQ16));
    assert(positionQ16 >= 32767U && positionQ16 <= 32768U);
    std::cout << "[PASS] Project-phase write head\n";
}

void test_latest_circular_pass_wins_across_wrap() {
    ProjectRecordedShapeTake take{};
    assert(take.begin(4U, 0U));
    assert(take.touchDelta(1000, 0U));
    assert(take.sample(4U));
    for (uint16_t index = 0U; index < take.sampleCount; ++index) {
        assert(take.values[index] == 1000);
    }

    assert(take.touchDelta(1000, 4U));
    assert(take.sample(8U));
    for (uint16_t index = 0U; index < take.sampleCount; ++index) {
        assert(take.values[index] == 2000);
    }
    assert(take.values[0U] == take.values[take.sampleCount - 1U]);
    std::cout << "[PASS] latest circular pass wins\n";
}

void test_prefill_is_explicit_and_only_overdubbed_after_input() {
    constexpr std::array<ProjectPackedCurvePoint, 3U> seed{
        ProjectPackedCurvePoint{0U, -1000},
        ProjectPackedCurvePoint{2U, 1000},
        ProjectPackedCurvePoint{4U, -1000},
    };
    const ProjectCurveSpec source{
        .sourceDurationTicks = 4U,
        .durationTicks = 4U,
        .windowOffsetTicks = 0U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    ProjectRecordedShapeTake take{};
    assert(take.begin(4U, 0U));
    assert(take.prefill(source, seed.data(), seed.size()));
    assert(take.prefilled);
    assert(take.values[0U] == -1000);
    assert(take.values[1U] == 0);
    assert(take.values[2U] == 1000);
    assert(take.values[3U] == 0);
    assert(take.values[4U] == -1000);

    assert(take.touchDelta(500, 2U));
    assert(take.values[2U] == 1500);
    assert(take.values[0U] == -1000);
    assert(take.sample(3U));
    assert(take.values[3U] == 1500);
    assert(!take.prefill(source, seed.data(), seed.size()));
    std::cout << "[PASS] explicit prefill and overdub\n";
}

void test_prefill_honours_source_window_without_rescaling() {
    std::array<ProjectPackedCurvePoint, 17U> seed{};
    for (uint16_t tick = 0U; tick < seed.size(); ++tick) {
        seed[tick] = ProjectPackedCurvePoint{
            tick,
            static_cast<int16_t>(tick * 100U),
        };
    }
    const ProjectCurveSpec window{
        .sourceDurationTicks = 16U,
        .durationTicks = 4U,
        .windowOffsetTicks = 6U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
        .origin = ProjectCurveOrigin::CONVERTED_MEAN,
    };
    ProjectRecordedShapeTake take{};
    assert(take.begin(4U, 0U));
    assert(take.prefill(window, seed.data(), seed.size()));
    assert(take.values[0U] == 600);
    assert(take.values[1U] == 700);
    assert(take.values[2U] == 800);
    assert(take.values[3U] == 900);
    assert(take.values[4U] == 600);

    auto invalidDomain = window;
    invalidDomain.valueDomain = ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
    ProjectRecordedShapeTake invalidDomainTake{};
    assert(invalidDomainTake.begin(4U, 0U));
    assert(!invalidDomainTake.prefill(
        invalidDomain,
        seed.data(),
        seed.size()
    ));

    auto invalidWindow = window;
    invalidWindow.windowOffsetTicks = 17U;
    ProjectRecordedShapeTake invalidWindowTake{};
    assert(invalidWindowTake.begin(4U, 0U));
    assert(!invalidWindowTake.prefill(
        invalidWindow,
        seed.data(),
        seed.size()
    ));
    std::cout << "[PASS] canonical source window prefill\n";
}

int16_t referencePrefill(
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount,
    uint16_t tick
) {
    if (pointCount == 1U || tick <= points[0U].tick) {
        return points[0U].value;
    }
    uint16_t right = 1U;
    while (right < pointCount && points[right].tick <= tick) ++right;
    if (right >= pointCount) return points[pointCount - 1U].value;
    const auto& left = points[right - 1U];
    const auto& next = points[right];
    if (next.tick == left.tick) return next.value;
    const int64_t delta = static_cast<int64_t>(next.value) - left.value;
    int64_t numerator = delta * (tick - left.tick);
    const uint32_t span = next.tick - left.tick;
    numerator += numerator >= 0
        ? static_cast<int64_t>(span / 2U)
        : -static_cast<int64_t>(span / 2U);
    return static_cast<int16_t>(
        static_cast<int64_t>(left.value) + numerator / span
    );
}

void test_prefill_cursor_matches_reference_across_wrap_and_duplicates() {
    constexpr std::array<ProjectPackedCurvePoint, 6U> seed{
        ProjectPackedCurvePoint{0U, -1000},
        ProjectPackedCurvePoint{0U, 1111},
        ProjectPackedCurvePoint{3U, 3000},
        ProjectPackedCurvePoint{10U, 10000},
        ProjectPackedCurvePoint{10U, 12000},
        ProjectPackedCurvePoint{16U, 16000},
    };
    const ProjectCurveSpec window{
        .sourceDurationTicks = 16U,
        .durationTicks = 12U,
        .windowOffsetTicks = 10U,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
    };
    ProjectRecordedShapeTake take{};
    assert(take.begin(window.durationTicks, 0U));
    assert(take.prefill(window, seed.data(), seed.size()));
    for (uint16_t sample = 0U; sample < take.sampleCount; ++sample) {
        const uint16_t targetTick = sample;
        const uint16_t sourceTick = static_cast<uint16_t>(
            (window.windowOffsetTicks +
             targetTick % window.durationTicks) %
            window.sourceDurationTicks
        );
        assert(take.values[sample] == referencePrefill(
            seed.data(),
            static_cast<uint16_t>(seed.size()),
            sourceTick
        ));
    }
    assert(take.values[0U] == 12000);
    assert(take.values[6U] == -1000);
    assert(take.values[take.sampleCount - 1U] == take.values[0U]);
    assert(take.prefillSearchSteps <=
           take.sampleCount + 3U * seed.size());
    std::cout << "[PASS] linear prefill cursor, wrap, and duplicate ticks\n";
}

void test_preview_sampling_is_constant_time_and_interpolated() {
    ProjectRecordedShapeTake idle{};
    int16_t preview = 123;
    assert(!idle.samplePreviewValue(0U, preview));

    ProjectRecordedShapeTake take{};
    assert(take.begin(4U, 0U));
    take.values[0U] = -1000;
    take.values[1U] = 1000;
    take.values[2U] = 3000;
    take.values[3U] = 1000;
    take.values[4U] = -1000;
    assert(take.samplePreviewValue(0U, preview));
    assert(preview == -1000);
    assert(take.samplePreviewValue(8192U, preview));
    assert(preview >= -1 && preview <= 1);
    assert(take.samplePreviewValue(16384U, preview));
    assert(preview >= 999 && preview <= 1001);
    assert(take.samplePreviewValue(65535U, preview));
    assert(preview == -1000);
    std::cout << "[PASS] O(1) preview interpolation and endpoints\n";
}

std::array<int16_t, 5U> captureIgnoringConceptualBase(int conceptualBase) {
    (void)conceptualBase;
    ProjectRecordedShapeTake take{};
    assert(take.begin(4U, 0U));
    assert(take.touchDelta(30000, 0U));
    assert(take.touchDelta(10000, 0U));
    assert(take.currentValue == ProjectRecordedShapeTake::SOURCE_MAX);
    assert(take.touchDelta(-70000, 1U));
    assert(take.currentValue == ProjectRecordedShapeTake::SOURCE_MIN);
    std::array<int16_t, 5U> result{};
    for (uint16_t index = 0U; index < take.sampleCount; ++index) {
        result[index] = take.values[index];
        assert(result[index] >= ProjectRecordedShapeTake::SOURCE_MIN);
        assert(result[index] <= ProjectRecordedShapeTake::SOURCE_MAX);
        assert(result[index] != std::numeric_limits<int16_t>::min());
    }
    return result;
}

void test_relative_deltas_do_not_depend_on_destination_base() {
    const auto baseOnePercent = captureIgnoringConceptualBase(1);
    const auto baseFiftyPercent = captureIgnoringConceptualBase(50);
    const auto baseNinetyNinePercent = captureIgnoringConceptualBase(99);
    assert(baseOnePercent == baseFiftyPercent);
    assert(baseFiftyPercent == baseNinetyNinePercent);
    assert(baseOnePercent[0U] == ProjectRecordedShapeTake::SOURCE_MAX);
    assert(baseOnePercent[1U] == ProjectRecordedShapeTake::SOURCE_MIN);
    std::cout << "[PASS] base-independent signed delta integration\n";
}

void test_build_is_native_bipolar_and_simplifies_linear_segments() {
    ProjectRecordedShapeTake take{};
    assert(take.begin(4U, 0U));
    assert(take.touchDelta(1000, 0U));
    assert(take.touchDelta(3000, 2U));
    assert(take.touchDelta(-3000, 4U));
    assert(take.values[0U] == 1000);
    assert(take.values[1U] == 2500);
    assert(take.values[2U] == 4000);
    assert(take.values[3U] == 2500);
    assert(take.values[4U] == 1000);

    ProjectCurveSpec spec{};
    std::array<ProjectPackedCurvePoint, 3U> points{};
    uint16_t written = 0U;
    assert(take.buildPackedCurve(spec, points.data(), points.size(), written));
    assert(written == 3U);
    assert(points[0U].tick == 0U && points[0U].value == 1000);
    assert(points[1U].tick == 2U && points[1U].value == 4000);
    assert(points[2U].tick == 4U && points[2U].value == 1000);
    assert(spec.sourceDurationTicks == 4U);
    assert(spec.durationTicks == 4U);
    assert(spec.windowOffsetTicks == 0U);
    assert(spec.valueDomain == ProjectCurveValueDomain::BIPOLAR);
    assert(spec.origin == ProjectCurveOrigin::NATIVE);
    std::cout << "[PASS] bipolar packed build and simplification\n";
}

void test_build_capacity_failure_is_explicit_and_grid_is_bounded() {
    ProjectRecordedShapeTake take{};
    assert(take.begin(4U, 0U));
    assert(take.touchDelta(1000, 0U));
    assert(take.touchDelta(3000, 2U));
    assert(take.touchDelta(-3000, 4U));

    ProjectCurveSpec spec{};
    std::array<ProjectPackedCurvePoint, 2U> tooSmall{};
    uint16_t written = 17U;
    assert(!take.buildPackedCurve(
        spec,
        tooSmall.data(),
        tooSmall.size(),
        written
    ));
    assert(written == 0U);

    ProjectRecordedShapeTake longTake{};
    assert(longTake.begin(std::numeric_limits<uint16_t>::max(), 123456U));
    assert(longTake.sampleCount == ProjectRecordedShapeTake::SAMPLE_CAPACITY);
    assert(longTake.reduced);
    std::cout << "[PASS] explicit output and grid capacity bounds\n";
}

}  // namespace

int main() {
    test_new_take_is_zero_centred_and_no_op_until_moved();
    test_project_phase_places_and_advances_the_write_head();
    test_latest_circular_pass_wins_across_wrap();
    test_prefill_is_explicit_and_only_overdubbed_after_input();
    test_prefill_honours_source_window_without_rescaling();
    test_prefill_cursor_matches_reference_across_wrap_and_duplicates();
    test_preview_sampling_is_constant_time_and_interpolated();
    test_relative_deltas_do_not_depend_on_destination_base();
    test_build_is_native_bipolar_and_simplifies_linear_segments();
    test_build_capacity_failure_is_explicit_and_grid_is_bounded();
    std::cout << "\nAll ProjectRecordedShapeTake tests passed.\n";
    return 0;
}

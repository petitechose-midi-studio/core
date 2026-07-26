#include <array>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <limits>

#include "handler/common/ProjectRecordedShapeCaptureWorkflow.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace {

namespace macro = core::state::macro;
namespace modulation = core::state::modulation;
using core::handler::ProjectRecordedShapeCaptureWorkflow;

struct Probe {
    uint32_t mutated = 0U;
    uint32_t published = 0U;
    uint32_t cleared = 0U;
    modulation::ProjectRecordedShapeAuditionDescriptor last{};
};

void markMutated(void* context) {
    ++static_cast<Probe*>(context)->mutated;
}

void publishAudition(
    void* context,
    const modulation::ProjectRecordedShapeAuditionDescriptor& descriptor
) {
    auto& probe = *static_cast<Probe*>(context);
    ++probe.published;
    probe.last = descriptor;
}

void clearAudition(void* context) {
    ++static_cast<Probe*>(context)->cleared;
}

struct Fixture {
    macro::MacroPagesState pages{};
    macro::MacroUiState ui{};
    core::state::StatusBarState status{};
    macro::MacroHistoryService history{};
    Probe probe{};
    ProjectRecordedShapeCaptureWorkflow workflow{
        ProjectRecordedShapeCaptureWorkflow::StateRefs{
            pages,
            ui,
            status,
            history,
        },
        ProjectRecordedShapeCaptureWorkflow::Operations{
            .context = &probe,
            .auditionContext = &probe,
            .markProjectMutated = markMutated,
            .publishAudition = publishAudition,
            .clearAudition = clearAudition,
        },
    };
};

void publishTime(Fixture& fixture,
                 uint32_t tick,
                 uint32_t nowMs,
                 uint32_t generation = 7U,
                 bool playing = true) {
    modulation::publishProjectControlTimeTelemetry(
        fixture.pages.control.timeTelemetry,
        modulation::ProjectControlTimeSnapshot{
            .musicalTick = tick,
            .monotonicMs = nowMs,
            .transportGeneration = generation,
            .playing = playing,
        }
    );
}

modulation::ProjectModulationResult createSeedShape(
    Fixture& fixture,
    const char* name = "Seed"
) {
    constexpr std::array<modulation::ProjectPackedCurvePoint, 3U> points{
        modulation::ProjectPackedCurvePoint{0U, -4000},
        modulation::ProjectPackedCurvePoint{4U, 4000},
        modulation::ProjectPackedCurvePoint{8U, -4000},
    };
    return fixture.history.createUnassignedRecordedShape(
        fixture.pages,
        modulation::RecordedShapeDraft{
            .name = name,
            .curve = modulation::ProjectCurveSpec{
                .sourceDurationTicks = 8U,
                .durationTicks = 8U,
                .windowOffsetTicks = 0U,
                .valueDomain =
                    modulation::ProjectCurveValueDomain::BIPOLAR,
            },
            .points = points.data(),
            .pointCount = static_cast<uint16_t>(points.size()),
        }
    );
}

void test_create_unassigned_is_one_history_backed_source() {
    Fixture fixture;
    const uint32_t beforeRevision = fixture.workflow.revision();
    assert(fixture.workflow.armCreateUnassigned(1000U, 8U, "Motion 1"));
    assert(fixture.workflow.active());
    assert(fixture.workflow.revision() != beforeRevision);
    assert(fixture.workflow.touchDeltaQ15(12000, 1000U));
    const auto committed = fixture.workflow.release(1000U);
    assert(committed.changed());
    assert(fixture.workflow.status() ==
           modulation::ProjectRecordedShapeCaptureStatus::COMMITTED);
    assert(!fixture.workflow.active());
    assert(fixture.pages.control.authored.modulation.sourceCount == 1U);
    assert(fixture.pages.control.authored.modulation.outputBindingCount == 0U);
    const auto& source =
        fixture.pages.control.authored.modulation.sources[0U];
    assert(source.id == committed.sourceId);
    assert(source.kind == modulation::ModulatorKind::RECORDED_SHAPE);
    assert(fixture.pages.control.authored.curves.recordCount == 1U);
    assert(fixture.probe.mutated == 1U);
    assert(fixture.probe.published == 0U);
    assert(fixture.probe.cleared == 0U);
    assert(fixture.history.undoCount() == 1U);
    std::cout << "[PASS] detached Recorded Shape capture\n";
}

void test_create_assigned_applies_sparse_macro_topology_at_release() {
    Fixture fixture;
    constexpr macro::MacroAutomationSlotAddress address{
        .track = 1U,
        .page = 0U,
        .macro = 5U,
    };
    assert(!fixture.pages.isTrackEnabled(address.track));
    assert(fixture.workflow.armCreateAssigned(2000U, 8U, address));
    assert(!fixture.pages.isTrackEnabled(address.track));
    assert(fixture.workflow.touchDeltaQ15(32767, 2000U));
    assert(fixture.probe.published == 1U);
    assert(fixture.probe.last.mode ==
           modulation::ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED);
    assert(!modulation::valid(fixture.probe.last.sourceId));
    assert(fixture.probe.last.destination ==
           modulation::projectControlDestination(address));
    assert(fixture.probe.last.amountQ15 ==
           ProjectRecordedShapeCaptureWorkflow::DEPTH_100_PERCENT_Q15);

    const auto committed = fixture.workflow.release(2000U);
    assert(committed.changed());
    assert(fixture.pages.isTrackEnabled(address.track));
    assert(fixture.pages.pageData(address.track, address.page)
               .isMacroActive(address.macro));
    assert(fixture.pages.control.authored.modulation.outputBindingCount == 1U);
    const auto& binding =
        fixture.pages.control.authored.modulation.outputBindings[0U];
    assert(binding.sourceId == committed.sourceId);
    assert(binding.destination ==
           modulation::projectControlDestination(address));
    assert(binding.amountQ15 == 16384);
    assert(fixture.probe.cleared == 1U);
    assert(fixture.probe.mutated == 1U);
    std::cout << "[PASS] assigned capture and deferred topology\n";
}

void test_replace_prefills_and_copy_on_write_is_exact() {
    Fixture fixture;
    const auto seed = createSeedShape(fixture);
    assert(seed.changed());
    const auto duplicate = fixture.history.duplicateProjectModulator(
        fixture.pages,
        seed.sourceId,
        "Shared"
    );
    assert(duplicate.changed());
    const auto* originalBefore = modulation::findProjectModulator(
        fixture.pages.control.authored.modulation,
        seed.sourceId
    );
    const auto* duplicateBefore = modulation::findProjectModulator(
        fixture.pages.control.authored.modulation,
        duplicate.sourceId
    );
    assert(originalBefore != nullptr && duplicateBefore != nullptr);
    const auto sharedCurve = originalBefore->parameters.recordedCurveId;
    assert(duplicateBefore->parameters.recordedCurveId == sharedCurve);
    assert(modulation::findProjectCurve(
               fixture.pages.control.authored.curves,
               sharedCurve)->referenceCount == 2U);

    assert(fixture.workflow.armReplaceExisting(3000U, seed.sourceId));
    const auto* preview = fixture.workflow.previewTake();
    assert(preview != nullptr && preview->prefilled);
    assert(fixture.workflow.touchDeltaQ15(2000, 3000U));
    assert(fixture.probe.last.mode ==
           modulation::ProjectRecordedShapeCaptureMode::REPLACE_EXISTING);
    assert(fixture.probe.last.sourceId == seed.sourceId);
    const auto replaced = fixture.workflow.release(3000U);
    assert(replaced.changed());

    const auto* originalAfter = modulation::findProjectModulator(
        fixture.pages.control.authored.modulation,
        seed.sourceId
    );
    const auto* duplicateAfter = modulation::findProjectModulator(
        fixture.pages.control.authored.modulation,
        duplicate.sourceId
    );
    assert(originalAfter != nullptr && duplicateAfter != nullptr);
    assert(originalAfter->parameters.recordedCurveId != sharedCurve);
    assert(duplicateAfter->parameters.recordedCurveId == sharedCurve);
    assert(modulation::findProjectCurve(
               fixture.pages.control.authored.curves,
               sharedCurve)->referenceCount == 1U);
    assert(modulation::findProjectCurve(
               fixture.pages.control.authored.curves,
               originalAfter->parameters.recordedCurveId)->referenceCount ==
           1U);
    assert(fixture.probe.cleared == 1U);
    assert(fixture.probe.mutated == 1U);
    std::cout << "[PASS] exact prefill and replace COW\n";
}

void test_project_phase_wrap_and_transport_invalidation() {
    Fixture fixture;
    fixture.pages.control.runtime.initialized = true;
    fixture.pages.control.runtime.activationMusicalTick = 100U;
    publishTime(fixture, 115U, 4000U);
    constexpr macro::MacroAutomationSlotAddress address{
        .track = 0U,
        .page = 0U,
        .macro = 0U,
    };
    assert(fixture.workflow.armCreateAssigned(4000U, 8U, address));
    assert(fixture.workflow.touchDeltaQ15(1000, 4000U));
    uint16_t position = 0U;
    assert(fixture.workflow.previewTake()->writePositionQ16(position));
    assert(position > 57000U);

    publishTime(fixture, 117U, 4010U);
    assert(fixture.workflow.sample(4010U));
    assert(fixture.workflow.previewTake()->writePositionQ16(position));
    assert(position > 7000U && position < 9000U);

    publishTime(fixture, 118U, 4020U, 8U);
    assert(!fixture.workflow.sample(4020U));
    assert(!fixture.workflow.active());
    assert(fixture.workflow.status() ==
           modulation::ProjectRecordedShapeCaptureStatus::INVALIDATED);
    assert(fixture.pages.control.authored.modulation.sourceCount == 0U);
    assert(fixture.probe.cleared == 1U);
    assert(fixture.probe.mutated == 0U);
    std::cout << "[PASS] Project phase wrap and transport fail-closed\n";
}

void test_untouched_release_and_cancel_publish_nothing() {
    Fixture fixture;
    assert(fixture.workflow.armCreateUnassigned(5000U, 8U));
    auto* retainedScratch = fixture.ui.recordedShapeCapture.take.get();
    const auto noOp = fixture.workflow.release(5000U);
    assert(noOp.status == modulation::ProjectModulationStatus::NO_CHANGE);
    assert(fixture.workflow.status() ==
           modulation::ProjectRecordedShapeCaptureStatus::NO_CHANGE);
    assert(fixture.pages.control.authored.modulation.sourceCount == 0U);
    assert(fixture.history.undoCount() == 0U);
    assert(fixture.probe.mutated == 0U);

    assert(fixture.workflow.armCreateUnassigned(5010U, 8U));
    assert(fixture.ui.recordedShapeCapture.take.get() == retainedScratch);
    assert(fixture.workflow.touchDeltaQ15(1000, 5010U));
    assert(fixture.workflow.cancel());
    assert(fixture.workflow.status() ==
           modulation::ProjectRecordedShapeCaptureStatus::CANCELLED);
    assert(fixture.pages.control.authored.modulation.sourceCount == 0U);
    assert(fixture.history.undoCount() == 0U);
    assert(fixture.probe.mutated == 0U);
    std::cout << "[PASS] no-op/cancel retain scratch only\n";
}

void test_concurrent_authored_or_curve_mutation_rolls_back_audition() {
    {
        Fixture assigned;
        constexpr macro::MacroAutomationSlotAddress address{
            .track = 0U,
            .page = 0U,
            .macro = 0U,
        };
        assert(assigned.workflow.armCreateAssigned(6000U, 8U, address));
        assert(assigned.workflow.touchDeltaQ15(1000, 6000U));
        assigned.pages.control.markAuthoredMutation();
        const auto rejected = assigned.workflow.release(6000U);
        assert(rejected.status ==
               modulation::ProjectModulationStatus::INVARIANT_VIOLATION);
        assert(assigned.pages.control.authored.modulation.sourceCount == 0U);
        assert(assigned.probe.cleared == 1U);
        assert(assigned.probe.mutated == 0U);
    }

    {
        Fixture replaced;
        const auto seed = createSeedShape(replaced);
        assert(seed.changed());
        assert(replaced.workflow.armReplaceExisting(6100U, seed.sourceId));
        assert(replaced.ui.recordedShapeCapture
                   .expectedCurvePointHashValid);
        assert(replaced.workflow.touchDeltaQ15(1000, 6100U));
        const auto* source = modulation::findProjectModulator(
            replaced.pages.control.authored.modulation,
            seed.sourceId
        );
        assert(source != nullptr);
        const auto* record = modulation::findProjectCurve(
            replaced.pages.control.authored.curves,
            source->parameters.recordedCurveId
        );
        assert(record != nullptr);
        replaced.pages.control.authored.curves.points[record->pointOffset]
            .value++;
        const auto curveRejected = replaced.workflow.release(6100U);
        assert(curveRejected.status ==
               modulation::ProjectModulationStatus::INVARIANT_VIOLATION);
        assert(replaced.probe.mutated == 0U);
        assert(replaced.probe.cleared == 1U);
    }
    std::cout << "[PASS] authored and raw curve concurrency guards\n";
}

void test_raw_encoder_uses_cumulative_mapping_without_rounding_drift() {
    Fixture fixture;
    assert(fixture.workflow.armCreateUnassigned(7000U, 8U));
    assert(fixture.workflow.configureRawEncoderOrigin(1000));
    for (int32_t position = 1001; position <= 1600; ++position) {
        assert(fixture.workflow.touchRawEncoder(position, 7000U));
    }
    int16_t value = 0;
    assert(fixture.workflow.currentSourceValueQ15(value));
    assert(value == modulation::ProjectRecordedShapeTake::SOURCE_MAX);
    assert(fixture.ui.recordedShapeCapture.rawEncoderAppliedQ15 == 32767);
    assert(fixture.workflow.touchRawEncoder(1000, 7000U));
    assert(fixture.workflow.currentSourceValueQ15(value));
    assert(value == 0);
    assert(fixture.workflow.cancel());
    std::cout << "[PASS] cumulative RAW encoder mapping\n";
}

void test_macro_ui_lifecycle_reset_clears_live_audition() {
    Fixture fixture;
    constexpr macro::MacroAutomationSlotAddress address{
        .track = 0U,
        .page = 0U,
        .macro = 0U,
    };
    assert(fixture.workflow.armCreateAssigned(7500U, 8U, address));
    assert(fixture.workflow.touchDeltaQ15(1000, 7500U));
    assert(fixture.probe.published == 1U);
    fixture.ui.resetInteraction();
    assert(!fixture.workflow.active());
    assert(fixture.workflow.status() ==
           modulation::ProjectRecordedShapeCaptureStatus::IDLE);
    assert(fixture.probe.cleared == 1U);
    assert(fixture.pages.control.authored.modulation.sourceCount == 0U);
    assert(fixture.probe.mutated == 0U);
    std::cout << "[PASS] lifecycle reset clears audition\n";
}

void test_capacity_failure_is_visible_and_atomic() {
    Fixture fixture;
    for (uint16_t index = 0U;
         index < modulation::PROJECT_MODULATOR_CAPACITY;
         ++index) {
        char name[modulation::PROJECT_MODULATOR_NAME_CAPACITY]{};
        std::snprintf(name, sizeof(name), "L%u", index);
        assert(modulation::createLfoModulator(
            fixture.pages.control.authored.modulation,
            modulation::ModulatorLfoDraft{.name = name}
        ).changed());
    }
    assert(!fixture.workflow.armCreateUnassigned(8000U, 8U));
    assert(!fixture.workflow.active());
    assert(fixture.workflow.lastProjectStatus() ==
           modulation::ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED);
    assert(fixture.history.undoCount() == 0U);
    assert(fixture.probe.mutated == 0U);
    std::cout << "[PASS] source-capacity preflight\n";
}

void test_noncanonical_binding_amount_is_rejected_before_arm() {
    Fixture fixture;
    constexpr macro::MacroAutomationSlotAddress address{
        .track = 0U,
        .page = 0U,
        .macro = 0U,
    };
    assert(!fixture.workflow.armCreateAssigned(
        8100U,
        8U,
        address,
        std::numeric_limits<int16_t>::min()
    ));
    assert(!fixture.workflow.active());
    assert(fixture.workflow.lastProjectStatus() ==
           modulation::ProjectModulationStatus::INVALID_ARGUMENT);
    assert(fixture.pages.control.authored.modulation.sourceCount == 0U);
    assert(fixture.pages.control.authored.modulation.outputBindingCount == 0U);
    assert(fixture.history.undoCount() == 0U);
    assert(fixture.probe.mutated == 0U);
    std::cout << "[PASS] INT16_MIN binding amount rejected atomically\n";
}

}  // namespace

int main() {
    test_create_unassigned_is_one_history_backed_source();
    test_create_assigned_applies_sparse_macro_topology_at_release();
    test_replace_prefills_and_copy_on_write_is_exact();
    test_project_phase_wrap_and_transport_invalidation();
    test_untouched_release_and_cancel_publish_nothing();
    test_concurrent_authored_or_curve_mutation_rolls_back_audition();
    test_raw_encoder_uses_cumulative_mapping_without_rounding_drift();
    test_macro_ui_lifecycle_reset_clears_live_audition();
    test_capacity_failure_is_visible_and_atomic();
    test_noncanonical_binding_amount_is_rejected_before_arm();
    std::cout << "\nAll ProjectRecordedShapeCaptureWorkflow tests passed.\n";
    return 0;
}

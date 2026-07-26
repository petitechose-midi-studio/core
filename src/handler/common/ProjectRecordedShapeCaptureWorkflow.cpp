#include "handler/common/ProjectRecordedShapeCaptureWorkflow.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "handler/common/ProjectRecordedShapeCaptureWorkflowInternals.hpp"
#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::handler {

using namespace core::state::modulation;
using core::state::macro::MacroAutomationSlotAddress;
using core::state::macro::MacroDestinationActivationPlan;
using namespace recorded_shape_capture_detail;

FLASHMEM ProjectRecordedShapeCaptureWorkflow::ProjectRecordedShapeCaptureWorkflow(
    StateRefs state
)
    : ProjectRecordedShapeCaptureWorkflow(state, Operations{}) {}

FLASHMEM ProjectRecordedShapeCaptureWorkflow::ProjectRecordedShapeCaptureWorkflow(
    StateRefs state,
    Operations operations
)
    : pages_(&state.pages)
    , macro_ui_(&state.macroUi)
    , status_bar_(&state.statusBar)
    , history_(&state.history)
    , operations_(operations) {}

FLASHMEM ProjectRecordedShapeCaptureWorkflow
ProjectRecordedShapeCaptureWorkflow::fromCoreState(
    core::state::CoreState& state,
    PublishProjectRecordedShapeAuditionFn publishAudition,
    ClearProjectRecordedShapeAuditionFn clearAudition,
    void* auditionContext
) {
    return ProjectRecordedShapeCaptureWorkflow{
        StateRefs{state.pages, state.macroUi, state.statusBar,
                  state.macroHistory},
        Operations{
            .context = &state,
            .auditionContext = auditionContext != nullptr
                ? auditionContext
                : &state,
            .markProjectMutated = markProjectMutatedFromCoreState,
            .publishAudition = publishAudition,
            .clearAudition = clearAudition,
        },
    };
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::beginTake_(
    uint32_t nowMs,
    uint16_t durationTicks
) const {
    auto& session = macro_ui_->recordedShapeCapture;
    if (!session.ensureScratch()) return false;

    const auto time = extrapolateProjectControlTime(
        pages_->control.timeTelemetry,
        nowMs
    );
    uint32_t projectPhase = 0U;
    const bool projectClock = pages_->control.timeTelemetry.revision > 0U &&
        pages_->control.runtime.initialized && time.playing &&
        time.musicalTick >= pages_->control.runtime.activationMusicalTick;
    session.clock = projectClock
        ? ProjectRecordedShapeCaptureClock::PROJECT_TRANSPORT
        : ProjectRecordedShapeCaptureClock::MONOTONIC_FALLBACK;
    session.startedAtMs = nowMs;
    session.startedMusicalTick = time.musicalTick;
    session.transportGeneration = time.transportGeneration;
    session.activationMusicalTick =
        pages_->control.runtime.activationMusicalTick;
    if (projectClock) {
        projectPhase = time.musicalTick - session.activationMusicalTick;
    }
    session.durationTicks = durationTicks;
    return session.take->begin(durationTicks, projectPhase);
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::armCreate_(
    uint32_t nowMs,
    uint16_t durationTicks,
    ProjectRecordedShapeCaptureMode mode,
    const MacroAutomationSlotAddress* address,
    int16_t amountQ15,
    const char* name,
    uint8_t accent,
    bool enabled
) const {
    auto& session = macro_ui_->recordedShapeCapture;
    auto& control = pages_->control;
    auto& graph = control.authored.modulation;
    auto& arena = control.authored.curves;
    if (session.active()) return false;
    if (durationTicks == 0U || name == nullptr ||
        name[0] == '\0' ||
        macro_ui_->automationTake.phase !=
            core::state::macro::MacroAutomationTakePhase::IDLE ||
        history_->hasPendingModulatorAuditionTransaction(*pages_)) {
        session.endSession(
            ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            ProjectModulationStatus::INVALID_ARGUMENT
        );
        return false;
    }
    if (graph.sourceCount >= PROJECT_MODULATOR_CAPACITY) {
        session.endSession(
            ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            ProjectModulationStatus::SOURCE_CAPACITY_EXCEEDED
        );
        return false;
    }
    if (graph.nextSourceId == 0U || arena.nextCurveId == 0U) {
        session.endSession(
            ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            ProjectModulationStatus::ID_EXHAUSTED
        );
        return false;
    }
    if (arena.recordCount >= PROJECT_CURVE_LIVE_CAPACITY ||
        arena.recordCount >= PROJECT_CURVE_RECORD_CAPACITY) {
        session.endSession(
            ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            ProjectModulationStatus::CURVE_RECORD_CAPACITY_EXCEEDED
        );
        return false;
    }
    if (arena.pointCount > PROJECT_CURVE_POINT_CAPACITY - 2U) {
        session.endSession(
            ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED
        );
        return false;
    }

    MacroDestinationActivationPlan activation{};
    if (mode == ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED) {
        ProjectModulationStatus bindingPreflight =
            ProjectModulationStatus::OK;
        if (address == nullptr ||
            amountQ15 == std::numeric_limits<int16_t>::min()) {
            bindingPreflight = ProjectModulationStatus::INVALID_ARGUMENT;
        } else if (graph.outputBindingCount >=
                       PROJECT_MODULATION_BINDING_CAPACITY) {
            bindingPreflight =
                ProjectModulationStatus::BINDING_CAPACITY_EXCEEDED;
        } else if (graph.nextBindingId == 0U) {
            bindingPreflight = ProjectModulationStatus::ID_EXHAUSTED;
        }
        if (bindingPreflight != ProjectModulationStatus::OK) {
            session.endSession(
                ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
                bindingPreflight
            );
            return false;
        }
        activation = core::state::macro::MacroWorkflow::
            planDestinationActivation(*pages_, *address);
        if (!activation.valid) {
            session.endSession(
                ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
                ProjectModulationStatus::INVALID_ARGUMENT
            );
            return false;
        }
    } else if (mode !=
                   ProjectRecordedShapeCaptureMode::CREATE_UNASSIGNED ||
               address != nullptr) {
        return false;
    }

    session.mode = mode;
    session.address = address != nullptr ? *address :
        MacroAutomationSlotAddress{};
    session.destinationPlan = activation;
    session.destination = address != nullptr
        ? core::state::modulation::projectControlDestination(*address)
        : ModulationDestination{};
    session.amountQ15 = amountQ15;
    session.accent = accent;
    session.enabled = enabled;
    session.authoredRevision = control.authoredRevision;
    session.auditionContext = operations_.auditionContext != nullptr
        ? operations_.auditionContext
        : operations_.context;
    session.publishAudition = operations_.publishAudition;
    session.clearAudition = operations_.clearAudition;
    session.sourceName.fill('\0');
    std::strncpy(
        session.sourceName.data(),
        name,
        session.sourceName.size() - 1U
    );
    if (!beginTake_(nowMs, durationTicks)) {
        session.endSession(
            ProjectRecordedShapeCaptureStatus::SCRATCH_UNAVAILABLE,
            ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED
        );
        return false;
    }
    session.lastProjectStatus = ProjectModulationStatus::NO_CHANGE;
    session.publishStatus(
        session.take->reduced
            ? ProjectRecordedShapeCaptureStatus::REDUCED
            : ProjectRecordedShapeCaptureStatus::ARMED
    );
    return true;
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::armCreateUnassigned(
    uint32_t nowMs,
    uint16_t durationTicks,
    const char* name,
    uint8_t accent,
    bool enabled
) const {
    return armCreate_(
        nowMs,
        durationTicks,
        ProjectRecordedShapeCaptureMode::CREATE_UNASSIGNED,
        nullptr,
        0,
        name,
        accent,
        enabled
    );
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::armCreateAssigned(
    uint32_t nowMs,
    uint16_t durationTicks,
    const MacroAutomationSlotAddress& address,
    int16_t amountQ15,
    const char* name,
    uint8_t accent,
    bool enabled
) const {
    return armCreate_(
        nowMs,
        durationTicks,
        ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED,
        &address,
        amountQ15,
        name,
        accent,
        enabled
    );
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::armReplaceExisting(
    uint32_t nowMs,
    ModulatorId sourceId
) const {
    auto& session = macro_ui_->recordedShapeCapture;
    auto& control = pages_->control;
    const auto& graph = control.authored.modulation;
    const auto& arena = control.authored.curves;
    if (session.active()) return false;
    if (!valid(sourceId) ||
        macro_ui_->automationTake.phase !=
            core::state::macro::MacroAutomationTakePhase::IDLE ||
        history_->hasPendingModulatorAuditionTransaction(*pages_)) {
        session.endSession(
            ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            ProjectModulationStatus::INVALID_ARGUMENT
        );
        return false;
    }
    const auto* source = findProjectModulator(graph, sourceId);
    if (source == nullptr || source->kind != ModulatorKind::RECORDED_SHAPE) {
        session.endSession(
            ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            source == nullptr ? ProjectModulationStatus::INVALID_ID
                              : ProjectModulationStatus::INVALID_ARGUMENT
        );
        return false;
    }
    const auto* record = findProjectCurve(
        arena,
        source->parameters.recordedCurveId
    );
    const auto* points = record != nullptr
        ? curvePoints(arena, *record)
        : nullptr;
    if (record == nullptr || points == nullptr ||
        record->durationTicks == 0U ||
        record->pointCount >
            core::state::macro::RECORDED_SHAPE_HISTORY_POINT_CAPACITY) {
        session.endSession(
            ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            record != nullptr && record->pointCount >
                    core::state::macro::RECORDED_SHAPE_HISTORY_POINT_CAPACITY
                ? ProjectModulationStatus::HISTORY_CAPACITY_EXCEEDED
                : ProjectModulationStatus::INVARIANT_VIOLATION
        );
        return false;
    }

    session.mode = ProjectRecordedShapeCaptureMode::REPLACE_EXISTING;
    session.sourceId = sourceId;
    session.expectedSource = *source;
    session.expectedCurve = *record;
    session.expectedSourceIndex = static_cast<uint16_t>(
        source - graph.sources.data()
    );
    session.expectedCurveIndex = static_cast<uint16_t>(
        record - arena.records.data()
    );
    session.expectedCurvePointHashValid = pointHash(
        points,
        record->pointCount,
        session.expectedCurvePointHash
    );
    if (!session.expectedCurvePointHashValid) {
        session.endSession(
            ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            ProjectModulationStatus::INVARIANT_VIOLATION
        );
        return false;
    }
    session.authoredRevision = control.authoredRevision;
    session.auditionContext = operations_.auditionContext != nullptr
        ? operations_.auditionContext
        : operations_.context;
    session.publishAudition = operations_.publishAudition;
    session.clearAudition = operations_.clearAudition;
    session.sourceName = source->name;
    session.accent = source->accent;
    session.enabled =
        (source->flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
    if (!beginTake_(nowMs, record->durationTicks) ||
        !session.take->prefill(
            curveSpec(*record),
            points,
            record->pointCount
        )) {
        session.endSession(
            ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            ProjectModulationStatus::INVARIANT_VIOLATION
        );
        return false;
    }
    session.lastProjectStatus = ProjectModulationStatus::NO_CHANGE;
    session.publishStatus(
        session.take->reduced
            ? ProjectRecordedShapeCaptureStatus::REDUCED
            : ProjectRecordedShapeCaptureStatus::ARMED
    );
    return true;
}

FLASHMEM ProjectRecordedShapeCaptureWorkflow::ElapsedTime
ProjectRecordedShapeCaptureWorkflow::elapsed_(uint32_t nowMs) const {
    const auto& session = macro_ui_->recordedShapeCapture;
    if (!session.active()) return {};
    if (session.clock == ProjectRecordedShapeCaptureClock::PROJECT_TRANSPORT) {
        const auto time = extrapolateProjectControlTime(
            pages_->control.timeTelemetry,
            nowMs
        );
        if (pages_->control.timeTelemetry.revision == 0U || !time.playing ||
            !pages_->control.runtime.initialized ||
            pages_->control.runtime.activationMusicalTick !=
                session.activationMusicalTick ||
            time.transportGeneration != session.transportGeneration ||
            time.musicalTick < session.startedMusicalTick) {
            return {};
        }
        return {
            .ticks = time.musicalTick - session.startedMusicalTick,
            .valid = true,
        };
    }
    const uint32_t elapsedMs = nowMs - session.startedAtMs;
    const double ticks = std::llround(
        static_cast<double>(elapsedMs) *
        validTempo(status_bar_->tempo.get()) *
        PROJECT_CONTROL_TICKS_PER_BEAT / 60000.0
    );
    return {
        .ticks = static_cast<uint32_t>(std::min<double>(
            ticks,
            static_cast<double>(std::numeric_limits<uint32_t>::max())
        )),
        .valid = true,
    };
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::validSession_(
    bool verifyCurvePoints
) const {
    const auto& session = macro_ui_->recordedShapeCapture;
    const auto& control = pages_->control;
    if (!session.active() || !session.take || !session.packedPoints ||
        control.authoredRevision != session.authoredRevision) {
        return false;
    }
    if (session.mode == ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED) {
        const auto live = core::state::macro::MacroWorkflow::
            planDestinationActivation(*pages_, session.address);
        return sameActivationPlan(live, session.destinationPlan);
    }
    if (session.mode !=
            ProjectRecordedShapeCaptureMode::REPLACE_EXISTING) {
        return session.mode ==
            ProjectRecordedShapeCaptureMode::CREATE_UNASSIGNED;
    }

    const auto& graph = control.authored.modulation;
    const auto& arena = control.authored.curves;
    if (session.expectedSourceIndex >= graph.sourceCount ||
        session.expectedCurveIndex >= arena.recordCount) {
        return false;
    }
    const auto& source = graph.sources[session.expectedSourceIndex];
    const auto& record = arena.records[session.expectedCurveIndex];
    if (!sameSource(source, session.expectedSource) ||
        source.parameters.recordedCurveId != record.id ||
        !sameCurve(record, session.expectedCurve)) {
        return false;
    }
    if (!verifyCurvePoints) return true;
    const auto* points = curvePoints(arena, record);
    uint64_t hash = 0U;
    return session.expectedCurvePointHashValid &&
        pointHash(points, record.pointCount, hash) &&
        hash == session.expectedCurvePointHash;
}

FLASHMEM void ProjectRecordedShapeCaptureWorkflow::publishAudition_() const {
    auto& session = macro_ui_->recordedShapeCapture;
    if (session.publishAudition == nullptr) return;
    ProjectRecordedShapeAuditionDescriptor descriptor{};
    if (!auditionDescriptor(descriptor)) return;
    session.auditionPublished = true;
    session.publishAudition(session.auditionContext, descriptor);
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::touchDeltaQ15(
    int32_t deltaQ15,
    uint32_t nowMs
) const {
    auto& session = macro_ui_->recordedShapeCapture;
    if (!session.active()) return false;
    const auto elapsed = elapsed_(nowMs);
    if (!elapsed.valid || !validSession_(false)) {
        (void)fail_(
            ProjectRecordedShapeCaptureStatus::INVALIDATED,
            ProjectModulationStatus::INVARIANT_VIOLATION
        );
        return false;
    }
    if (!session.take->touchDelta(deltaQ15, elapsed.ticks)) {
        (void)fail_(
            ProjectRecordedShapeCaptureStatus::INVALIDATED,
            ProjectModulationStatus::INVARIANT_VIOLATION
        );
        return false;
    }
    session.publishStatus(
        session.take->reduced
            ? ProjectRecordedShapeCaptureStatus::REDUCED
            : (session.take->touched
                  ? ProjectRecordedShapeCaptureStatus::RECORDING
                  : ProjectRecordedShapeCaptureStatus::ARMED)
    );
    publishAudition_();
    return true;
}

FLASHMEM bool
ProjectRecordedShapeCaptureWorkflow::configureRawEncoderOrigin(
    int32_t position
) const {
    auto& session = macro_ui_->recordedShapeCapture;
    if (!session.active()) return false;
    session.rawEncoderOrigin = position;
    session.rawEncoderAppliedQ15 = 0;
    session.rawEncoderConfigured = true;
    return true;
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::touchRawEncoder(
    int32_t position,
    uint32_t nowMs,
    uint16_t ticksPerFullScale
) const {
    auto& session = macro_ui_->recordedShapeCapture;
    if (!session.active() || ticksPerFullScale == 0U) return false;
    if (!session.rawEncoderConfigured) {
        session.rawEncoderOrigin = position;
        session.rawEncoderAppliedQ15 = 0;
        session.rawEncoderConfigured = true;
        return touchDeltaQ15(0, nowMs);
    }
    const int64_t rawDelta = static_cast<int64_t>(position) -
        session.rawEncoderOrigin;
    int64_t numerator = rawDelta * ProjectRecordedShapeTake::SOURCE_MAX;
    numerator += numerator >= 0
        ? static_cast<int64_t>(ticksPerFullScale / 2U)
        : -static_cast<int64_t>(ticksPerFullScale / 2U);
    const int32_t cumulativeQ15 = static_cast<int32_t>(std::clamp<int64_t>(
        numerator / ticksPerFullScale,
        ProjectRecordedShapeTake::SOURCE_MIN,
        ProjectRecordedShapeTake::SOURCE_MAX
    ));
    const int32_t deltaQ15 = cumulativeQ15 -
        session.rawEncoderAppliedQ15;
    session.rawEncoderAppliedQ15 = cumulativeQ15;
    return touchDeltaQ15(deltaQ15, nowMs);
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::sample(
    uint32_t nowMs
) const {
    auto& session = macro_ui_->recordedShapeCapture;
    if (!session.active()) return false;
    const auto elapsed = elapsed_(nowMs);
    if (!elapsed.valid || !validSession_(false)) {
        (void)fail_(
            ProjectRecordedShapeCaptureStatus::INVALIDATED,
            ProjectModulationStatus::INVARIANT_VIOLATION
        );
        return false;
    }
    if (!session.take->sample(elapsed.ticks)) {
        (void)fail_(
            ProjectRecordedShapeCaptureStatus::INVALIDATED,
            ProjectModulationStatus::INVARIANT_VIOLATION
        );
        return false;
    }
    session.publishStatus(
        session.take->reduced
            ? ProjectRecordedShapeCaptureStatus::REDUCED
            : (session.take->touched
                  ? ProjectRecordedShapeCaptureStatus::RECORDING
                  : ProjectRecordedShapeCaptureStatus::ARMED)
    );
    publishAudition_();
    return true;
}

FLASHMEM ProjectModulationResult
ProjectRecordedShapeCaptureWorkflow::fail_(
    ProjectRecordedShapeCaptureStatus status,
    ProjectModulationStatus projectStatus
) const {
    macro_ui_->recordedShapeCapture.endSession(status, projectStatus);
    return resultWith(projectStatus);
}

FLASHMEM ProjectModulationResult
ProjectRecordedShapeCaptureWorkflow::release(uint32_t nowMs) const {
    auto& session = macro_ui_->recordedShapeCapture;
    if (!session.active()) {
        return resultWith(ProjectModulationStatus::INVALID_ARGUMENT);
    }
    const auto elapsed = elapsed_(nowMs);
    if (!elapsed.valid || !validSession_(true)) {
        return fail_(
            ProjectRecordedShapeCaptureStatus::INVALIDATED,
            ProjectModulationStatus::INVARIANT_VIOLATION
        );
    }
    if (!session.take->sample(elapsed.ticks)) {
        return fail_(
            ProjectRecordedShapeCaptureStatus::INVALIDATED,
            ProjectModulationStatus::INVARIANT_VIOLATION
        );
    }
    if (!session.take->touched || !session.take->changed) {
        return fail_(
            ProjectRecordedShapeCaptureStatus::NO_CHANGE,
            ProjectModulationStatus::NO_CHANGE
        );
    }

    ProjectCurveSpec spec{};
    uint16_t pointCount = 0U;
    if (!session.take->buildPackedCurve(
            spec,
            session.packedPoints.get(),
            ProjectRecordedShapeCaptureState::PACKED_POINT_CAPACITY,
            pointCount
        )) {
        return fail_(
            ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED
        );
    }

    const RecordedShapeDraft sourceDraft{
        .name = session.sourceName.data(),
        .curve = spec,
        .points = session.packedPoints.get(),
        .pointCount = pointCount,
        .accent = session.accent,
        .enabled = session.enabled,
    };
    ProjectModulationResult result{};
    if (session.mode ==
            ProjectRecordedShapeCaptureMode::CREATE_UNASSIGNED) {
        result = history_->createUnassignedRecordedShape(*pages_, sourceDraft);
    } else if (session.mode ==
                   ProjectRecordedShapeCaptureMode::CREATE_ASSIGNED) {
        const ModulationBindingDraft binding{
            .sourceId = {},
            .destination = session.destination,
            .amountQ15 = session.amountQ15,
            .application = ModulationApplication::NATURAL,
            .transfer = ModulationTransfer::LINEAR,
            .slewMs = 0U,
            .enabled = true,
        };
        result = history_->createAssignedRecordedShape(
            *pages_,
            session.address,
            sourceDraft,
            binding,
            false,
            session.destinationPlan.changesTopology()
                ? &session.destinationPlan
                : nullptr
        );
    } else if (session.mode ==
                   ProjectRecordedShapeCaptureMode::REPLACE_EXISTING) {
        result = history_->replaceProjectRecordedShapeCurve(
            *pages_,
            session.sourceId,
            spec,
            session.packedPoints.get(),
            pointCount
        );
    } else {
        return fail_(
            ProjectRecordedShapeCaptureStatus::INVALIDATED,
            ProjectModulationStatus::INVARIANT_VIOLATION
        );
    }

    if (!result.changed()) {
        return fail_(
            result.status == ProjectModulationStatus::NO_CHANGE
                ? ProjectRecordedShapeCaptureStatus::NO_CHANGE
                : ProjectRecordedShapeCaptureStatus::COMMIT_FAILED,
            result.status
        );
    }

    session.endSession(
        ProjectRecordedShapeCaptureStatus::COMMITTED,
        result.status
    );
    if (operations_.markProjectMutated != nullptr) {
        operations_.markProjectMutated(operations_.context);
    }
    return result;
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::cancel() const {
    auto& session = macro_ui_->recordedShapeCapture;
    if (!session.active()) return false;
    session.endSession(
        ProjectRecordedShapeCaptureStatus::CANCELLED,
        ProjectModulationStatus::NO_CHANGE
    );
    return true;
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::active() const {
    return macro_ui_->recordedShapeCapture.active();
}

FLASHMEM ProjectRecordedShapeCaptureStatus
ProjectRecordedShapeCaptureWorkflow::status() const {
    return macro_ui_->recordedShapeCapture.status;
}

FLASHMEM uint32_t ProjectRecordedShapeCaptureWorkflow::revision() const {
    return macro_ui_->recordedShapeCapture.revision;
}

FLASHMEM ProjectModulationStatus
ProjectRecordedShapeCaptureWorkflow::lastProjectStatus() const {
    return macro_ui_->recordedShapeCapture.lastProjectStatus;
}

FLASHMEM const ProjectRecordedShapeTake*
ProjectRecordedShapeCaptureWorkflow::previewTake() const {
    const auto& session = macro_ui_->recordedShapeCapture;
    return session.active() ? session.take.get() : nullptr;
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::currentSourceValueQ15(
    int16_t& value
) const {
    const auto* take = previewTake();
    if (take == nullptr || !take->touched) return false;
    value = take->currentValue;
    return true;
}

FLASHMEM bool ProjectRecordedShapeCaptureWorkflow::auditionDescriptor(
    ProjectRecordedShapeAuditionDescriptor& out
) const {
    const auto& session = macro_ui_->recordedShapeCapture;
    if (!session.active() || !session.take || !session.take->touched ||
        session.mode ==
            ProjectRecordedShapeCaptureMode::CREATE_UNASSIGNED) {
        return false;
    }
    uint16_t positionQ16 = 0U;
    if (!session.take->writePositionQ16(positionQ16)) return false;
    out = {
        .mode = session.mode,
        .sourceId = session.sourceId,
        .destination = session.destination,
        .authoredRevision = session.authoredRevision,
        .sourceValueQ15 = session.take->currentValue,
        .amountQ15 = session.amountQ15,
        .positionQ16 = positionQ16,
    };
    return true;
}

}  // namespace core::handler

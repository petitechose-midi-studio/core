#include "handler/project/ProjectHandlerInternals.hpp"

#include <algorithm>
#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "state/contextual/GuardedActionState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/modulation/ProjectModulatorSourceSession.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

namespace core::handler {

using namespace project_handler_internal;

FLASHMEM void ProjectHandler::markRecordedShapeMutation(void* context) {
    auto* self = static_cast<ProjectHandler*>(context);
    if (self != nullptr) self->publishModulatorMutation(false);
}

FLASHMEM void ProjectHandler::publishRecordedShapeAudition(
    void* context,
    const core::state::modulation::ProjectRecordedShapeAuditionDescriptor&
        descriptor
) {
    auto* self = static_cast<ProjectHandler*>(context);
    if (self == nullptr || descriptor.mode !=
            core::state::modulation::ProjectRecordedShapeCaptureMode::
                REPLACE_EXISTING) {
        return;
    }
    (void)core::state::modulation::setProjectRecordedShapeSourceAudition(
        self->pages_.control.runtime,
        descriptor.sourceId,
        descriptor.sourceValueQ15
    );
}

FLASHMEM void ProjectHandler::clearRecordedShapeAudition(void* context) {
    auto* self = static_cast<ProjectHandler*>(context);
    if (self == nullptr) return;
    core::state::modulation::clearProjectRecordedShapeRuntimeAudition(
        self->pages_.control.runtime
    );
}

FLASHMEM core::state::modulation::ModulatorSourceState*
ProjectHandler::focusedModulator() {
    auto& graph = pages_.control.authored.modulation;
    const auto node = navigation_.currentNode.get();
    if (node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS ||
        node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_RENAME ||
        node == core::state::project::ProjectNodeId::MODULATOR_TRIGGER ||
        node == core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS ||
        (node ==
             core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER &&
         !navigation_.creatingModulatorSource)) {
        return core::state::modulation::findProjectModulator(
            graph,
            navigation_.selectedModulator
        );
    }
    if (navigation_.currentNode.get() !=
        core::state::project::ProjectNodeId::MODULATORS_ROOT) {
        return nullptr;
    }
    const uint8_t row = navigation_.focusedRow.get();
    return row < graph.sourceCount ? &graph.sources[row] : nullptr;
}

FLASHMEM const core::state::modulation::ModulatorSourceState*
ProjectHandler::focusedModulator() const {
    return const_cast<ProjectHandler*>(this)->focusedModulator();
}

FLASHMEM core::state::modulation::ModulationBindingState*
ProjectHandler::focusedModulationBinding() {
    if (navigation_.currentNode.get() !=
        core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS) {
        return nullptr;
    }
    return core::state::project::modulators::sourceBindingAtOrdinal(
        pages_.control.authored.modulation,
        navigation_.selectedModulator,
        navigation_.focusedRow.get()
    );
}

FLASHMEM const core::state::modulation::ModulationBindingState*
ProjectHandler::focusedModulationBinding() const {
    return const_cast<ProjectHandler*>(this)->focusedModulationBinding();
}

FLASHMEM uint16_t ProjectHandler::focusedModulatorDetailRowCount() const {
    if (navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_SOURCE_KIND_PICKER) {
        return core::state::project::modulators::MODULATOR_SOURCE_KIND_COUNT;
    }
    if (navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_TRIGGER) {
        const auto* triggerSource = focusedModulator();
        if (!triggerSource) return 0U;
        const auto session = core::state::modulation::
            resolveProjectModulatorSourceSession(
                pages_.control,
                triggerSource->id
            );
        if (pages_.control.audition.active() && !session.valid()) return 0U;
        return core::state::project::modulators::MODULATOR_TRIGGER_DETAIL_COUNT;
    }
    if (navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        return core::state::project::modulators::destinationPickerRowCount(
            pages_,
            navigation_
        );
    }
    const auto* source = focusedModulator();
    if (!source) return 0U;
    if (navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS) {
        return static_cast<uint16_t>(
            core::state::project::modulators::sourceDestinationCount(
                pages_.control.authored.modulation,
                source->id
            ) + 1U
        );
    }
    const bool options = navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS;
    const auto session = core::state::modulation::
        resolveProjectModulatorSourceSession(pages_.control, source->id);
    if (pages_.control.audition.active() && !session.valid()) return 0U;
    return core::state::project::modulators::sourceWorkspaceLayout(
        source->kind,
        options,
        session.audition()
    ).count;
}

FLASHMEM void ProjectHandler::publishModulatorMutation(bool markAuthored) {
    if (markAuthored) pages_.control.markAuthoredMutation();
    config_revision_.set(core::state::macro::nextMacroConfigRevision(
        config_revision_.get()
    ));
    navigation_.notifyContentChanged();
    lifecycle_.markProjectMutated();
}

FLASHMEM void ProjectHandler::refreshModulatorPreview(
    bool syncMacroRuntime,
    uint8_t dirtyMacro
) {
    if (syncMacroRuntime) {
        core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
            macros_,
            pages_
        );
        config_revision_.set(core::state::macro::nextMacroConfigRevision(
            config_revision_.get(),
            dirtyMacro
        ));
    }
    navigation_.notifyContentChanged();
}

FLASHMEM bool ProjectHandler::focusedRecordedShapeRecord() const {
    using namespace core::state::project::modulators;
    if (navigation_.currentNode.get() !=
            core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        pages_.control.audition.active()) {
        return false;
    }
    const auto* source = focusedModulator();
    return source != nullptr &&
        source->kind ==
            core::state::modulation::ModulatorKind::RECORDED_SHAPE &&
        sourceDetailLayout(source->kind).at(navigation_.focusedRow.get()) ==
            SourceDetailItem::RECORD;
}

FLASHMEM bool ProjectHandler::beginRecordedShapeCapture() {
    if (recorded_shape_capture_button_active_) return true;
    if (!focusedRecordedShapeRecord() || recorded_shape_capture_.active()) {
        return false;
    }
    const auto* source = focusedModulator();
    const uint32_t now = time_provider_ ? time_provider_() : 0U;
    const bool armed = source != nullptr &&
        recorded_shape_capture_.armReplaceExisting(now, source->id);
    syncRecordedShapeCaptureRevision();
    if (!armed) {
        navigation_.setLifecycleFeedback("Record unavailable");
        return false;
    }
    configureOptRaw(encoders_);
    if (!recorded_shape_capture_.configureRawEncoderOrigin(0)) {
        (void)recorded_shape_capture_.cancel();
        syncRecordedShapeCaptureRevision();
        syncFocusedEncoder();
        navigation_.setLifecycleFeedback("Record unavailable");
        return false;
    }
    recorded_shape_capture_button_active_ = true;
    navigation_.setLifecycleFeedback("ARMED · TURN OPT");
    navigation_.notifyContentChanged();
    return true;
}

FLASHMEM void ProjectHandler::releaseRecordedShapeCapture() {
    if (!recorded_shape_capture_button_active_) return;
    const auto result = recorded_shape_capture_.release(
        time_provider_ ? time_provider_() : 0U
    );
    syncRecordedShapeCaptureRevision();
    recorded_shape_capture_button_active_ = false;
    syncFocusedEncoder();
    if (result.changed()) {
        navigation_.setLifecycleFeedback("Recorded · Undo available");
    } else if (recorded_shape_capture_.status() ==
                   core::state::modulation::
                       ProjectRecordedShapeCaptureStatus::NO_CHANGE) {
        navigation_.setLifecycleFeedback("No movement · Unchanged");
    } else {
        navigation_.setLifecycleFeedback("Record failed · Unchanged");
    }
    navigation_.notifyContentChanged();
}

FLASHMEM bool ProjectHandler::cancelRecordedShapeCapture(
    const char* feedback
) {
    if (!recorded_shape_capture_button_active_) return false;
    (void)recorded_shape_capture_.cancel();
    syncRecordedShapeCaptureRevision();
    recorded_shape_capture_button_active_ = false;
    syncFocusedEncoder();
    if (feedback != nullptr) navigation_.setLifecycleFeedback(feedback);
    navigation_.notifyContentChanged();
    return true;
}

FLASHMEM void ProjectHandler::syncRecordedShapeCaptureRevision() {
    const uint32_t revision = recorded_shape_capture_.revision();
    if (macro_ui_.recordedShapeCaptureRevision.get() != revision) {
        macro_ui_.recordedShapeCaptureRevision.set(revision);
    }
}

FLASHMEM bool ProjectHandler::createDefaultRecordedShape() {
    using namespace core::state::modulation;
    constexpr uint16_t duration = static_cast<uint16_t>(
        4U * PROJECT_CONTROL_TICKS_PER_BEAT
    );
    constexpr std::array<ProjectPackedCurvePoint, 2U> points{{
        {0U, 0},
        {duration, 0},
    }};
    char name[PROJECT_MODULATOR_NAME_CAPACITY]{};
    formatNextProjectModulatorName(
        pages_.control.authored.modulation,
        ModulatorKind::RECORDED_SHAPE,
        name,
        sizeof(name)
    );
    const auto created = macro_history_.createUnassignedRecordedShape(
        pages_,
        RecordedShapeDraft{
            .name = name,
            .curve = ProjectCurveSpec{
                .sourceDurationTicks = duration,
                .durationTicks = duration,
                .windowOffsetTicks = 0U,
                .interpolation = ProjectCurveInterpolation::LINEAR,
                .valueDomain = ProjectCurveValueDomain::BIPOLAR,
                .origin = ProjectCurveOrigin::NATIVE,
            },
            .points = points.data(),
            .pointCount = static_cast<uint16_t>(points.size()),
        }
    );
    if (!created.changed()) {
        navigation_.setLifecycleFeedback("Recorded Shape creation failed");
        return false;
    }
    while (navigation_.depth.get() > 0U) {
        (void)core::state::project::backProjectNavigation(navigation_);
    }
    auto& graph = pages_.control.authored.modulation;
    for (uint16_t index = 0U; index < graph.sourceCount; ++index) {
        if (graph.sources[index].id != created.sourceId) continue;
        navigation_.focusedRow.set(static_cast<uint8_t>(index));
        break;
    }
    navigation_.selectedModulator = created.sourceId;
    (void)core::state::project::openProjectModulatorDetail(
        navigation_, created.sourceId
    );
    navigation_.focusedRow.set(0U);
    publishModulatorMutation(false);
    navigation_.setLifecycleFeedback("Ready · Hold + turn OPT");
    return true;
}

FLASHMEM bool ProjectHandler::resizeFocusedRecordedShape(uint8_t beats) {
    using namespace core::state::modulation;
    auto* source = focusedModulator();
    if (source == nullptr || source->kind != ModulatorKind::RECORDED_SHAPE ||
        beats == 0U || beats > 64U || recorded_shape_capture_.active()) {
        return false;
    }
    auto& arena = pages_.control.authored.curves;
    const auto* curve = findProjectCurve(
        arena, source->parameters.recordedCurveId
    );
    if (curve == nullptr || curve->sourceDurationTicks == 0U ||
        curve->pointCount == 0U ||
        curve->pointCount > core::state::modulation::
            ProjectRecordedShapeCaptureState::PACKED_POINT_CAPACITY ||
        static_cast<uint32_t>(curve->pointOffset) + curve->pointCount >
            arena.pointCount ||
        !macro_ui_.recordedShapeCapture.ensureScratch()) {
        navigation_.setLifecycleFeedback("Length unavailable");
        return true;
    }
    const uint16_t nextDuration = static_cast<uint16_t>(
        static_cast<uint16_t>(beats) * PROJECT_CONTROL_TICKS_PER_BEAT
    );
    if (curve->sourceDurationTicks == nextDuration &&
        curve->durationTicks == nextDuration &&
        curve->windowOffsetTicks == 0U) {
        return true;
    }
    auto* output = macro_ui_.recordedShapeCapture.packedPoints.get();
    const auto* input = arena.points.data() + curve->pointOffset;
    const uint32_t previousDuration = curve->sourceDurationTicks;
    for (uint16_t index = 0U; index < curve->pointCount; ++index) {
        const uint32_t scaled =
            static_cast<uint32_t>(input[index].tick) * nextDuration +
            previousDuration / 2U;
        output[index] = {
            static_cast<uint16_t>(std::min<uint32_t>(
                nextDuration,
                scaled / previousDuration
            )),
            input[index].value,
        };
    }
    const ProjectCurveSpec spec{
        .sourceDurationTicks = nextDuration,
        .durationTicks = nextDuration,
        .windowOffsetTicks = 0U,
        .interpolation = curve->interpolation,
        .valueDomain = curve->valueDomain,
        .origin = curve->origin,
    };
    const auto resized = macro_history_.replaceProjectRecordedShapeCurve(
        pages_, source->id, spec, output, curve->pointCount
    );
    if (!resized.changed()) {
        navigation_.setLifecycleFeedback("Length unchanged");
        return true;
    }
    publishModulatorMutation(false);
    char feedback[32]{};
    std::snprintf(
        feedback,
        sizeof(feedback),
        "Length · %u beat%s",
        static_cast<unsigned>(beats),
        beats == 1U ? "" : "s"
    );
    navigation_.setLifecycleFeedback(feedback);
    return true;
}

FLASHMEM void ProjectHandler::toggleFocusedModulator() {
    if (auto* binding = focusedModulationBinding()) {
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            binding->destination.track,
            binding->destination.page,
            binding->destination.macro,
        };
        const bool enabled =
            (binding->flags &
             core::state::modulation::PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
        if (macro_history_.setModulationBindingEnabled(
                pages_, address, binding->id, !enabled
            )) {
            publishModulatorMutation(false);
            navigation_.setLifecycleFeedback(
                enabled ? "Destination Off" : "Destination On"
            );
        }
        return;
    }
    auto* source = focusedModulator();
    if (!source) return;
    const bool enabled =
        (source->flags &
         core::state::modulation::PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
    if (macro_history_.setProjectModulatorEnabled(
            pages_, source->id, !enabled
        )) {
        publishModulatorMutation(false);
        navigation_.setLifecycleFeedback(enabled ? "Source Off" : "Source On");
    }
}

FLASHMEM void ProjectHandler::beginModulatorBottomLeft() {
    if (focusedRecordedShapeRecord()) {
        (void)beginRecordedShapeCapture();
        return;
    }
    const auto* source = focusedModulator();
    if (!source) return;
    navigation_.clearLifecycleFeedback();
    auto guard = navigation_.modulatorGuard.get();
    if (core::state::contextual::guardedActionTerminal(guard)) {
        core::state::contextual::resetGuardedAction(guard);
    }
    if (!core::state::contextual::beginGuardedActionPress(
            guard,
            time_provider_ ? time_provider_() : 0U,
            static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        )) {
        return;
    }
    navigation_.guardedModulator = source->id;
    const auto* binding = focusedModulationBinding();
    navigation_.guardedModulationBinding = binding ? binding->id
                                                   : core::state::modulation::ModulationBindingId{};
    navigation_.modulatorGuard.set(guard);
}

FLASHMEM void ProjectHandler::releaseModulatorBottomLeft() {
    if (recorded_shape_capture_button_active_) {
        releaseRecordedShapeCapture();
        return;
    }
    auto guard = navigation_.modulatorGuard.get();
    if (!core::state::modulation::valid(navigation_.guardedModulator) &&
        !core::state::modulation::valid(
            navigation_.guardedModulationBinding
        )) return;
    const uint32_t now = time_provider_ ? time_provider_() : 0U;
    if (guard.phase == core::state::contextual::GuardedActionPhase::PRESSED &&
        (now - guard.pressedAtMs) >= Config::Timing::LATCH_THRESHOLD_MS) {
        (void)core::state::contextual::armGuardedAction(
            guard,
            guard.pressedAtMs
        );
        (void)core::state::contextual::updateGuardedAction(guard, now);
    }
    const auto outcome = core::state::contextual::releaseGuardedAction(
        guard,
        now
    );
    navigation_.modulatorGuard.set(guard);
    if (outcome == core::state::contextual::GuardedActionRelease::TAP) {
        toggleFocusedModulator();
    } else if (
        outcome == core::state::contextual::GuardedActionRelease::COMMITTED
    ) {
        deleteGuardedModulator();
    } else if (
        outcome == core::state::contextual::GuardedActionRelease::CANCELLED
    ) {
        navigation_.setLifecycleFeedback(
            core::state::modulation::valid(
                navigation_.guardedModulationBinding
            ) ? "Remove cancelled" : "Delete cancelled"
        );
    }
    navigation_.guardedModulator = {};
    navigation_.guardedModulationBinding = {};
    navigation_.modulatorGuard.set({});
}

FLASHMEM void ProjectHandler::beginModulatorBottomRight() {
    const auto node = navigation_.currentNode.get();
    if (node != core::state::project::ProjectNodeId::MODULATORS_ROOT &&
        node != core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL &&
        node != core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS &&
        node != core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS) {
        return;
    }
    const auto* source = focusedModulator();
    if (!source) return;
    navigation_.clearLifecycleFeedback();
    auto guard = navigation_.modulatorClipboardGuard.get();
    if (core::state::contextual::guardedActionTerminal(guard)) {
        core::state::contextual::resetGuardedAction(guard);
    }
    if (!core::state::contextual::beginGuardedActionPress(
            guard,
            time_provider_ ? time_provider_() : 0U,
            static_cast<uint16_t>(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        )) {
        return;
    }
    navigation_.guardedClipboardModulator = source->id;
    navigation_.modulatorClipboardPasteAvailable =
        node != core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS &&
        clipboard_.hasProjectModulatorSource() &&
        core::state::modulation::findProjectModulator(
            pages_.control.authored.modulation,
            clipboard_.projectModulatorSource.sourceId
        ) != nullptr;
    navigation_.modulatorClipboardGuard.set(guard);
}

FLASHMEM void ProjectHandler::copyFocusedModulator() {
    const auto id = navigation_.guardedClipboardModulator;
    if (!core::state::modulation::valid(id) ||
        !clipboard_.storeProjectModulatorSource(pages_.control, id)) {
        navigation_.setLifecycleFeedback("Source copy failed");
        return;
    }
    const auto* source = core::state::modulation::findProjectModulator(
        pages_.control.authored.modulation,
        id
    );
    char feedback[48]{};
    std::snprintf(
        feedback,
        sizeof(feedback),
        "Copied %s",
        source ? source->name.data() : "source"
    );
    navigation_.setLifecycleFeedback(feedback);
}

FLASHMEM void ProjectHandler::pasteProjectModulatorSource() {
    using namespace core::state::modulation;
    if (!clipboard_.hasProjectModulatorSource()) {
        navigation_.setLifecycleFeedback("No Source to paste");
        return;
    }
    const auto sourceId = clipboard_.projectModulatorSource.sourceId;
    const auto* source = findProjectModulator(
        pages_.control.authored.modulation,
        sourceId
    );
    if (!source) {
        navigation_.setLifecycleFeedback("Copied Source unavailable");
        return;
    }
    char name[PROJECT_MODULATOR_NAME_CAPACITY]{};
    formatNextProjectModulatorName(
        pages_.control.authored.modulation,
        source->kind,
        name,
        sizeof(name)
    );
    const auto duplicate = macro_history_.duplicateProjectModulator(
        pages_,
        sourceId,
        name
    );
    if (!duplicate.changed()) {
        navigation_.setLifecycleFeedback("Source paste failed");
        return;
    }
    while (navigation_.depth.get() > 0U) {
        (void)core::state::project::backProjectNavigation(navigation_);
    }
    auto& graph = pages_.control.authored.modulation;
    for (uint16_t index = 0; index < graph.sourceCount; ++index) {
        if (graph.sources[index].id == duplicate.sourceId) {
            navigation_.focusedRow.set(static_cast<uint8_t>(index));
            break;
        }
    }
    navigation_.selectedModulator = duplicate.sourceId;
    publishModulatorMutation(false);
    char feedback[48]{};
    std::snprintf(feedback, sizeof(feedback), "Pasted %s", name);
    navigation_.setLifecycleFeedback(feedback);
}

FLASHMEM void ProjectHandler::makeFocusedModulatorIndependent() {
    using namespace core::state::modulation;
    auto& graph = pages_.control.authored.modulation;
    const auto* binding = focusedModulationBinding();
    const auto* source = binding
        ? findProjectModulator(graph, binding->sourceId)
        : nullptr;
    if (!binding || !source) {
        navigation_.setLifecycleFeedback("Destination unavailable");
        return;
    }
    if (core::state::project::modulators::sourceDestinationCount(
            graph,
            source->id
        ) <= 1U) {
        navigation_.setLifecycleFeedback("Already independent");
        return;
    }

    char name[PROJECT_MODULATOR_NAME_CAPACITY]{};
    formatNextProjectModulatorName(
        graph,
        source->kind,
        name,
        sizeof(name)
    );
    const ModulationBindingId bindingId = binding->id;
    const ModulatorSplitRequest request{
        .sourceId = source->id,
        .cloneName = name,
        .bindingIdsToMove = &bindingId,
        .bindingCountToMove = 1U,
    };
    const auto split = macro_history_.splitProjectModulator(pages_, request);
    if (!split.changed()) {
        navigation_.setLifecycleFeedback("Cannot make independent");
        return;
    }

    navigation_.selectedModulator = split.sourceId;
    navigation_.selectedModulationBinding = bindingId;
    navigation_.focusedRow.set(0U);
    publishModulatorMutation(false);
    char feedback[48]{};
    std::snprintf(
        feedback,
        sizeof(feedback),
        "Independent · %s",
        name
    );
    navigation_.setLifecycleFeedback(feedback);
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::releaseModulatorBottomRight() {
    auto guard = navigation_.modulatorClipboardGuard.get();
    if (!core::state::modulation::valid(
            navigation_.guardedClipboardModulator
        )) return;
    const uint32_t now = time_provider_ ? time_provider_() : 0U;
    if (guard.phase == core::state::contextual::GuardedActionPhase::PRESSED &&
        (now - guard.pressedAtMs) >= Config::Timing::LATCH_THRESHOLD_MS) {
        (void)core::state::contextual::armGuardedAction(
            guard,
            guard.pressedAtMs
        );
        (void)core::state::contextual::updateGuardedAction(guard, now);
    }
    const auto outcome = core::state::contextual::releaseGuardedAction(
        guard,
        now
    );
    navigation_.modulatorClipboardGuard.set(guard);
    const bool destination = navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS;
    if (destination &&
        (outcome == core::state::contextual::GuardedActionRelease::TAP ||
         outcome == core::state::contextual::GuardedActionRelease::COMMITTED)) {
        makeFocusedModulatorIndependent();
    } else if (outcome == core::state::contextual::GuardedActionRelease::TAP) {
        copyFocusedModulator();
    } else if (
        outcome == core::state::contextual::GuardedActionRelease::COMMITTED
    ) {
        pasteProjectModulatorSource();
    }
    navigation_.guardedClipboardModulator = {};
    navigation_.modulatorClipboardPasteAvailable = false;
    navigation_.modulatorClipboardGuard.set({});
}

FLASHMEM void ProjectHandler::deleteGuardedModulator() {
    using namespace core::state::modulation;
    auto& graph = pages_.control.authored.modulation;
    const ModulationBindingId guardedBinding =
        navigation_.guardedModulationBinding;
    if (valid(guardedBinding)) {
        const auto* binding = findProjectModulationBinding(graph, guardedBinding);
        if (!binding) return;
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            binding->destination.track,
            binding->destination.page,
            binding->destination.macro,
        };
        const uint8_t removedRow = navigation_.focusedRow.get();
        if (!macro_history_.removeModulationBinding(
                pages_, address, guardedBinding
            )) {
            navigation_.setLifecycleFeedback("Remove failed");
            return;
        }
        const uint16_t remaining =
            core::state::project::modulators::sourceDestinationCount(
                graph,
                navigation_.selectedModulator
            );
        const uint8_t nextRow = remaining == 0U
            ? 0U
            : static_cast<uint8_t>(
                  std::min<uint16_t>(removedRow, remaining - 1U)
              );
        navigation_.focusedRow.set(nextRow);
        const auto* next =
            core::state::project::modulators::sourceBindingAtOrdinal(
                graph,
                navigation_.selectedModulator,
                nextRow
            );
        navigation_.selectedModulationBinding = next ? next->id
                                                     : ModulationBindingId{};
        publishModulatorMutation(false);
        navigation_.setLifecycleFeedback("Destination removed");
        return;
    }
    const ModulatorId id = navigation_.guardedModulator;
    const auto* source = findProjectModulator(graph, id);
    if (!source) return;
    const uint16_t destinations =
        core::state::project::modulators::sourceDestinationCount(graph, id);
    char name[PROJECT_MODULATOR_NAME_CAPACITY]{};
    std::snprintf(name, sizeof(name), "%s", source->name.data());
    uint16_t sourceIndex = 0;
    while (sourceIndex < graph.sourceCount && graph.sources[sourceIndex].id != id) {
        ++sourceIndex;
    }
    const auto result = macro_history_.deleteProjectModulator(pages_, id);
    if (!result.changed()) {
        navigation_.setLifecycleFeedback("Delete failed");
        return;
    }
    while (navigation_.depth.get() > 0U &&
           navigation_.currentNode.get() !=
               core::state::project::ProjectNodeId::MODULATORS_ROOT) {
        (void)core::state::project::backProjectNavigation(navigation_);
    }
    const uint16_t remaining = graph.sourceCount;
    const uint16_t next = sourceIndex < remaining
        ? sourceIndex
        : (remaining > 0U ? remaining - 1U : 0U);
    navigation_.focusedRow.set(static_cast<uint8_t>(next));
    navigation_.selectedModulator = {};
    char feedback[64]{};
    std::snprintf(
        feedback,
        sizeof(feedback),
        "Deleted %s · %u dest.",
        name,
        static_cast<unsigned>(destinations)
    );
    publishModulatorMutation(false);
    navigation_.setLifecycleFeedback(feedback);
}

}  // namespace core::handler

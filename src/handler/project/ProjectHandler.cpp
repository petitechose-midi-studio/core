#include "handler/project/ProjectHandlerInternals.hpp"

#include <algorithm>
#include <cstdio>

#include <config/Timing.hpp>
#include <oc/time/Time.hpp>

#include "handler/common/ModulatorNavigationWorkflow.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

namespace core::handler {

using namespace project_handler_internal;

FLASHMEM ProjectHandler::ProjectHandler(StateRefs state,
                                        SequencerSettingsDomainServices sequencerSettings,
                                        oc::api::EncoderAPI& encoders,
                                        oc::api::ButtonAPI& buttons,
                                        oc::type::ScopeID projectViewScope,
                                        uint32_t (*timeProvider)())
    : overlays_(state.overlays)
    , active_view_(state.activeView)
    , navigation_(state.navigation)
    , sequencer_(state.sequencer)
    , sequencer_tracks_(state.sequencerTracks)
    , status_bar_(state.statusBar)
    , midi_sync_(state.midiSync)
    , pages_(state.pages)
    , macro_edit_(state.macroEdit)
    , config_revision_(state.configRevision)
    , macro_history_(state.macroHistory)
    , clipboard_(state.clipboard)
    , history_(state.history)
    , lifecycle_(state.lifecycle)
    , sequencer_settings_(sequencerSettings)
    , encoders_(encoders)
    , buttons_(buttons)
    , project_view_scope_(projectViewScope)
    , time_provider_(timeProvider ? timeProvider : oc::time::millis) {
    setupBindings();
}

FLASHMEM bool ProjectHandler::canHandleProjectInput() const {
    return !overlays_.hasVisible();
}

FLASHMEM bool ProjectHandler::projectConfirmationActive() const {
    return core::state::project::projectNavigationInProjectConfirmation(navigation_);
}

FLASHMEM bool ProjectHandler::physicalHoldActive() const {
    return canHandleProjectInput() && !projectConfirmationActive() &&
           navigation_.physicalHoldActive.get();
}

FLASHMEM bool ProjectHandler::regularProjectInputActive() const {
    return canHandleProjectInput() && !navigation_.physicalHoldActive.get();
}

FLASHMEM void ProjectHandler::enterPhysicalHoldLayer() {
    navigation_.physicalHoldActive.set(true);
}

FLASHMEM void ProjectHandler::leavePhysicalHoldLayer() {
    navigation_.physicalHoldActive.set(false);
    syncFocusedEncoder();
}

FLASHMEM core::state::modulation::ModulatorSourceState*
ProjectHandler::focusedModulator() {
    auto& graph = pages_.control.authored.modulation;
    const auto node = navigation_.currentNode.get();
    if (node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        node == core::state::project::ProjectNodeId::MODULATOR_REACH ||
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
        core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        return navigation_.creatingModulatorSource ? 9U : 8U;
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
    if (navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_REACH) {
        return core::state::project::modulators::sourceReachChoiceLayout(
            pages_.control.authored.modulation,
            source->id
        ).count;
    }
    return core::state::project::modulators::sourceDetailLayout(source->kind).count;
}

FLASHMEM void ProjectHandler::publishModulatorMutation(bool markAuthored) {
    if (markAuthored) pages_.control.markAuthoredMutation();
    config_revision_.set(config_revision_.get() + 1U);
    navigation_.notifyContentChanged();
    lifecycle_.markProjectMutated();
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
        node != core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL) {
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
    if (outcome == core::state::contextual::GuardedActionRelease::TAP) {
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
    if (navigation_.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        navigation_.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS) {
        (void)core::state::project::backProjectNavigation(navigation_);
        if (navigation_.currentNode.get() ==
            core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL) {
            (void)core::state::project::backProjectNavigation(navigation_);
        }
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

void ProjectHandler::update(uint32_t nowMs) {
    const bool modulatorContext =
        active_view_.get() == core::ui::ViewType::PROJECT &&
        canHandleProjectInput() &&
        !navigation_.physicalHoldActive.get() &&
        (navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATORS_ROOT ||
         navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
         navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS);
    const bool bottomLeftPressed = buttons_.isPressed(Config::ButtonID::BOTTOM_LEFT);
    if (modulatorContext && bottomLeftPressed &&
        !modulator_bottom_left_was_pressed_) {
        beginModulatorBottomLeft();
    } else if (modulator_bottom_left_was_pressed_ && !bottomLeftPressed) {
        releaseModulatorBottomLeft();
    } else if (!modulatorContext && modulator_bottom_left_was_pressed_) {
        navigation_.guardedModulator = {};
        navigation_.guardedModulationBinding = {};
        navigation_.modulatorGuard.set({});
    }
    modulator_bottom_left_was_pressed_ = modulatorContext && bottomLeftPressed;

    const bool clipboardContext =
        active_view_.get() == core::ui::ViewType::PROJECT &&
        canHandleProjectInput() && !navigation_.physicalHoldActive.get() &&
        (navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATORS_ROOT ||
         navigation_.currentNode.get() ==
             core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL);
    const bool bottomRightPressed =
        buttons_.isPressed(Config::ButtonID::BOTTOM_RIGHT);
    if (clipboardContext && bottomRightPressed &&
        !modulator_bottom_right_was_pressed_) {
        beginModulatorBottomRight();
    } else if (modulator_bottom_right_was_pressed_ && !bottomRightPressed) {
        releaseModulatorBottomRight();
    } else if (!clipboardContext && modulator_bottom_right_was_pressed_) {
        navigation_.guardedClipboardModulator = {};
        navigation_.modulatorClipboardPasteAvailable = false;
        navigation_.modulatorClipboardGuard.set({});
    }
    modulator_bottom_right_was_pressed_ = clipboardContext && bottomRightPressed;

    auto guard = navigation_.modulatorGuard.get();
    if (guard.phase == core::state::contextual::GuardedActionPhase::PRESSED &&
        (nowMs - guard.pressedAtMs) >= Config::Timing::LATCH_THRESHOLD_MS) {
        (void)core::state::contextual::armGuardedAction(
            guard,
            guard.pressedAtMs
        );
        navigation_.modulatorGuard.set(guard);
    }
    auto clipboardGuard = navigation_.modulatorClipboardGuard.get();
    if (clipboardGuard.phase ==
            core::state::contextual::GuardedActionPhase::PRESSED &&
        (nowMs - clipboardGuard.pressedAtMs) >=
            Config::Timing::LATCH_THRESHOLD_MS) {
        (void)core::state::contextual::armGuardedAction(
            clipboardGuard,
            clipboardGuard.pressedAtMs
        );
        navigation_.modulatorClipboardGuard.set(clipboardGuard);
    }
}

FLASHMEM void ProjectHandler::resetProject() {
    lifecycle_.resetMusicalProject();
}

FLASHMEM void ProjectHandler::back() {
    macro_history_.endCoalescing();
    if (modulator_navigation::shouldReturnToMacroOnBack(navigation_) &&
        modulator_navigation::returnToMacro(
            {
                overlays_,
                active_view_,
                navigation_,
                macro_edit_,
                pages_,
            },
            time_provider_()
        )) {
        return;
    }
    core::state::project::backProjectNavigation(navigation_);
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::consumeUndo() {
    if (navigation_.activeTab.get() ==
            core::state::project::ProjectTab::MODULATORS &&
        macro_history_.undo(pages_)) {
        publishModulatorMutation(false);
        navigation_.setLifecycleFeedback("Modulation undone");
        syncFocusedEncoder();
        return;
    }
    history_.undo();
}

FLASHMEM void ProjectHandler::consumeRedo() {
    if (navigation_.activeTab.get() ==
            core::state::project::ProjectTab::MODULATORS &&
        macro_history_.redo(pages_)) {
        publishModulatorMutation(false);
        navigation_.setLifecycleFeedback("Modulation redone");
        syncFocusedEncoder();
        return;
    }
    history_.redo();
}


}  // namespace core::handler

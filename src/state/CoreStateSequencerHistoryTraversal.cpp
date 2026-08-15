#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <new>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>
#include <utility>

#include "state/CoreState.hpp"

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
#include <wiring.h>
#endif

#include "macro/MacroWorkflow.hpp"
#include "midi/MidiUtils.hpp"
#include "state/CoreStateBootstrap.hpp"
#include "state/CoreStateLifecycle.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/shared/SharedTrackCoordinator.hpp"

namespace core::state {

namespace {

FLASHMEM const char* historyDirectionLabel(sequencer::SequencerHistoryDirection direction) {
    return direction == sequencer::SequencerHistoryDirection::Redo ? "REDO" : "UNDO";
}

FLASHMEM const char* historyPropertyLabel(sequencer::StepProperty property) {
    switch (property) {
        case sequencer::StepProperty::NOTE: return "Pitch";
        case sequencer::StepProperty::VELOCITY: return "Velocity";
        case sequencer::StepProperty::GATE: return "Gate";
        case sequencer::StepProperty::NUDGE: return "Nudge";
        case sequencer::StepProperty::PROBABILITY: return "Chance";
        default: return "Property";
    }
}

FLASHMEM const char* historyActionLabel(sequencer::SequencerHistoryActionKind kind) {
    switch (kind) {
        case sequencer::SequencerHistoryActionKind::StepToggle: return "Step Toggle";
        case sequencer::SequencerHistoryActionKind::StepPropertyEdit: return "Step Property";
        case sequencer::SequencerHistoryActionKind::StepEdit: return "Step Edit";
        case sequencer::SequencerHistoryActionKind::QuickControls: return "Quick Controls";
        case sequencer::SequencerHistoryActionKind::PatternSettings: return "Pattern Settings";
        case sequencer::SequencerHistoryActionKind::PatternVariation: return "Variation Range";
        case sequencer::SequencerHistoryActionKind::ProjectScaleSettings: return "Project Scale";
        case sequencer::SequencerHistoryActionKind::PageStructure: return "Page Structure";
        case sequencer::SequencerHistoryActionKind::TrackStructure: return "Track Structure";
        case sequencer::SequencerHistoryActionKind::FullBank: return "Sequencer Set";
        case sequencer::SequencerHistoryActionKind::PatternRandomize: return "Pattern Randomize";
        case sequencer::SequencerHistoryActionKind::DrumStepToggle: return "Drum Step State";
        case sequencer::SequencerHistoryActionKind::DrumStepPropertyEdit:
            return "Drum Step Property";
        case sequencer::SequencerHistoryActionKind::DrumLaneEdit: return "Drum Lane";
        case sequencer::SequencerHistoryActionKind::DrumLaneStructure:
            return "Drum Lane Structure";
        case sequencer::SequencerHistoryActionKind::DrumPatternSettings:
            return "Drum Pattern";
        case sequencer::SequencerHistoryActionKind::DrumTrackKind: return "Track Type";
        case sequencer::SequencerHistoryActionKind::DrumAdvancedContent:
            return "Drum Step Content";
        case sequencer::SequencerHistoryActionKind::DrumLaneContent:
            return "Drum Lane Content";
        case sequencer::SequencerHistoryActionKind::PatternEdit:
        default: return "Pattern Edit";
    }
}

FLASHMEM void formatHistoryValue(char* buffer, size_t bufferSize, sequencer::StepProperty property,
                                 int32_t value) {
    if (!buffer || bufferSize == 0) return;

    if (property == sequencer::StepProperty::NOTE) {
        core::midi::formatNoteName(
            buffer, bufferSize, static_cast<uint8_t>(value < 0 ? 0 : (value > 127 ? 127 : value)));
        return;
    }

    if (property == sequencer::StepProperty::GATE) {
        std::snprintf(buffer, bufferSize, "%ld%%", static_cast<long>(value));
        return;
    }

    if (property == sequencer::StepProperty::NUDGE) {
        std::snprintf(buffer, bufferSize, "%+ld", static_cast<long>(value));
        return;
    }

    std::snprintf(buffer, bufferSize, "%ld", static_cast<long>(value));
}

FLASHMEM void formatHistoryVariationValue(char* buffer, size_t bufferSize,
                                          sequencer::StepProperty property, int32_t value) {
    if (!buffer || bufferSize == 0) return;

    if (property == sequencer::StepProperty::NOTE) {
        std::snprintf(buffer, bufferSize, "+/-%ldst", static_cast<long>(value));
        return;
    }

    if (property == sequencer::StepProperty::GATE) {
        std::snprintf(buffer, bufferSize, "+/-%ld%%", static_cast<long>(value));
        return;
    }

    if (property == sequencer::StepProperty::NUDGE) {
        std::snprintf(buffer, bufferSize, "+/-%ld", static_cast<long>(value));
        return;
    }

    std::snprintf(buffer, bufferSize, "+/-%ld", static_cast<long>(value));
}

FLASHMEM void formatHistoryStructureValue(char* buffer, size_t bufferSize,
                                          sequencer::SequencerHistoryActionKind kind,
                                          int32_t value) {
    if (!buffer || bufferSize == 0) return;

    const char* unit =
        kind == sequencer::SequencerHistoryActionKind::TrackStructure ? "track" : "page";
    std::snprintf(buffer, bufferSize, "%ld %s%s", static_cast<long>(value), unit,
                  value == 1 ? "" : "s");
}

FLASHMEM void showSequencerHistoryFeedback(sequencer::SequencerState& sequencerState,
                                           const sequencer::SequencerHistoryApplyResult& result,
                                           uint32_t nowMs, const char* actionOverride = nullptr,
                                           const char* statusOverride = nullptr) {
    if (!result.applied) return;

    const auto& descriptor = result.descriptor;
    char line1[sequencer::SequencerHistoryFeedbackState::LINE_SIZE]{};
    char line2[sequencer::SequencerHistoryFeedbackState::LINE_SIZE]{};
    char line3[sequencer::SequencerHistoryFeedbackState::LINE_SIZE]{};

    const char* direction = historyDirectionLabel(result.direction);
    if (descriptor.trackIndex != sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        std::snprintf(line1, sizeof(line1), "%s T%02u", direction,
                      static_cast<unsigned>(descriptor.trackIndex + 1U));
    } else {
        std::snprintf(line1, sizeof(line1), "%s", direction);
    }

    if (actionOverride != nullptr) {
        std::snprintf(line2, sizeof(line2), "%s", actionOverride);
    } else if ((descriptor.kind == sequencer::SequencerHistoryActionKind::StepToggle ||
                descriptor.kind ==
                    sequencer::SequencerHistoryActionKind::DrumStepToggle) &&
               descriptor.stepIndex != sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        if (descriptor.laneIndex !=
            sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
            std::snprintf(
                line2,
                sizeof(line2),
                "L%02u Step %02u",
                static_cast<unsigned>(descriptor.laneIndex + 1U),
                static_cast<unsigned>(descriptor.stepIndex + 1U)
            );
        } else {
            std::snprintf(line2, sizeof(line2), "Step %02u State",
                          static_cast<unsigned>(descriptor.stepIndex + 1U));
        }
    } else if (descriptor.stepIndex != sequencer::SequencerHistoryDescriptor::INVALID_INDEX) {
        std::snprintf(line2, sizeof(line2), "Step %02u %s",
                      static_cast<unsigned>(descriptor.stepIndex + 1U),
                      historyPropertyLabel(descriptor.property));
    } else if (descriptor.kind == sequencer::SequencerHistoryActionKind::PatternVariation) {
        std::snprintf(line2, sizeof(line2), "Range %s", historyPropertyLabel(descriptor.property));
    } else {
        std::snprintf(line2, sizeof(line2), "%s", historyActionLabel(descriptor.kind));
    }

    if (statusOverride != nullptr) {
        std::snprintf(line3, sizeof(line3), "%s", statusOverride);
    } else if (descriptor.hasValue) {
        const int32_t fromValue = result.direction == sequencer::SequencerHistoryDirection::Undo
                                      ? descriptor.afterValue
                                      : descriptor.beforeValue;
        const int32_t toValue = result.direction == sequencer::SequencerHistoryDirection::Undo
                                    ? descriptor.beforeValue
                                    : descriptor.afterValue;

        if (descriptor.kind == sequencer::SequencerHistoryActionKind::StepToggle ||
            descriptor.kind ==
                sequencer::SequencerHistoryActionKind::DrumStepToggle) {
            std::snprintf(line3, sizeof(line3), "%s -> %s", fromValue != 0 ? "On" : "Off",
                          toValue != 0 ? "On" : "Off");
        } else if (descriptor.kind == sequencer::SequencerHistoryActionKind::PageStructure ||
                   descriptor.kind == sequencer::SequencerHistoryActionKind::TrackStructure) {
            char fromText[14]{};
            char toText[14]{};
            formatHistoryStructureValue(fromText, sizeof(fromText), descriptor.kind, fromValue);
            formatHistoryStructureValue(toText, sizeof(toText), descriptor.kind, toValue);
            std::snprintf(line3, sizeof(line3), "%s -> %s", fromText, toText);
        } else if (descriptor.kind == sequencer::SequencerHistoryActionKind::PatternVariation) {
            char fromText[12]{};
            char toText[12]{};
            formatHistoryVariationValue(fromText, sizeof(fromText), descriptor.property, fromValue);
            formatHistoryVariationValue(toText, sizeof(toText), descriptor.property, toValue);
            std::snprintf(line3, sizeof(line3), "%s -> %s", fromText, toText);
        } else {
            char fromText[12]{};
            char toText[12]{};
            formatHistoryValue(fromText, sizeof(fromText), descriptor.property, fromValue);
            formatHistoryValue(toText, sizeof(toText), descriptor.property, toValue);
            std::snprintf(line3, sizeof(line3), "%s -> %s", fromText, toText);
        }
    } else {
        std::snprintf(line3, sizeof(line3), "Applied");
    }

    sequencerState.historyFeedback.show(line1, line2, line3, nowMs);
}

FLASHMEM void reconcileSequencerCcLaneUiFromRestoredHistory(sequencer::SequencerState& editor) {
    auto& ccLaneUi = editor.ccLaneUi;
    const bool laneScopedMode =
        ccLaneUi.mode == sequencer::SequencerCcLaneUiMode::LANE_GRID ||
        ccLaneUi.mode == sequencer::SequencerCcLaneUiMode::TRANSITION_PICKER ||
        ccLaneUi.mode == sequencer::SequencerCcLaneUiMode::LANE_SETTINGS;
    if (!laneScopedMode) return;

    const auto* bank = sequencer::sequencerCcLaneView(editor.pattern);
    const bool focusedLaneExists = bank != nullptr && ccLaneUi.focusedLane < bank->lanes.size() &&
                                   bank->lanes[ccLaneUi.focusedLane].occupied;
    if (!focusedLaneExists) {
        // History restores musical data, not transient editor ownership. A
        // direct-create Undo or a remove Redo can therefore invalidate the
        // grid that was open before the global history gesture. Close the
        // complete session so no hidden CC-lane handler retains NAV/OPT.
        ccLaneUi.reset();
    }
}

FLASHMEM void syncSequencerStructureUiFromRestoredHistory(CoreState& state) {
    state.trackNavigation.previewAddSlot.set(false);
    state.trackNavigation.syncPreviewTrack(state.sharedTrackActive.get());

    state.sequencer.structureUi.syncPreviewPage(state.sequencer.visiblePage());
    reconcileSequencerCcLaneUiFromRestoredHistory(state.sequencer);
}

FLASHMEM void reconcileMacroTrackStructureFromRestoredHistory(
    CoreState& state, const sequencer::SequencerHistoryMacroTrackStructurePayload& payload) {
    for (uint8_t track = 0U; track < macro::TRACK_COUNT; ++track) {
        if ((payload.capturedTrackMask & static_cast<uint16_t>(1U << track)) == 0U) { continue; }
        (void)state.macroUi.manualOverrides.clearTrack(track);
    }
    state.macroUi.refreshManualOverrideMask(state.pages.currentActiveTrack(),
                                            state.pages.currentActivePage());
    state.macroUi.automationEditRevision.set(state.macroUi.automationEditRevision.get() + 1U);
    state.macroUi.runtimeProjectionRevision.set(
        core::state::macro::nextMacroRuntimeProjectionRevision(
            state.macroUi.runtimeProjectionRevision.get(),
            core::state::macro::kMacroRuntimeProjectionDirtyConfig));
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);
    state.configRevision.set(core::state::macro::nextMacroConfigRevision(
        state.configRevision.get(), core::state::macro::kMacroConfigDirtyAll));
    core::state::project::reconcileProjectModulatorNavigationAfterHistory(
        state.projectNavigation, state.pages.control.authored.modulation);
}

}  // namespace

FLASHMEM bool CoreState::traverseSequencerHistory_(
    sequencer::SequencerHistoryDirection direction
) {
    if (sequencer.stepContentDraft.rejectTransitionIfActive(
            sequencer::SequencerStepContentDraftBlockedTransition::HISTORY)) {
        return false;
    }
    if (commitSequencerPatternHistoryCoalescingOutcome() ==
        sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return false;
    }

    sequencer::SequencerPreparedStructureHistoryReplay preparedStructure;
    const auto structurePreparation =
        sequencerHistory.prepareStructureHistoryReplay(
            direction,
            sequencerTracks,
            sequencer,
            pages,
            preparedStructure);
    if (structurePreparation ==
        sequencer::SequencerStructureHistoryReplayPrepareOutcome::Rejected) {
        return false;
    }
    const bool structurePrepared = structurePreparation ==
        sequencer::SequencerStructureHistoryReplayPrepareOutcome::Prepared;
    if (structurePrepared) {
        return traversePreparedSequencerStructureHistory_(
            direction, std::move(preparedStructure));
    }
    return traverseGenericSequencerHistory_(direction);
}

FLASHMEM bool CoreState::armPreparedSequencerHistoryActivation_(
    sequencer::SequencerHistoryDirection direction,
    const sequencer::SequencerTrackActivationHistoryPlan& activation,
    sequencer::SequencerTrackActivationHistoryTransition& transition
) {
    sequencer::SequencerTrackActivationHistoryTransitionPlan plan;
    const auto target = direction == sequencer::SequencerHistoryDirection::Undo
        ? sequencer::SequencerTrackActivationTarget::BEFORE
        : sequencer::SequencerTrackActivationTarget::AFTER;
    return sequencerTrackActivations.planHistoryTransition(
               activation.reference,
               target,
               activation.targetAudibleMask,
               statusBar.playing.get(),
               plan) &&
        sequencerTrackActivations.tryArmPlannedHistoryTransition(
            plan, transition);
}

FLASHMEM bool CoreState::traversePreparedSequencerStructureHistory_(
    sequencer::SequencerHistoryDirection direction,
    sequencer::SequencerPreparedStructureHistoryReplay&& prepared
) {
    const auto* macroStructure = prepared.macroStructure;
    const auto activation = prepared.activation;
    const bool hasActivation = activation.valid();
    sequencer::SequencerTrackActivationHistoryTransition transition;
    if (hasActivation &&
        !armPreparedSequencerHistoryActivation_(
            direction, activation, transition)) {
        return false;
    }

    const uint8_t activeTrackBefore = sequencerTracks.activeTrackIndex();
    const auto result = sequencerHistory.commitPreparedStructureHistoryReplay(
        sequencerTracks,
        sequencer,
        pages,
        std::move(prepared));
    if (hasActivation) {
        sequencerTrackActivations.commitHistoryTransition(transition);
    }
    publishSequencerHistoryTraversal_(
        result,
        macroStructure,
        activation,
        transition,
        hasActivation,
        activeTrackBefore);
    return true;
}

FLASHMEM bool CoreState::traverseGenericSequencerHistory_(
    sequencer::SequencerHistoryDirection direction
) {
    sequencer::SequencerTrackActivationHistoryPlan activation;
    const bool hasActivation = direction == sequencer::SequencerHistoryDirection::Undo
        ? sequencerHistory.peekUndoTrackActivation(activation)
        : sequencerHistory.peekRedoTrackActivation(activation);
    sequencer::SequencerTrackActivationHistoryTransition transition;
    const auto target = direction == sequencer::SequencerHistoryDirection::Undo
        ? sequencer::SequencerTrackActivationTarget::BEFORE
        : sequencer::SequencerTrackActivationTarget::AFTER;
    if (hasActivation &&
        !sequencerTrackActivations.prepareHistoryTransition(
            activation.reference,
            target,
            activation.targetAudibleMask,
            statusBar.playing.get(),
            transition)) {
        return false;
    }

    const uint8_t activeTrackBefore = sequencerTracks.activeTrackIndex();
    const auto result = direction == sequencer::SequencerHistoryDirection::Undo
        ? sequencerHistory.undoWithResult(sequencerTracks, sequencer)
        : sequencerHistory.redoWithResult(sequencerTracks, sequencer);
    if (!result.applied) {
        if (hasActivation) {
            sequencerTrackActivations.rollbackHistoryTransition(transition);
        }
        return false;
    }
    if (hasActivation) {
        sequencerTrackActivations.commitHistoryTransition(transition);
    }
    publishSequencerHistoryTraversal_(
        result,
        nullptr,
        activation,
        transition,
        hasActivation,
        activeTrackBefore);
    return true;
}

FLASHMEM void CoreState::publishSequencerHistoryTraversal_(
    const sequencer::SequencerHistoryApplyResult& result,
    const sequencer::SequencerHistoryMacroTrackStructurePayload* macroStructure,
    const sequencer::SequencerTrackActivationHistoryPlan& activation,
    const sequencer::SequencerTrackActivationHistoryTransition& transition,
    bool hasActivation,
    uint8_t activeTrackBefore
) {
    // History application has already restored editor and bank atomically.
    // Consume its deferred watched-signal notifications at the same prepared
    // boundary so traversal publishes dirty/save exactly once without recloning.
    publishPreparedSequencerMutation();
    sequencer::refreshContentView(sequencer);
    sequencer.contentView.bump();
    const bool trackPaste =
        hasActivation &&
        activation.reference.origin == sequencer::SequencerTrackActivationOrigin::TRACK_PASTE;
    const bool waitsForLoop = hasActivation && statusBar.playing.get() &&
                              (transition.queuedMask & activation.targetAudibleMask) != 0;
    const bool cancelsPending = hasActivation && transition.cancelledMask != 0 &&
                                transition.queuedMask == 0;
    showSequencerHistoryFeedback(
        sequencer, result, oc::time::millis(), trackPaste ? "Track Paste" : nullptr,
        waitsForLoop ? "At next loop"
                     : (cancelsPending ? "Pending cancelled" : (trackPaste ? "Applied" : nullptr)));
    refreshSharedTrackStateFromSequencer();
    if (macroStructure != nullptr) {
        reconcileMacroTrackStructureFromRestoredHistory(*this, *macroStructure);
    } else if (result.descriptor.kind ==
                   sequencer::SequencerHistoryActionKind::TrackStructure &&
               activeTrackBefore != sequencerTracks.activeTrackIndex()) {
        reconcilePreparedSequencerActiveTrackPresentation();
    }
    syncSequencerStructureUiFromRestoredHistory(*this);
}

FLASHMEM bool CoreState::undoSequencerHistory() {
    return traverseSequencerHistory_(sequencer::SequencerHistoryDirection::Undo);
}

FLASHMEM bool CoreState::redoSequencerHistory() {
    return traverseSequencerHistory_(sequencer::SequencerHistoryDirection::Redo);
}

FLASHMEM bool CoreState::clearSequencerHistory() {
    // A non-undoable Sequencer load/reset is also a boundary in the single
    // Project chronology. Clear both payload owners so no unreachable entries
    // consume retained-history capacity behind that boundary.
    return clearProjectHistory();
}

}  // namespace core::state

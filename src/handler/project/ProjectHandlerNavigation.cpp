#include "handler/project/ProjectHandlerInternals.hpp"

#include <algorithm>
#include <cstdio>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "ui/macro/MacroLfoAuditionModel.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

namespace core::handler {

using namespace project_handler_internal;

namespace {

FLASHMEM core::state::modulation::ModulatorReach minimumReachForDestination(
    const core::state::modulation::ModulatorReach& current,
    const core::state::modulation::ModulationDestination& destination
) {
    using namespace core::state::modulation;
    if (modulatorReachContains(current, destination)) return current;
    switch (current.kind) {
        case ModulatorReachKind::DETACHED:
            return {
                .kind = ModulatorReachKind::MACRO,
                .track = destination.track,
                .page = destination.page,
                .macro = destination.macro,
            };
        case ModulatorReachKind::MACRO:
            return {
                .trackMask = static_cast<uint16_t>(
                    (1U << current.track) | (1U << destination.track)
                ),
                .kind = ModulatorReachKind::TRACK_SET,
            };
        case ModulatorReachKind::TRACK_SET:
            return {
                .trackMask = static_cast<uint16_t>(
                    current.trackMask | (1U << destination.track)
                ),
                .kind = ModulatorReachKind::TRACK_SET,
            };
        case ModulatorReachKind::PROJECT:
        default:
            return current;
    }
}

FLASHMEM bool sourceAlreadyTargets(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulationDestination& destination
) {
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.sourceId == sourceId && binding.destination == destination) {
            return true;
        }
    }
    return false;
}

}  // namespace

FLASHMEM void ProjectHandler::navigate(float delta) {
    if (delta != 0.0f) {
        navigation_.clearLifecycleFeedback();
    }
    if (isProjectNameEditorNode(navigation_.currentNode.get())) {
        navigation_.projectNameKeyIndex = core::state::project::projectNameKeyboardMoveColumn(
            navigation_.projectNameKeyIndex,
            signedStepCount(delta)
        );
        navigation_.notifyContentChanged();
        return;
    }
    macro_history_.endCoalescing();
    core::state::project::navigateProjectRows(
        navigation_,
        delta,
        pages_.control.authored.modulation.sourceCount,
        focusedModulatorDetailRowCount()
    );
    if (navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS) {
        const auto* binding = focusedModulationBinding();
        navigation_.selectedModulationBinding = binding
            ? binding->id
            : core::state::modulation::ModulationBindingId{};
    }
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::switchTab(float delta) {
    if (delta == 0.0f) return;
    navigation_.clearLifecycleFeedback();
    core::state::project::switchProjectTab(navigation_, signedStepCount(delta));
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::enterFocused() {
    const auto node = navigation_.currentNode.get();
    if (node == core::state::project::ProjectNodeId::MODULATORS_ROOT ||
        node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL ||
        node == core::state::project::ProjectNodeId::MODULATOR_REACH ||
        node == core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS ||
        node ==
            core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        navigation_.clearLifecycleFeedback();
        enterFocusedModulator();
        syncFocusedEncoder();
        return;
    }
    if (activateFocusedProjectAction()) {
        syncFocusedEncoder();
        return;
    }
    if (applyFocusedProjectStep(1)) {
        syncFocusedEncoder();
        return;
    }
    core::state::project::enterFocusedProjectRow(navigation_);
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::enterFocusedModulator() {
    using core::state::project::ProjectNodeId;
    if (navigation_.currentNode.get() == ProjectNodeId::MODULATORS_ROOT) {
        const auto* source = focusedModulator();
        if (source != nullptr) {
            (void)core::state::project::openProjectModulatorDetail(
                navigation_,
                source->id
            );
        } else {
            (void)core::state::project::openProjectModulatorDestinationPicker(
                navigation_,
                pages_.currentActiveTrack(),
                pages_.currentActivePage(),
                true
            );
        }
        return;
    }

    if (navigation_.currentNode.get() ==
        ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        commitDestinationPickerSelection();
        return;
    }

    if (navigation_.currentNode.get() == ProjectNodeId::MODULATOR_REACH) {
        using namespace core::state::modulation;
        using core::state::project::modulators::ReachChoiceKind;
        const auto* source = focusedModulator();
        if (!source) return;
        const auto& graph = pages_.control.authored.modulation;
        const auto layout =
            core::state::project::modulators::sourceReachChoiceLayout(
                graph,
                source->id
            );
        const auto choice = layout.at(navigation_.focusedRow.get());
        if (choice.kind != ReachChoiceKind::SPLIT_TRACK) {
            const ModulatorReach reach = choice.kind == ReachChoiceKind::PROJECT
                ? ModulatorReach{.kind = ModulatorReachKind::PROJECT}
                : core::state::project::modulators::tightestSourceReach(
                      graph,
                      source->id
                  );
            if (macro_history_.setProjectModulatorReach(
                    pages_,
                    source->id,
                    reach
                )) {
                publishModulatorMutation(false);
                navigation_.setLifecycleFeedback(
                    choice.kind == ReachChoiceKind::PROJECT
                        ? "Reach · Project" : "Reach · Tightest"
                );
            } else {
                navigation_.setLifecycleFeedback(
                    choice.kind == ReachChoiceKind::PROJECT
                        ? "Already Project" : "Already Tightest"
                );
            }
            return;
        }

        const ModulatorReach retainedReach =
            core::state::project::modulators::sourcePartitionReach(
                graph,
                source->id,
                choice.track,
                false
            );
        const ModulatorReach cloneReach =
            core::state::project::modulators::sourcePartitionReach(
                graph,
                source->id,
                choice.track,
                true
            );
        const ModulatorId retainedSourceId = source->id;
        char cloneName[PROJECT_MODULATOR_NAME_CAPACITY]{};
        std::snprintf(
            cloneName,
            sizeof(cloneName),
            "T%u %s",
            static_cast<unsigned>(choice.track + 1U),
            source->name.data()
        );
        const auto split = macro_history_.splitProjectModulatorTrack(
            pages_,
            source->id,
            choice.track,
            cloneName,
            retainedReach,
            cloneReach
        );
        if (!split.changed()) {
            navigation_.setLifecycleFeedback("Split unavailable");
            return;
        }
        while (navigation_.depth.get() > 0U) {
            (void)core::state::project::backProjectNavigation(navigation_);
        }
        auto& liveGraph = pages_.control.authored.modulation;
        for (uint16_t index = 0; index < liveGraph.sourceCount; ++index) {
            if (liveGraph.sources[index].id == retainedSourceId) {
                navigation_.focusedRow.set(static_cast<uint8_t>(index));
                break;
            }
        }
        navigation_.selectedModulator = retainedSourceId;
        publishModulatorMutation(false);
        char feedback[48]{};
        std::snprintf(
            feedback,
            sizeof(feedback),
            "Split T%u · %u dest.",
            static_cast<unsigned>(choice.track + 1U),
            static_cast<unsigned>(choice.destinationCount)
        );
        navigation_.setLifecycleFeedback(feedback);
        return;
    }

    if (navigation_.currentNode.get() == ProjectNodeId::MODULATOR_DESTINATIONS) {
        if (focusedModulationBinding() == nullptr) {
            (void)core::state::project::openProjectModulatorDestinationPicker(
                navigation_,
                pages_.currentActiveTrack(),
                pages_.currentActivePage(),
                false
            );
        }
        return;
    }

    const auto* source = focusedModulator();
    if (!source) return;
    const auto layout = core::state::project::modulators::sourceDetailLayout(
        source->kind
    );
    const auto item = layout.at(navigation_.focusedRow.get());
    if (item == core::state::project::modulators::SourceDetailItem::DESTINATIONS) {
        if (core::state::project::openProjectModulatorDestinations(navigation_)) {
            const auto* first = focusedModulationBinding();
            navigation_.selectedModulationBinding = first
                ? first->id
                : core::state::modulation::ModulationBindingId{};
        }
    } else if (item ==
               core::state::project::modulators::SourceDetailItem::REACH) {
        (void)core::state::project::openProjectModulatorReach(navigation_);
    }
}

FLASHMEM void ProjectHandler::commitDestinationPickerSelection() {
    using namespace core::state::modulation;
    if (navigation_.currentNode.get() !=
        core::state::project::ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        return;
    }
    const bool creating = navigation_.creatingModulatorSource;
    const uint8_t row = navigation_.focusedRow.get();
    const uint8_t track = navigation_.destinationPickerTrack;
    const uint8_t page = navigation_.destinationPickerPage;
    auto& graph = pages_.control.authored.modulation;

    char name[PROJECT_MODULATOR_NAME_CAPACITY]{};
    formatNextProjectLfoName(graph, name, sizeof(name));
    ModulatorLfoDraft sourceDraft{};
    sourceDraft.name = name;
    sourceDraft.parameters.periodTicks = PROJECT_CONTROL_TICKS_PER_BEAT;
    sourceDraft.parameters.shape = ModulatorLfoShape::SINE;
    sourceDraft.parameters.retrigger = ModulatorRetriggerPolicy::TRANSPORT;
    sourceDraft.parameters.timing = ModulatorTimingMode::SYNC;

    if (creating && row == core::state::macro::MACRO_COUNT) {
        sourceDraft.reach = {};
        const auto created = macro_history_.createUnassignedLfo(
            pages_, sourceDraft
        );
        if (!created.changed()) {
            navigation_.setLifecycleFeedback("Source capacity unavailable");
            return;
        }
        (void)core::state::project::backProjectNavigation(navigation_);
        for (uint16_t index = 0; index < graph.sourceCount; ++index) {
            if (graph.sources[index].id == created.sourceId) {
                navigation_.focusedRow.set(static_cast<uint8_t>(index));
                break;
            }
        }
        navigation_.selectedModulator = created.sourceId;
        publishModulatorMutation(false);
        navigation_.setLifecycleFeedback("LFO created · Unassigned");
        return;
    }
    if (row >= core::state::macro::MACRO_COUNT ||
        !pages_.pageData(track, page).isMacroActive(row)) {
        navigation_.setLifecycleFeedback("Create this Macro first");
        return;
    }

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        track,
        page,
        row,
    };
    const auto destination = projectControlDestination(address);
    ModulationBindingDraft binding{};
    binding.destination = destination;
    binding.amountQ15 = 8192;
    binding.application = ModulationApplication::NATURAL;

    ProjectModulationResult begun{};
    ModulatorId targetSource{};
    if (creating) {
        sourceDraft.reach = {
            .kind = ModulatorReachKind::MACRO,
            .track = track,
            .page = page,
            .macro = row,
        };
        begun = macro_history_.beginLfoModulatorAudition(
            pages_, address, sourceDraft, binding
        );
        targetSource = begun.sourceId;
    } else {
        targetSource = navigation_.selectedModulator;
        const auto* source = findProjectModulator(graph, targetSource);
        if (!source) {
            navigation_.setLifecycleFeedback("Source unavailable");
            return;
        }
        if (sourceAlreadyTargets(graph, targetSource, destination)) {
            navigation_.setLifecycleFeedback("Already assigned");
            return;
        }
        const ModulatorReach widened = minimumReachForDestination(
            source->reach,
            destination
        );
        const bool needsWidening =
            !modulatorReachContains(source->reach, destination);
        begun = macro_history_.beginExistingModulatorAudition(
            pages_,
            address,
            targetSource,
            binding,
            needsWidening ? &widened : nullptr
        );
    }
    if (!begun.changed()) {
        navigation_.setLifecycleFeedback("Destination unavailable");
        return;
    }
    if (!macro_history_.commitModulatorAudition(pages_, address)) {
        (void)macro_history_.cancelModulatorAudition(pages_, address);
        navigation_.setLifecycleFeedback("Apply failed");
        return;
    }

    (void)core::state::project::backProjectNavigation(navigation_);
    if (creating) {
        uint16_t sourceIndex = 0;
        while (sourceIndex < graph.sourceCount &&
               graph.sources[sourceIndex].id != targetSource) {
            ++sourceIndex;
        }
        navigation_.focusedRow.set(static_cast<uint8_t>(sourceIndex));
        navigation_.selectedModulator = targetSource;
        (void)core::state::project::openProjectModulatorDetail(
            navigation_, targetSource
        );
        (void)core::state::project::openProjectModulatorDestinations(navigation_);
    }
    const uint16_t destinationCount =
        core::state::project::modulators::sourceDestinationCount(
            graph,
            targetSource
        );
    navigation_.focusedRow.set(
        static_cast<uint8_t>(destinationCount > 0U ? destinationCount - 1U : 0U)
    );
    navigation_.selectedModulationBinding = begun.bindingId;
    publishModulatorMutation(false);
    navigation_.setLifecycleFeedback(
        creating ? "LFO created · +25%" : "Destination added · +25%"
    );
}

FLASHMEM void ProjectHandler::setFocusedValue(float normalized) {
    if (setFocusedProjectValue(normalized)) {
        navigation_.clearLifecycleFeedback();
    }
}

FLASHMEM void ProjectHandler::syncFocusedEncoder() {
    using core::state::project::ProjectNodeId;

    const auto node = navigation_.currentNode.get();
    const uint8_t row = navigation_.focusedRow.get();

    if (node == ProjectNodeId::MODULATORS_ROOT) {
        const auto* source = focusedModulator();
        if (source == nullptr ||
            source->kind != core::state::modulation::ModulatorKind::LFO) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        const auto& lfo = source->parameters.lfo;
        if (lfo.timing == core::state::modulation::ModulatorTimingMode::FREE) {
            const int count = static_cast<int>(
                PROJECT_MODULATOR_FREE_PERIODS_MS.size()
            );
            configureOptDiscrete(
                encoders_,
                count,
                indexToNormalized(
                    projectModulatorFreePeriodIndex(lfo.freePeriodMs),
                    count
                )
            );
        } else {
            configureOptDiscrete(
                encoders_,
                core::ui::macro::lfo_audition::RATE_COUNT,
                indexToNormalized(
                    core::ui::macro::lfo_audition::rateIndex(lfo.periodTicks),
                    core::ui::macro::lfo_audition::RATE_COUNT
                )
            );
        }
        return;
    }

    if (node == ProjectNodeId::MODULATOR_SOURCE_DETAIL) {
        const auto* source = focusedModulator();
        if (!source) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        const auto item = core::state::project::modulators::sourceDetailLayout(
            source->kind
        ).at(row);
        using Item = core::state::project::modulators::SourceDetailItem;
        using namespace core::state::modulation;
        switch (item) {
            case Item::ENABLED:
                configureOptDiscrete(
                    encoders_,
                    2,
                    (source->flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U
                        ? 1.0f : 0.0f
                );
                return;
            case Item::SHAPE:
                configureOptDiscrete(
                    encoders_,
                    core::ui::macro::lfo_audition::SHAPE_COUNT,
                    indexToNormalized(
                        static_cast<int>(source->parameters.lfo.shape),
                        core::ui::macro::lfo_audition::SHAPE_COUNT
                    )
                );
                return;
            case Item::RATE: {
                const auto& lfo = source->parameters.lfo;
                if (lfo.timing == ModulatorTimingMode::FREE) {
                    const int count = static_cast<int>(
                        PROJECT_MODULATOR_FREE_PERIODS_MS.size()
                    );
                    configureOptDiscrete(
                        encoders_,
                        count,
                        indexToNormalized(
                            projectModulatorFreePeriodIndex(lfo.freePeriodMs),
                            count
                        )
                    );
                } else {
                    configureOptDiscrete(
                        encoders_,
                        core::ui::macro::lfo_audition::RATE_COUNT,
                        indexToNormalized(
                            core::ui::macro::lfo_audition::rateIndex(
                                lfo.periodTicks
                            ),
                            core::ui::macro::lfo_audition::RATE_COUNT
                        )
                    );
                }
                return;
            }
            case Item::TIMING:
                configureOptDiscrete(
                    encoders_,
                    2,
                    source->parameters.lfo.timing == ModulatorTimingMode::FREE
                        ? 1.0f : 0.0f
                );
                return;
            case Item::PHASE:
                configureOptDiscrete(
                    encoders_,
                    101,
                    std::clamp(
                        (static_cast<float>(source->parameters.lfo.phaseQ15) +
                         32767.0f) /
                            65534.0f,
                        0.0f,
                        1.0f
                    )
                );
                return;
            case Item::RETRIGGER:
                configureOptDiscrete(
                    encoders_,
                    3,
                    indexToNormalized(
                        static_cast<int>(source->parameters.lfo.retrigger),
                        3
                    )
                );
                return;
            case Item::REACH:
                configureOptDiscrete(
                    encoders_,
                    2,
                    source->reach.kind == ModulatorReachKind::PROJECT
                        ? 1.0f : 0.0f
                );
                return;
            default:
                configureOptDiscrete(encoders_, 1, 0.0f);
                return;
        }
    }

    if (node == ProjectNodeId::MODULATOR_REACH) {
        configureOptDiscrete(encoders_, 1, 0.0f);
        return;
    }

    if (node == ProjectNodeId::MODULATOR_DESTINATIONS) {
        const auto* binding = focusedModulationBinding();
        if (!binding) {
            configureOptDiscrete(encoders_, 1, 0.0f);
            return;
        }
        configureOptDiscrete(
            encoders_,
            201,
            std::clamp(
                (static_cast<float>(binding->amountQ15) / 32767.0f + 1.0f) *
                    0.5f,
                0.0f,
                1.0f
            )
        );
        return;
    }

    if (node == ProjectNodeId::MODULATOR_DESTINATION_PICKER) {
        configureOptDiscrete(encoders_, 1, 0.0f);
        return;
    }

    if (node == ProjectNodeId::MUSIC_SCALE && row <= 2) {
        const int count = sequencer_settings_.choiceCount(row);
        if (count > 0) {
            configureOptDiscrete(
                encoders_,
                count,
                indexToNormalized(sequencer_settings_.currentChoiceIndex(row), count)
            );
        }
        return;
    }

    if (node == ProjectNodeId::MUSIC_ROOT && row == 3) {
        configureOptDiscrete(
            encoders_,
            project::PROJECT_STEP_PASTE_MODE_COUNT,
            indexToNormalized(
                static_cast<int>(navigation_.stepPasteMode),
                project::PROJECT_STEP_PASTE_MODE_COUNT
            )
        );
        return;
    }

    if (node == ProjectNodeId::TRANSPORT_ROOT) {
        switch (row) {
            case 0:
                configureOptContinuous(
                    encoders_,
                    tempoToNormalized(status_bar_.tempo.get()),
                    normalizedTurnsForStepRate(
                        project::PROJECT_TEMPO_RANGE_STEPS,
                        PROJECT_OPT_TEMPO_STEPS_PER_TURN
                    )
                );
                return;
            case 1:
                configureOptDiscrete(
                    encoders_,
                    project::PROJECT_SWING_STEPS,
                    indexToNormalized(navigation_.transportSwingPercent, project::PROJECT_SWING_STEPS),
                    normalizedTurnsForStepRate(
                        project::PROJECT_SWING_STEPS,
                        PROJECT_OPT_PERCENT_STEPS_PER_TURN
                    )
                );
                return;
            case 2:
                configureOptDiscrete(
                    encoders_,
                    3,
                    indexToNormalized(midiSyncModeIndex(midi_sync_.mode.get()), 3)
                );
                return;
            case 3:
                configureOptDiscrete(
                    encoders_,
                    project::PROJECT_RUN_MODE_COUNT,
                    indexToNormalized(navigation_.transportRunMode, project::PROJECT_RUN_MODE_COUNT)
                );
                return;
            default:
                return;
        }
    }

    if (node == ProjectNodeId::STORAGE_ROOT) {
        switch (row) {
            case 6:
                configureOptDiscrete(encoders_, 2, navigation_.autosaveEnabled ? 1.0f : 0.0f);
                return;
            default:
                return;
        }
    }

    if (node == ProjectNodeId::ROUTING_ROOT &&
        row < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
        const uint8_t activeTrack = sequencer_tracks_.activeTrackIndex();
        const uint8_t channel = (row == activeTrack)
            ? sequencer_.pattern.midiChannel.get()
            : sequencer_tracks_.track(row).midiChannel.get();
        configureOptDiscrete(
            encoders_,
            project::PROJECT_MIDI_CHANNEL_COUNT,
            indexToNormalized(channel, project::PROJECT_MIDI_CHANNEL_COUNT)
        );
        return;
    }

    if (isProjectNameEditorNode(node)) {
        navigation_.projectNameOptRawPosition = 0.0f;
        navigation_.projectNameOptRowAccumulator = 0.0f;
        configureOptRaw(encoders_);
        return;
    }
}


}  // namespace core::handler

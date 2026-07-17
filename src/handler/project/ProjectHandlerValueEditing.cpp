#include "handler/project/ProjectHandlerInternals.hpp"

#include <algorithm>

#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "ui/macro/MacroLfoAuditionModel.hpp"
#include "state/project/ProjectModulatorMenuModel.hpp"

#include <cmath>
#include <utility>

#include "handler/sequencer/SequencerFullBankHistoryUtils.hpp"

namespace core::handler {

using namespace project_handler_internal;

FLASHMEM bool ProjectHandler::applyFocusedProjectStep(int steps) {
    if (steps == 0) return false;
    const bool applied = applyFocusedMusicRootStep(steps) ||
                         applyFocusedMusicScaleStep(steps) ||
                         applyFocusedTransportStep(steps) ||
                         applyFocusedStorageStep(steps) ||
                         applyFocusedRoutingStep(steps) ||
                         applyFocusedNameEditorStep(steps);
    if (applied) {
        navigation_.clearLifecycleFeedback();
    }
    return applied;
}

FLASHMEM bool ProjectHandler::applyFocusedMusicRootStep(int steps) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::MUSIC_ROOT) {
        return false;
    }

    if (steps == 0 || navigation_.focusedRow.get() != 3) return false;

    const int current = static_cast<int>(navigation_.stepPasteMode);
    const int next = wrapIndex(current + steps, project::PROJECT_STEP_PASTE_MODE_COUNT);
    if (next == current) return true;

    navigation_.stepPasteMode =
        project::sanitizeProjectStepPasteMode(static_cast<uint8_t>(next));
    navigation_.notifyContentChanged();
    lifecycle_.markProjectMutated();
    return true;
}

FLASHMEM bool ProjectHandler::applyFocusedMusicScaleStep(int steps) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::MUSIC_SCALE) {
        return false;
    }

    const uint8_t row = navigation_.focusedRow.get();
    if (row > 2) return false;

    const int count = sequencer_settings_.choiceCount(row);
    if (count <= 0) return false;

    const int current = sequencer_settings_.currentChoiceIndex(row);
    const int next = wrapIndex(current + steps, count);
    if (next == current) return true;

    history_.commitCoalescedPatternEdit();
    auto change = captureSequencerFullBankHistoryBefore(sequencer_tracks_, sequencer_);

    sequencer_settings_.applyChoice(row, next);

    if (change && captureSequencerFullBankHistoryAfter(sequencer_tracks_, sequencer_, *change)) {
        recordSequencerFullBankHistoryChange(
            history_,
            std::move(change),
            core::state::sequencer::SequencerHistoryDescriptor{
                .kind = core::state::sequencer::SequencerHistoryActionKind::ProjectScaleSettings,
            }
        );
    }
    return true;
}

FLASHMEM bool ProjectHandler::applyFocusedTransportStep(int steps) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::TRANSPORT_ROOT) {
        return false;
    }

    if (steps == 0) return false;

    const uint8_t row = navigation_.focusedRow.get();
    switch (row) {
        case 0: {
            const int current = project::roundedProjectTempoBpm(status_bar_.tempo.get());
            const int next = clampInt(
                current + steps,
                static_cast<int>(project::PROJECT_TEMPO_MIN_BPM),
                static_cast<int>(project::PROJECT_TEMPO_MAX_BPM)
            );
            if (next == current) return true;
            const auto nextTempo = static_cast<float>(next);
            status_bar_.tempo.set(nextTempo);
            if (!status_bar_.tempoLocked.get()) {
                status_bar_.tempoDisplay.set(nextTempo);
            }
            lifecycle_.markProjectMutated();
            return true;
        }
        case 1: {
            const int current = navigation_.transportSwingPercent;
            const int next = clampInt(current + steps, 0, project::PROJECT_SWING_MAX_PERCENT);
            if (next == current) return true;
            navigation_.transportSwingPercent = static_cast<uint8_t>(next);
            navigation_.notifyContentChanged();
            lifecycle_.markProjectMutated();
            return true;
        }
        case 2: {
            const int current = midiSyncModeIndex(midi_sync_.mode.get());
            const int next = wrapIndex(current + steps, 3);
            midi_sync_.mode.set(midiSyncModeAt(next));
            return true;
        }
        case 3: {
            const int current = navigation_.transportRunMode;
            const int next = wrapIndex(current + steps, project::PROJECT_RUN_MODE_COUNT);
            navigation_.transportRunMode = static_cast<uint8_t>(next);
            navigation_.notifyContentChanged();
            lifecycle_.markProjectMutated();
            return true;
        }
        default:
            return false;
    }
}

FLASHMEM bool ProjectHandler::applyFocusedStorageStep(int steps) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::STORAGE_ROOT) {
        return false;
    }

    if (steps == 0) return false;

    const uint8_t row = navigation_.focusedRow.get();
    switch (row) {
        case 6:
            navigation_.autosaveEnabled = !navigation_.autosaveEnabled;
            navigation_.notifyContentChanged();
            return true;
        default:
            return false;
    }
}

FLASHMEM bool ProjectHandler::applyFocusedNameEditorStep(int steps) {
    if (!isProjectNameEditorNode(navigation_.currentNode.get()) || steps == 0) {
        return false;
    }

    navigation_.projectNameKeyIndex = core::state::project::projectNameKeyboardMoveColumn(
        navigation_.projectNameKeyIndex,
        steps
    );
    navigation_.notifyContentChanged();
    return true;
}

FLASHMEM bool ProjectHandler::applyFocusedRoutingStep(int steps) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::ROUTING_ROOT) {
        return false;
    }

    if (steps == 0) return false;

    const uint8_t track = navigation_.focusedRow.get();
    if (track >= core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
        return false;
    }

    const uint8_t activeTrack = sequencer_tracks_.activeTrackIndex();
    const uint8_t current = (track == activeTrack)
        ? sequencer_.pattern.midiChannel.get()
        : sequencer_tracks_.track(track).midiChannel.get();
    const auto next = static_cast<uint8_t>(
        wrapIndex(current + steps, project::PROJECT_MIDI_CHANNEL_COUNT)
    );
    if (next == current) return true;

    sequencer_tracks_.track(track).midiChannel.set(next);
    if (track == activeTrack) {
        sequencer_.pattern.midiChannel.set(next);
    }
    navigation_.notifyContentChanged();
    lifecycle_.markProjectMutated();
    return true;
}

FLASHMEM bool ProjectHandler::setFocusedProjectValue(float normalized) {
    return setFocusedMusicRootValue(normalized) ||
           setFocusedMusicScaleValue(normalized) ||
           setFocusedTransportValue(normalized) ||
           setFocusedStorageValue(normalized) || setFocusedRoutingValue(normalized) ||
           setFocusedModulatorValue(normalized) ||
           setFocusedNameEditorValue(normalized);
}

FLASHMEM bool ProjectHandler::setFocusedModulatorValue(float normalized) {
    using namespace core::state::modulation;
    using Item = core::state::project::modulators::SourceDetailItem;
    if (navigation_.currentNode.get() ==
        core::state::project::ProjectNodeId::MODULATOR_DESTINATIONS) {
        auto* binding = focusedModulationBinding();
        if (!binding) return false;
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            binding->destination.track,
            binding->destination.page,
            binding->destination.macro,
        };
        const float depth = clampNormalized(normalized) * 2.0f - 1.0f;
        if (macro_history_.setModulationBindingDepthCoalesced(
                pages_, address, binding->id, depth
            )) {
            publishModulatorMutation(false);
        }
        return true;
    }
    auto* source = focusedModulator();
    if (!source) return false;

    const auto node = navigation_.currentNode.get();
    Item item = Item::RATE;
    if (node == core::state::project::ProjectNodeId::MODULATOR_SOURCE_DETAIL) {
        item = core::state::project::modulators::sourceDetailLayout(source->kind).at(
            navigation_.focusedRow.get()
        );
    } else if (node ==
               core::state::project::ProjectNodeId::MODULATOR_SOURCE_OPTIONS) {
        item = core::state::project::modulators::sourceOptionsLayout(source->kind).at(
            navigation_.focusedRow.get()
        );
    } else if (node != core::state::project::ProjectNodeId::MODULATORS_ROOT) {
        return false;
    }

    const float value = clampNormalized(normalized);
    if (item == Item::ENABLED) {
        const bool enabled = value >= 0.5f;
        if (macro_history_.setProjectModulatorEnabled(
                pages_, source->id, enabled
            )) {
            publishModulatorMutation(false);
        }
        return true;
    }
    if (item == Item::REACH) {
        const ModulatorReach reach = value >= 0.5f
            ? ModulatorReach{.kind = ModulatorReachKind::PROJECT}
            : core::state::project::modulators::tightestSourceReach(
                  pages_.control.authored.modulation,
                  source->id
              );
        if (macro_history_.setProjectModulatorReach(
                pages_, source->id, reach
            )) {
            publishModulatorMutation(false);
        }
        return true;
    }
    if (source->kind != ModulatorKind::LFO) return false;

    auto parameters = source->parameters.lfo;
    switch (item) {
        case Item::SHAPE:
            parameters.shape = static_cast<ModulatorLfoShape>(
                normalizedToIndex(
                    value,
                    core::ui::macro::lfo_audition::SHAPE_COUNT
                )
            );
            break;
        case Item::RATE:
            if (parameters.timing == ModulatorTimingMode::FREE) {
                parameters.freePeriodMs = PROJECT_MODULATOR_FREE_PERIODS_MS[
                    static_cast<size_t>(normalizedToIndex(
                        value,
                        static_cast<int>(PROJECT_MODULATOR_FREE_PERIODS_MS.size())
                    ))
                ];
            } else {
                parameters.periodTicks =
                    core::ui::macro::lfo_audition::RATE_PERIOD_TICKS[
                        static_cast<size_t>(normalizedToIndex(
                            value,
                            core::ui::macro::lfo_audition::RATE_COUNT
                        ))
                    ];
            }
            break;
        case Item::TIMING:
            parameters.timing = value >= 0.5f
                ? ModulatorTimingMode::FREE
                : ModulatorTimingMode::SYNC;
            break;
        case Item::PHASE: {
            const int32_t phase = static_cast<int32_t>(value * 65534.0f + 0.5f) -
                32767;
            parameters.phaseQ15 = static_cast<int16_t>(
                std::clamp<int32_t>(phase, -32767, 32767)
            );
            break;
        }
        case Item::RETRIGGER:
            if (parameters.retrigger ==
                ModulatorRetriggerPolicy::EXPLICIT_TRIGGER) {
                return false;
            }
            parameters.retrigger = static_cast<ModulatorRetriggerPolicy>(
                normalizedToIndex(value, 2)
            );
            break;
        default:
            return false;
    }
    if (macro_history_.setProjectLfoParametersCoalesced(
            pages_, source->id, parameters
        )) {
        publishModulatorMutation(false);
    }
    return true;
}

FLASHMEM bool ProjectHandler::setFocusedNameEditorValue(float normalized) {
    if (!isProjectNameEditorNode(navigation_.currentNode.get())) {
        return false;
    }

    const float delta = normalized - navigation_.projectNameOptRawPosition;
    navigation_.projectNameOptRawPosition = normalized;
    if (delta == 0.0f) return true;

    navigation_.projectNameOptRowAccumulator += delta / PROJECT_NAME_KEYBOARD_OPT_TICKS_PER_ROW;
    const float absolute = std::fabs(navigation_.projectNameOptRowAccumulator);
    if (absolute < 1.0f) return true;

    const int steps = static_cast<int>(absolute);
    const bool increasing = navigation_.projectNameOptRowAccumulator > 0.0f;
    navigation_.projectNameOptRowAccumulator +=
        increasing
            ? -static_cast<float>(steps)
            : static_cast<float>(steps);

    const int rowDelta = increasing ? -steps : steps;
    const auto next = core::state::project::projectNameKeyboardMoveRow(
        navigation_.projectNameKeyIndex,
        rowDelta
    );
    if (next == navigation_.projectNameKeyIndex) return true;

    navigation_.projectNameKeyIndex = next;
    navigation_.notifyContentChanged();
    return true;
}

FLASHMEM bool ProjectHandler::setFocusedMusicRootValue(float normalized) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::MUSIC_ROOT) {
        return false;
    }

    if (navigation_.focusedRow.get() != 3) return false;

    const int current = static_cast<int>(navigation_.stepPasteMode);
    const int next = normalizedToIndex(normalized, project::PROJECT_STEP_PASTE_MODE_COUNT);
    if (next == current) return true;

    navigation_.stepPasteMode =
        project::sanitizeProjectStepPasteMode(static_cast<uint8_t>(next));
    navigation_.notifyContentChanged();
    lifecycle_.markProjectMutated();
    return true;
}

FLASHMEM bool ProjectHandler::setFocusedMusicScaleValue(float normalized) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::MUSIC_SCALE) {
        return false;
    }

    const uint8_t row = navigation_.focusedRow.get();
    if (row > 2) return false;

    const int count = sequencer_settings_.choiceCount(row);
    if (count <= 0) return false;

    const int current = sequencer_settings_.currentChoiceIndex(row);
    const int next = normalizedToIndex(normalized, count);
    if (next == current) return true;

    history_.commitCoalescedPatternEdit();
    auto change = captureSequencerFullBankHistoryBefore(sequencer_tracks_, sequencer_);

    sequencer_settings_.applyChoice(row, next);

    if (change && captureSequencerFullBankHistoryAfter(sequencer_tracks_, sequencer_, *change)) {
        recordSequencerFullBankHistoryChange(
            history_,
            std::move(change),
            core::state::sequencer::SequencerHistoryDescriptor{
                .kind = core::state::sequencer::SequencerHistoryActionKind::ProjectScaleSettings,
            }
        );
    }
    return true;
}

FLASHMEM bool ProjectHandler::setFocusedTransportValue(float normalized) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::TRANSPORT_ROOT) {
        return false;
    }

    const uint8_t row = navigation_.focusedRow.get();
    switch (row) {
        case 0: {
            const int current = project::roundedProjectTempoBpm(status_bar_.tempo.get());
            const int next = tempoFromNormalized(normalized);
            if (next == current) return true;
            const auto nextTempo = static_cast<float>(next);
            status_bar_.tempo.set(nextTempo);
            if (!status_bar_.tempoLocked.get()) {
                status_bar_.tempoDisplay.set(nextTempo);
            }
            lifecycle_.markProjectMutated();
            return true;
        }
        case 1: {
            const int current = navigation_.transportSwingPercent;
            const int next = normalizedToIndex(normalized, project::PROJECT_SWING_STEPS);
            if (next == current) return true;
            navigation_.transportSwingPercent = static_cast<uint8_t>(next);
            navigation_.notifyContentChanged();
            lifecycle_.markProjectMutated();
            return true;
        }
        case 2: {
            const int current = midiSyncModeIndex(midi_sync_.mode.get());
            const int next = normalizedToIndex(normalized, 3);
            if (next == current) return true;
            midi_sync_.mode.set(midiSyncModeAt(next));
            return true;
        }
        case 3: {
            const int current = navigation_.transportRunMode;
            const int next = normalizedToIndex(normalized, project::PROJECT_RUN_MODE_COUNT);
            if (next == current) return true;
            navigation_.transportRunMode = static_cast<uint8_t>(next);
            navigation_.notifyContentChanged();
            lifecycle_.markProjectMutated();
            return true;
        }
        default:
            return false;
    }
}

FLASHMEM bool ProjectHandler::setFocusedStorageValue(float normalized) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::STORAGE_ROOT) {
        return false;
    }

    const uint8_t row = navigation_.focusedRow.get();
    switch (row) {
        case 6: {
            const bool next = normalized >= 0.5f;
            if (next == navigation_.autosaveEnabled) return true;
            navigation_.autosaveEnabled = next;
            navigation_.notifyContentChanged();
            return true;
        }
        default:
            return false;
    }
}

FLASHMEM bool ProjectHandler::setFocusedRoutingValue(float normalized) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::ROUTING_ROOT) {
        return false;
    }

    const uint8_t track = navigation_.focusedRow.get();
    if (track >= core::state::sequencer::SequencerTrackBankState::TRACK_COUNT) {
        return false;
    }

    const uint8_t activeTrack = sequencer_tracks_.activeTrackIndex();
    const uint8_t current = (track == activeTrack)
        ? sequencer_.pattern.midiChannel.get()
        : sequencer_tracks_.track(track).midiChannel.get();
    const auto next = static_cast<uint8_t>(
        normalizedToIndex(normalized, project::PROJECT_MIDI_CHANNEL_COUNT)
    );
    if (next == current) return true;

    sequencer_tracks_.track(track).midiChannel.set(next);
    if (track == activeTrack) {
        sequencer_.pattern.midiChannel.set(next);
    }
    navigation_.notifyContentChanged();
    lifecycle_.markProjectMutated();
    return true;
}


}  // namespace core::handler

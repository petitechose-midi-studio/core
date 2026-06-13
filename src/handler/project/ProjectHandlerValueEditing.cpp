#include "handler/project/ProjectHandlerInternals.hpp"

#include <cmath>
#include <utility>

#include "handler/sequencer/SequencerFullBankHistoryUtils.hpp"

namespace core::handler {

using namespace project_handler_internal;

FLASHMEM bool ProjectHandler::applyFocusedProjectStep(int steps) {
    if (steps == 0) return false;
    const bool applied = applyFocusedMusicScaleStep(steps) || applyFocusedTransportStep(steps) ||
                         applyFocusedStorageStep(steps) || applyFocusedRoutingStep(steps) ||
                         applyFocusedNameEditorStep(steps);
    if (applied) {
        navigation_.clearLifecycleFeedback();
    }
    return applied;
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
    return setFocusedMusicScaleValue(normalized) || setFocusedTransportValue(normalized) ||
           setFocusedStorageValue(normalized) || setFocusedRoutingValue(normalized) ||
           setFocusedNameEditorValue(normalized);
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

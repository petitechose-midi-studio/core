#include "handler/project/ProjectHandlerInternals.hpp"

namespace core::handler {

using namespace project_handler_internal;

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
    core::state::project::navigateProjectRows(navigation_, delta);
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::switchTab(float delta) {
    if (delta == 0.0f) return;
    navigation_.clearLifecycleFeedback();
    core::state::project::switchProjectTab(navigation_, signedStepCount(delta));
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::enterFocused() {
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

FLASHMEM void ProjectHandler::setFocusedValue(float normalized) {
    if (setFocusedProjectValue(normalized)) {
        navigation_.clearLifecycleFeedback();
    }
}

FLASHMEM void ProjectHandler::syncFocusedEncoder() {
    using core::state::project::ProjectNodeId;

    const auto node = navigation_.currentNode.get();
    const uint8_t row = navigation_.focusedRow.get();

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

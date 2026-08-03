#include <cmath>

#include <config/PlatformCompat.hpp>

#include "handler/project/ProjectHandlerInternals.hpp"

namespace core::handler {

using namespace project_handler_internal;

namespace {

FLASHMEM bool acceptProjectScaleResult(
    core::state::project::ProjectNavigationState& navigation,
    core::state::sequencer::SequencerPreparedFullBankEditOutcome outcome) {
    using Outcome = core::state::sequencer::SequencerPreparedFullBankEditOutcome;
    switch (outcome) {
        case Outcome::Committed:
        case Outcome::NoChange: return true;
        case Outcome::ResourceUnavailable:
            navigation.setLifecycleFeedback("Memory unavailable - unchanged");
            return false;
        case Outcome::HistoryUnavailable:
            navigation.setLifecycleFeedback("History unavailable - unchanged");
            return false;
        case Outcome::Blocked: return false;
    }
    return false;
}

}  // namespace

FLASHMEM bool ProjectHandler::recordProjectSettingsChange(
    const core::state::project::ProjectSettingsHistorySnapshot& before,
    core::state::project::ProjectSettingsHistoryActionKind kind, uint8_t subject, bool coalesce) {
    const auto after = core::state::project::captureProjectSettingsHistorySnapshot(
        status_bar_, navigation_, midi_sync_);
    if (!settings_history_.record(before, after, kind, subject, coalesce)) {
        (void)core::state::project::applyProjectSettingsHistorySnapshot(status_bar_, navigation_,
                                                                        midi_sync_, before);
        return false;
    }
    if (coalesce) {
        const uint32_t nowMs = time_provider_ ? time_provider_() : 0U;
        settings_gesture_commit_deadline_ms_ = nowMs + ROUTING_GESTURE_IDLE_COMMIT_MS;
    } else {
        endProjectSettingsGesture();
    }
    lifecycle_.markProjectMutated();
    return true;
}

FLASHMEM void ProjectHandler::endProjectSettingsGesture() {
    settings_history_.endCoalescing();
    settings_gesture_commit_deadline_ms_ = 0U;
}

FLASHMEM bool ProjectHandler::applyFocusedProjectStep(int steps) {
    if (steps == 0) return false;
    const bool applied = applyFocusedMusicRootStep(steps) || applyFocusedMusicScaleStep(steps) ||
                         applyFocusedTransportStep(steps) || applyFocusedStorageStep(steps) ||
                         applyFocusedRoutingStep(steps) || applyFocusedNameEditorStep(steps);
    if (applied) { navigation_.clearLifecycleFeedback(); }
    return applied;
}

FLASHMEM bool ProjectHandler::applyFocusedMusicRootStep(int steps) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::MUSIC_ROOT) {
        return false;
    }

    if (steps == 0) return false;

    const uint8_t row = navigation_.focusedRow.get();
    const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
        status_bar_, navigation_, midi_sync_);
    auto kind = core::state::project::ProjectSettingsHistoryActionKind::StepPasteMode;
    uint8_t subject = 0U;
    if (row == 3U) {
        const int current = static_cast<int>(navigation_.stepPasteMode);
        const int next = wrapIndex(current + steps, project::PROJECT_STEP_PASTE_MODE_COUNT);
        if (next == current) return true;
        navigation_.stepPasteMode =
            project::sanitizeProjectStepPasteMode(static_cast<uint8_t>(next));
    } else if (row >= 4U && row < 4U + project::PROJECT_CC_LANE_DEFAULT_COUNT) {
        const uint8_t lane = static_cast<uint8_t>(row - 4U);
        kind = core::state::project::ProjectSettingsHistoryActionKind::CcLaneDefault;
        subject = lane;
        const int current = navigation_.ccLaneDefaultControllers[lane];
        const int next = wrapIndex(current + steps, project::PROJECT_MIDI_CC_COUNT);
        if (next == current) return true;
        navigation_.ccLaneDefaultControllers[lane] = static_cast<uint8_t>(next);
    } else {
        return false;
    }
    navigation_.notifyContentChanged();
    return recordProjectSettingsChange(before, kind, subject, false);
}

FLASHMEM bool ProjectHandler::applyFocusedMusicScaleStep(int steps) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::MUSIC_SCALE) {
        return false;
    }

    const uint8_t row = navigation_.focusedRow.get();
    if (row == 3U || row == 4U) {
        const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
            status_bar_, navigation_, midi_sync_);
        if (row == 3U) {
            navigation_.patternsInheritScale = !navigation_.patternsInheritScale;
        } else {
            navigation_.clipsInheritScale = !navigation_.clipsInheritScale;
        }
        navigation_.notifyContentChanged();
        return recordProjectSettingsChange(
            before,
            row == 3U ? core::state::project::ProjectSettingsHistoryActionKind::PatternsInheritScale
                      : core::state::project::ProjectSettingsHistoryActionKind::ClipsInheritScale,
            0U, false);
    }
    if (row > 2U) return false;

    const int count = sequencer_settings_.choiceCount(row);
    if (count <= 0) return false;

    const int current = sequencer_settings_.currentChoiceIndex(row);
    const int next = wrapIndex(current + steps, count);
    if (next == current) return true;

    const auto result = history_.applyPreparedProjectScaleChoice(
        core::state::sequencer::SequencerPreparedFullBankEditOwner::ProjectScale,
        row,
        next
    );
    return acceptProjectScaleResult(navigation_, result.outcome);
}

FLASHMEM bool ProjectHandler::applyFocusedTransportStep(int steps) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::TRANSPORT_ROOT) {
        return false;
    }

    if (steps == 0) return false;

    const uint8_t row = navigation_.focusedRow.get();
    switch (row) {
        case 0: {
            const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
                status_bar_, navigation_, midi_sync_);
            const int current = project::roundedProjectTempoBpm(status_bar_.tempo.get());
            const int next =
                clampInt(current + steps, static_cast<int>(project::PROJECT_TEMPO_MIN_BPM),
                         static_cast<int>(project::PROJECT_TEMPO_MAX_BPM));
            if (next == current) return true;
            const auto nextTempo = static_cast<float>(next);
            status_bar_.tempo.set(nextTempo);
            if (!status_bar_.tempoLocked.get()) { status_bar_.tempoDisplay.set(nextTempo); }
            return recordProjectSettingsChange(
                before, core::state::project::ProjectSettingsHistoryActionKind::Tempo, 0U, false);
        }
        case 1: {
            const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
                status_bar_, navigation_, midi_sync_);
            const int current = navigation_.transportSwingPercent;
            const int next = clampInt(current + steps, 0, project::PROJECT_SWING_MAX_PERCENT);
            if (next == current) return true;
            navigation_.transportSwingPercent = static_cast<uint8_t>(next);
            navigation_.notifyContentChanged();
            return recordProjectSettingsChange(
                before, core::state::project::ProjectSettingsHistoryActionKind::Swing, 0U, false);
        }
        case 2: {
            const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
                status_bar_, navigation_, midi_sync_);
            const int current = midiSyncModeIndex(midi_sync_.mode.get());
            const int next = wrapIndex(current + steps, 3);
            if (next == current) return true;
            midi_sync_.mode.set(midiSyncModeAt(next));
            return recordProjectSettingsChange(
                before, core::state::project::ProjectSettingsHistoryActionKind::SyncMode, 0U,
                false);
        }
        case 3: {
            const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
                status_bar_, navigation_, midi_sync_);
            const int current = navigation_.transportRunMode;
            const int next = wrapIndex(current + steps, project::PROJECT_RUN_MODE_COUNT);
            if (next == current) return true;
            navigation_.transportRunMode = static_cast<uint8_t>(next);
            navigation_.notifyContentChanged();
            return recordProjectSettingsChange(
                before, core::state::project::ProjectSettingsHistoryActionKind::RunMode, 0U, false);
        }
        default: return false;
    }
}

FLASHMEM bool ProjectHandler::applyFocusedStorageStep(int steps) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::STORAGE_ROOT) {
        return false;
    }

    if (steps == 0) return false;

    const uint8_t row = navigation_.focusedRow.get();
    switch (row) {
        case 6: {
            const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
                status_bar_, navigation_, midi_sync_);
            navigation_.autosaveEnabled = !navigation_.autosaveEnabled;
            navigation_.notifyContentChanged();
            return recordProjectSettingsChange(
                before, core::state::project::ProjectSettingsHistoryActionKind::Autosave, 0U,
                false);
        }
        default: return false;
    }
}

FLASHMEM bool ProjectHandler::applyFocusedNameEditorStep(int steps) {
    if (!isProjectNameEditorNode(navigation_.currentNode.get()) || steps == 0) { return false; }

    navigation_.projectNameKeyIndex =
        core::state::project::projectNameKeyboardMoveColumn(navigation_.projectNameKeyIndex, steps);
    navigation_.notifyContentChanged();
    return true;
}

FLASHMEM bool ProjectHandler::applyFocusedRoutingStep(int steps) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::ROUTING_ROOT) {
        return false;
    }

    if (steps == 0) return false;

    const uint8_t track = navigation_.focusedRow.get();
    if (track >= core::state::project::PROJECT_TRACK_COUNT) { return false; }

    const uint8_t current = core::state::project::projectTrackMidiChannel(project_tracks_, track);
    const auto next =
        static_cast<uint8_t>(wrapIndex(current + steps, project::PROJECT_MIDI_CHANNEL_COUNT));
    if (next == current) return true;

    if (!setRoutingMidiChannel(track, next)) return false;
    navigation_.notifyContentChanged();
    return true;
}

FLASHMEM bool ProjectHandler::setFocusedProjectValue(float normalized) {
    return setFocusedMusicRootValue(normalized) || setFocusedMusicScaleValue(normalized) ||
           setFocusedTransportValue(normalized) || setFocusedStorageValue(normalized) ||
           setFocusedRoutingValue(normalized) || setFocusedModulatorValue(normalized) ||
           setFocusedNameEditorValue(normalized);
}

FLASHMEM bool ProjectHandler::setFocusedNameEditorValue(float normalized) {
    if (!isProjectNameEditorNode(navigation_.currentNode.get())) { return false; }

    const float delta = normalized - navigation_.projectNameOptRawPosition;
    navigation_.projectNameOptRawPosition = normalized;
    if (delta == 0.0f) return true;

    navigation_.projectNameOptRowAccumulator += delta / PROJECT_NAME_KEYBOARD_OPT_TICKS_PER_ROW;
    const float absolute = std::fabs(navigation_.projectNameOptRowAccumulator);
    if (absolute < 1.0f) return true;

    const int steps = static_cast<int>(absolute);
    const bool increasing = navigation_.projectNameOptRowAccumulator > 0.0f;
    navigation_.projectNameOptRowAccumulator +=
        increasing ? -static_cast<float>(steps) : static_cast<float>(steps);

    const int rowDelta = increasing ? -steps : steps;
    const auto next =
        core::state::project::projectNameKeyboardMoveRow(navigation_.projectNameKeyIndex, rowDelta);
    if (next == navigation_.projectNameKeyIndex) return true;

    navigation_.projectNameKeyIndex = next;
    navigation_.notifyContentChanged();
    return true;
}

FLASHMEM bool ProjectHandler::setFocusedMusicRootValue(float normalized) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::MUSIC_ROOT) {
        return false;
    }

    const uint8_t row = navigation_.focusedRow.get();
    const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
        status_bar_, navigation_, midi_sync_);
    auto kind = core::state::project::ProjectSettingsHistoryActionKind::StepPasteMode;
    uint8_t subject = 0U;
    if (row == 3U) {
        const int current = static_cast<int>(navigation_.stepPasteMode);
        const int next = normalizedToIndex(normalized, project::PROJECT_STEP_PASTE_MODE_COUNT);
        if (next == current) return true;
        navigation_.stepPasteMode =
            project::sanitizeProjectStepPasteMode(static_cast<uint8_t>(next));
    } else if (row >= 4U && row < 4U + project::PROJECT_CC_LANE_DEFAULT_COUNT) {
        const uint8_t lane = static_cast<uint8_t>(row - 4U);
        kind = core::state::project::ProjectSettingsHistoryActionKind::CcLaneDefault;
        subject = lane;
        const int current = navigation_.ccLaneDefaultControllers[lane];
        const int next = normalizedToIndex(normalized, project::PROJECT_MIDI_CC_COUNT);
        if (next == current) return true;
        navigation_.ccLaneDefaultControllers[lane] = static_cast<uint8_t>(next);
    } else {
        return false;
    }
    navigation_.notifyContentChanged();
    return recordProjectSettingsChange(before, kind, subject, true);
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

    const auto result = history_.applyPreparedProjectScaleChoice(
        core::state::sequencer::SequencerPreparedFullBankEditOwner::ProjectScale,
        row,
        next
    );
    return acceptProjectScaleResult(navigation_, result.outcome);
}

FLASHMEM bool ProjectHandler::setFocusedTransportValue(float normalized) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::TRANSPORT_ROOT) {
        return false;
    }

    const uint8_t row = navigation_.focusedRow.get();
    switch (row) {
        case 0: {
            const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
                status_bar_, navigation_, midi_sync_);
            const int current = project::roundedProjectTempoBpm(status_bar_.tempo.get());
            const int next = tempoFromNormalized(normalized);
            if (next == current) return true;
            const auto nextTempo = static_cast<float>(next);
            status_bar_.tempo.set(nextTempo);
            if (!status_bar_.tempoLocked.get()) { status_bar_.tempoDisplay.set(nextTempo); }
            return recordProjectSettingsChange(
                before, core::state::project::ProjectSettingsHistoryActionKind::Tempo, 0U, true);
        }
        case 1: {
            const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
                status_bar_, navigation_, midi_sync_);
            const int current = navigation_.transportSwingPercent;
            const int next = normalizedToIndex(normalized, project::PROJECT_SWING_STEPS);
            if (next == current) return true;
            navigation_.transportSwingPercent = static_cast<uint8_t>(next);
            navigation_.notifyContentChanged();
            return recordProjectSettingsChange(
                before, core::state::project::ProjectSettingsHistoryActionKind::Swing, 0U, true);
        }
        case 2: {
            const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
                status_bar_, navigation_, midi_sync_);
            const int current = midiSyncModeIndex(midi_sync_.mode.get());
            const int next = normalizedToIndex(normalized, 3);
            if (next == current) return true;
            midi_sync_.mode.set(midiSyncModeAt(next));
            return recordProjectSettingsChange(
                before, core::state::project::ProjectSettingsHistoryActionKind::SyncMode, 0U, true);
        }
        case 3: {
            const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
                status_bar_, navigation_, midi_sync_);
            const int current = navigation_.transportRunMode;
            const int next = normalizedToIndex(normalized, project::PROJECT_RUN_MODE_COUNT);
            if (next == current) return true;
            navigation_.transportRunMode = static_cast<uint8_t>(next);
            navigation_.notifyContentChanged();
            return recordProjectSettingsChange(
                before, core::state::project::ProjectSettingsHistoryActionKind::RunMode, 0U, true);
        }
        default: return false;
    }
}

FLASHMEM bool ProjectHandler::setFocusedStorageValue(float normalized) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::STORAGE_ROOT) {
        return false;
    }

    const uint8_t row = navigation_.focusedRow.get();
    switch (row) {
        case 6: {
            const auto before = core::state::project::captureProjectSettingsHistorySnapshot(
                status_bar_, navigation_, midi_sync_);
            const bool next = normalized >= 0.5f;
            if (next == navigation_.autosaveEnabled) return true;
            navigation_.autosaveEnabled = next;
            navigation_.notifyContentChanged();
            return recordProjectSettingsChange(
                before, core::state::project::ProjectSettingsHistoryActionKind::Autosave, 0U, true);
        }
        default: return false;
    }
}

FLASHMEM bool ProjectHandler::setFocusedRoutingValue(float normalized) {
    if (navigation_.currentNode.get() != core::state::project::ProjectNodeId::ROUTING_ROOT) {
        return false;
    }

    const uint8_t track = navigation_.focusedRow.get();
    if (track >= core::state::project::PROJECT_TRACK_COUNT) { return false; }

    const uint8_t current = core::state::project::projectTrackMidiChannel(project_tracks_, track);
    const auto next =
        static_cast<uint8_t>(normalizedToIndex(normalized, project::PROJECT_MIDI_CHANNEL_COUNT));
    if (next == current) return true;

    if (!setRoutingMidiChannel(track, next)) return false;
    navigation_.notifyContentChanged();
    return true;
}

FLASHMEM bool ProjectHandler::setRoutingMidiChannel(uint8_t track, uint8_t channel0Based) {
    if (routing_gesture_track_ != core::state::project::PROJECT_TRACK_COUNT &&
        routing_gesture_track_ != track) {
        commitPendingRoutingGesture();
    }

    const bool began = routing_gesture_track_ == core::state::project::PROJECT_TRACK_COUNT;
    if (began) {
        // Do not join a gesture owned by another UI surface.
        if (track_domain_.hasActiveGesture() ||
            !track_domain_.beginGesture(
                core::state::project::ProjectTrackHistoryActionKind::MidiChannel, track)) {
            return false;
        }
        routing_gesture_track_ = track;
    }

    if (!track_domain_.setMidiChannel(track, channel0Based)) {
        if (began) cancelPendingRoutingGesture();
        return false;
    }

    routing_gesture_commit_deadline_ms_ = time_provider_() + ROUTING_GESTURE_IDLE_COMMIT_MS;
    return true;
}

FLASHMEM void ProjectHandler::commitPendingRoutingGesture() {
    if (routing_gesture_track_ == core::state::project::PROJECT_TRACK_COUNT) { return; }
    if (track_domain_.hasActiveGesture()) { (void)track_domain_.endGesture(); }
    routing_gesture_track_ = core::state::project::PROJECT_TRACK_COUNT;
    routing_gesture_commit_deadline_ms_ = 0U;
}

FLASHMEM void ProjectHandler::cancelPendingRoutingGesture() {
    if (routing_gesture_track_ == core::state::project::PROJECT_TRACK_COUNT) { return; }
    if (track_domain_.hasActiveGesture()) { (void)track_domain_.cancelGesture(); }
    routing_gesture_track_ = core::state::project::PROJECT_TRACK_COUNT;
    routing_gesture_commit_deadline_ms_ = 0U;
}

}  // namespace core::handler

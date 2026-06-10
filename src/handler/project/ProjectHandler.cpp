#include "handler/project/ProjectHandler.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/type/TextFormat.hpp>

#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectSlug.hpp"

namespace core::handler {

using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

namespace {

constexpr int TEMPO_MIN_BPM = 20;
constexpr int TEMPO_MAX_BPM = 300;
constexpr int TEMPO_RANGE_STEPS = TEMPO_MAX_BPM - TEMPO_MIN_BPM + 1;
constexpr int TRANSPORT_SWING_MAX = 75;
constexpr int TRANSPORT_SWING_STEPS = TRANSPORT_SWING_MAX + 1;
constexpr int TRANSPORT_RUN_MODE_COUNT = 3;
constexpr int MIDI_CHANNEL_COUNT = 16;
constexpr uint16_t PROJECT_OPT_TICKS_PER_STEP =
    core::handler::sequencer::input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP;
constexpr float PROJECT_OPT_NORMALIZED_TURNS =
    core::handler::sequencer::input_utils::DEFAULT_NORMALIZED_TURNS;
constexpr float PROJECT_OPT_TEMPO_STEPS_PER_TURN = 24.0f;
constexpr float PROJECT_OPT_PERCENT_STEPS_PER_TURN = 18.0f;
constexpr float PROJECT_NAME_KEYBOARD_OPT_TICKS_PER_ROW =
    (600.0f * 4.0f) /
    static_cast<float>(core::state::project::PROJECT_NAME_KEYBOARD_ROW_COUNT);

FLASHMEM int signedStepCount(float delta) {
    if (delta == 0.0f) return 0;
    const float absolute = delta > 0.0f ? delta : -delta;
    int magnitude = static_cast<int>(absolute);
    if (magnitude < 1) magnitude = 1;
    return delta > 0.0f ? magnitude : -magnitude;
}

FLASHMEM int clampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

FLASHMEM int wrapIndex(int value, int count) {
    if (count <= 0) return 0;
    int wrapped = value % count;
    if (wrapped < 0) wrapped += count;
    return wrapped;
}

FLASHMEM float clampNormalized(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

FLASHMEM int normalizedToIndex(float normalized, int count) {
    if (count <= 1) return 0;
    const float value = clampNormalized(normalized);
    return std::clamp(
        static_cast<int>(value * static_cast<float>(count - 1) + 0.5f),
        0,
        count - 1
    );
}

FLASHMEM float indexToNormalized(int index, int count) {
    if (count <= 1) return 0.0f;
    const int clamped = clampInt(index, 0, count - 1);
    return static_cast<float>(clamped) / static_cast<float>(count - 1);
}

FLASHMEM bool isProjectNameEditorNode(core::state::project::ProjectNodeId node) {
    return node == core::state::project::ProjectNodeId::SAVE_AS_PROJECT_NAME ||
           node == core::state::project::ProjectNodeId::RENAME_PROJECT_NAME;
}

FLASHMEM char selectedProjectNameKey(
    const core::state::project::ProjectNavigationState& navigation
) {
    char key = core::state::project::projectNameKeyboardCellAt(
        navigation.projectNameKeyIndex
    ).character;
    if (navigation.projectNameShiftActive && key >= 'a' && key <= 'z') {
        key = static_cast<char>(key - 'a' + 'A');
    }
    return key;
}

FLASHMEM bool appendProjectNameChar(
    core::state::project::ProjectNavigationState& navigation,
    char character
) {
    if (character == '\0') return false;
    char* buffer = navigation.editingProjectSlug.data();
    const size_t length = std::strlen(buffer);
    if (length >= core::state::project::PROJECT_SLUG_MAX_LENGTH) return false;
    buffer[length] = character;
    buffer[length + 1U] = '\0';
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM bool appendProjectNameKey(core::state::project::ProjectNavigationState& navigation) {
    return appendProjectNameChar(navigation, selectedProjectNameKey(navigation));
}

FLASHMEM bool appendProjectNameSpace(core::state::project::ProjectNavigationState& navigation) {
    return appendProjectNameChar(navigation, ' ');
}

FLASHMEM bool backspaceProjectName(core::state::project::ProjectNavigationState& navigation) {
    char* buffer = navigation.editingProjectSlug.data();
    const size_t length = std::strlen(buffer);
    if (length == 0) return true;
    buffer[length - 1U] = '\0';
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM bool clearProjectName(core::state::project::ProjectNavigationState& navigation) {
    if (navigation.editingProjectSlug[0] == '\0') return true;
    navigation.editingProjectSlug = {};
    navigation.notifyContentChanged();
    return true;
}

FLASHMEM int roundedTempo(float tempoBpm) {
    if (tempoBpm < 0.0f) return 0;
    return static_cast<int>(tempoBpm + 0.5f);
}

FLASHMEM int tempoFromNormalized(float normalized) {
    return TEMPO_MIN_BPM + normalizedToIndex(normalized, TEMPO_RANGE_STEPS);
}

FLASHMEM float tempoToNormalized(float tempoBpm) {
    const int tempo = clampInt(roundedTempo(tempoBpm), TEMPO_MIN_BPM, TEMPO_MAX_BPM);
    return indexToNormalized(tempo - TEMPO_MIN_BPM, TEMPO_RANGE_STEPS);
}

FLASHMEM float normalizedTurnsForStepRate(int stepCount, float stepsPerTurn) {
    if (stepCount <= 1 || stepsPerTurn <= 0.0f) return PROJECT_OPT_NORMALIZED_TURNS;
    return static_cast<float>(stepCount - 1) / stepsPerTurn;
}

FLASHMEM int midiSyncModeIndex(core::state::MidiSyncMode mode) {
    switch (mode) {
        case core::state::MidiSyncMode::MASTER:
            return 0;
        case core::state::MidiSyncMode::SLAVE:
            return 1;
        case core::state::MidiSyncMode::AUTO:
        default:
            return 2;
    }
}

FLASHMEM core::state::MidiSyncMode midiSyncModeAt(int index) {
    switch (wrapIndex(index, 3)) {
        case 0:
            return core::state::MidiSyncMode::MASTER;
        case 1:
            return core::state::MidiSyncMode::SLAVE;
        case 2:
        default:
            return core::state::MidiSyncMode::AUTO;
    }
}

FLASHMEM const char* projectLifecycleFailureLabel(
    ProjectLifecycleDomainServices::Status status,
    const char* fallback
) {
    using Status = ProjectLifecycleDomainServices::Status;
    switch (status) {
        case Status::UNAVAILABLE:
            return "Storage unavailable";
        case Status::INVALID_ARGUMENT:
            return "Invalid project";
        case Status::ALREADY_EXISTS:
            return "Name exists";
        case Status::SAVE_FAILED:
            return "Save failed";
        case Status::LOAD_FAILED:
            return "Load failed";
        case Status::LIST_FAILED:
            return "List failed";
        case Status::PARTIAL_LOAD:
        case Status::OK:
        default:
            return fallback;
    }
}

FLASHMEM void formatProjectLifecycleFeedback(
    char* out,
    size_t outSize,
    const char* verb,
    const char* projectId
) {
    if (!out || outSize == 0U) return;
    size_t pos = oc::type::text::appendString(out, outSize, 0, verb);
    if (projectId && projectId[0] != '\0') {
        pos = oc::type::text::appendChar(out, outSize, pos, ' ');
        pos = oc::type::text::appendString(out, outSize, pos, projectId);
    }
    oc::type::text::terminate(out, outSize, pos);
}

FLASHMEM void configureOptContinuous(oc::api::EncoderAPI& encoders,
                                     float position,
                                     float normalizedTurns = PROJECT_OPT_NORMALIZED_TURNS) {
    encoders.setMode(EncoderID::OPT, oc::interface::EncoderMode::NORMALIZED);
    encoders.setDiscreteTicksPerStep(EncoderID::OPT, PROJECT_OPT_TICKS_PER_STEP);
    encoders.setNormalizedTurns(EncoderID::OPT, normalizedTurns);
    encoders.setContinuous(EncoderID::OPT);
    encoders.setPosition(EncoderID::OPT, clampNormalized(position));
}

FLASHMEM void configureOptRaw(oc::api::EncoderAPI& encoders) {
    encoders.setMode(EncoderID::OPT, oc::interface::EncoderMode::RAW);
    encoders.setPosition(EncoderID::OPT, 0.0f);
}

FLASHMEM void configureOptDiscrete(oc::api::EncoderAPI& encoders,
                                   int stepCount,
                                   float position,
                                   float normalizedTurns = PROJECT_OPT_NORMALIZED_TURNS) {
    encoders.setMode(EncoderID::OPT, oc::interface::EncoderMode::NORMALIZED);
    encoders.setDiscreteTicksPerStep(EncoderID::OPT, PROJECT_OPT_TICKS_PER_STEP);
    encoders.setNormalizedTurns(EncoderID::OPT, normalizedTurns);
    encoders.setDiscreteSteps(
        EncoderID::OPT,
        static_cast<uint8_t>(clampInt(stepCount, 1, 255))
    );
    encoders.setPosition(EncoderID::OPT, clampNormalized(position));
}

}  // namespace

FLASHMEM ProjectHandler::ProjectHandler(StateRefs state,
                                        SequencerSettingsDomainServices sequencerSettings,
                                        oc::api::EncoderAPI& encoders,
                                        oc::api::ButtonAPI& buttons,
                                        oc::type::ScopeID projectViewScope)
    : overlays_(state.overlays)
    , navigation_(state.navigation)
    , sequencer_(state.sequencer)
    , sequencer_tracks_(state.sequencerTracks)
    , status_bar_(state.statusBar)
    , midi_sync_(state.midiSync)
    , history_(state.history)
    , lifecycle_(state.lifecycle)
    , sequencer_settings_(sequencerSettings)
    , encoders_(encoders)
    , buttons_(buttons)
    , project_view_scope_(projectViewScope) {
    setupBindings();
}

FLASHMEM void ProjectHandler::setupBindings() {
    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(project_view_scope_)
        .when([this]() { return regularProjectInputActive(); })
        .then([this](float delta) { navigate(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(project_view_scope_)
        .when([this]() { return regularProjectInputActive(); })
        .then([this]() { enterFocused(); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   !core::state::project::projectNavigationAtRoot(navigation_);
        })
        .then([this]() { back(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .scope(project_view_scope_)
        .when([this]() {
            return canHandleProjectInput() &&
                   !projectConfirmationActive() &&
                   !isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() { enterPhysicalHoldLayer(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() { enterProjectNameShift(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return canHandleProjectInput() && navigation_.projectNameShiftActive;
        })
        .then([this]() { leaveProjectNameShift(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(project_view_scope_)
        .when([this]() { return physicalHoldActive(); })
        .then([this]() { leavePhysicalHoldLayer(); });

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(project_view_scope_)
        .when([this]() { return physicalHoldActive(); })
        .then([this](float delta) { switchTab(delta); });

    encoders_.encoder(EncoderID::OPT)
        .turn()
        .scope(project_view_scope_)
        .when([this]() { return regularProjectInputActive(); })
        .then([this](float normalized) { setFocusedValue(normalized); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(project_view_scope_)
        .when([this]() { return physicalHoldActive(); })
        .then([this]() { consumeUndo(); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(project_view_scope_)
        .when([this]() { return physicalHoldActive(); })
        .then([this]() { consumeRedo(); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() { backspaceProjectName(navigation_); });

    buttons_.button(ButtonID::LEFT_BOTTOM)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() { clearProjectName(navigation_); });

    buttons_.button(ButtonID::BOTTOM_CENTER)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() {
            if (!appendProjectNameSpace(navigation_)) {
                navigation_.setLifecycleFeedback("Name too long");
            }
        });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(project_view_scope_)
        .when([this]() {
            return regularProjectInputActive() &&
                   isProjectNameEditorNode(navigation_.currentNode.get());
        })
        .then([this]() { commitProjectNameEditor(); });
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

FLASHMEM void ProjectHandler::enterProjectNameShift() {
    if (navigation_.projectNameShiftActive) return;
    navigation_.projectNameShiftActive = true;
    navigation_.notifyContentChanged();
}

FLASHMEM void ProjectHandler::leaveProjectNameShift() {
    if (!navigation_.projectNameShiftActive) return;
    navigation_.projectNameShiftActive = false;
    navigation_.notifyContentChanged();
}

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
            const int current = roundedTempo(status_bar_.tempo.get());
            const int next = clampInt(current + steps, TEMPO_MIN_BPM, TEMPO_MAX_BPM);
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
            const int next = clampInt(current + steps, 0, TRANSPORT_SWING_MAX);
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
            const int next = wrapIndex(current + steps, TRANSPORT_RUN_MODE_COUNT);
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
    const auto next = static_cast<uint8_t>(wrapIndex(current + steps, MIDI_CHANNEL_COUNT));
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
            const int current = roundedTempo(status_bar_.tempo.get());
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
            const int next = normalizedToIndex(normalized, TRANSPORT_SWING_STEPS);
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
            const int next = normalizedToIndex(normalized, TRANSPORT_RUN_MODE_COUNT);
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
    const auto next = static_cast<uint8_t>(normalizedToIndex(normalized, MIDI_CHANNEL_COUNT));
    if (next == current) return true;

    sequencer_tracks_.track(track).midiChannel.set(next);
    if (track == activeTrack) {
        sequencer_.pattern.midiChannel.set(next);
    }
    navigation_.notifyContentChanged();
    lifecycle_.markProjectMutated();
    return true;
}

FLASHMEM bool ProjectHandler::loadProjectWithFeedback(const char* projectId) {
    const auto result = lifecycle_.loadProject(projectId);
    char feedback[32] = {};
    const char* verb = result.status == ProjectLifecycleDomainServices::Status::PARTIAL_LOAD
        ? "Loaded partial"
        : (result.success() ? "Loaded" : projectLifecycleFailureLabel(result.status, "Load failed"));
    formatProjectLifecycleFeedback(feedback, sizeof(feedback), verb, projectId);
    navigation_.setLifecycleFeedback(feedback);
    if (result.success()) {
        OC_LOG_INFO("[Project] load {} bytes={}", projectId, result.bytes);
    } else {
        OC_LOG_WARN("[Project] load {} failed status={}",
                    projectId,
                    static_cast<unsigned>(result.status));
    }
    return result.success();
}

FLASHMEM bool ProjectHandler::saveCurrentAndLoadProjectWithFeedback(const char* projectId) {
    const char* currentProjectId = lifecycle_.currentProjectId();
    const auto saved = lifecycle_.saveCurrentProject();
    if (!saved.success()) {
        char feedback[32] = {};
        formatProjectLifecycleFeedback(
            feedback,
            sizeof(feedback),
            projectLifecycleFailureLabel(saved.status, "Save failed"),
            currentProjectId
        );
        navigation_.setLifecycleFeedback(feedback);
        OC_LOG_WARN("[Project] save {} before load failed status={}",
                    currentProjectId,
                    static_cast<unsigned>(saved.status));
        return false;
    }

    OC_LOG_INFO("[Project] save {} before load bytes={}", currentProjectId, saved.bytes);
    return loadProjectWithFeedback(projectId);
}

FLASHMEM bool ProjectHandler::saveAsAndLoadProjectWithFeedback(const char* projectId) {
    const auto saved = lifecycle_.saveAsNextProject();
    const char* savedProjectId = lifecycle_.currentProjectId();
    if (!saved.success()) {
        char feedback[32] = {};
        formatProjectLifecycleFeedback(
            feedback,
            sizeof(feedback),
            projectLifecycleFailureLabel(saved.status, "Save As failed"),
            savedProjectId
        );
        navigation_.setLifecycleFeedback(feedback);
        OC_LOG_WARN("[Project] save-as before load failed status={}",
                    static_cast<unsigned>(saved.status));
        return false;
    }

    OC_LOG_INFO("[Project] save-as {} before load bytes={}", savedProjectId, saved.bytes);
    return loadProjectWithFeedback(projectId);
}

FLASHMEM bool ProjectHandler::saveAndResetProjectWithFeedback(bool saveAsNew) {
    const auto saved = saveAsNew
        ? lifecycle_.saveAsNextProject()
        : lifecycle_.saveCurrentProject();
    const char* savedProjectId = lifecycle_.currentProjectId();
    char feedback[32] = {};
    const char* verb = saved.success()
        ? "Saved"
        : projectLifecycleFailureLabel(saved.status, saveAsNew ? "Save As failed" : "Save failed");
    formatProjectLifecycleFeedback(feedback, sizeof(feedback), verb, savedProjectId);

    if (!saved.success()) {
        navigation_.setLifecycleFeedback(feedback);
        OC_LOG_WARN("[Project] save before reset failed status={}",
                    static_cast<unsigned>(saved.status));
        return false;
    }

    OC_LOG_INFO("[Project] save {} before reset bytes={}", savedProjectId, saved.bytes);
    resetProject();
    navigation_.setLifecycleFeedback(feedback);
    return true;
}

FLASHMEM bool ProjectHandler::commitProjectNameEditor() {
    const auto node = navigation_.currentNode.get();
    if (!isProjectNameEditorNode(node)) return false;

    const char* slug = navigation_.editingProjectSlug.data();
    if (!core::state::project::validProjectSlug(slug)) {
        navigation_.setLifecycleFeedback("Invalid name");
        return true;
    }

    const bool rename = node == core::state::project::ProjectNodeId::RENAME_PROJECT_NAME;
    const auto result = rename
        ? lifecycle_.renameCurrentProject(slug)
        : lifecycle_.saveAsProject(slug);

    char feedback[32] = {};
    const char* verb = result.success()
        ? (rename ? "Renamed" : "Saved")
        : projectLifecycleFailureLabel(result.status, rename ? "Rename failed" : "Save As failed");
    formatProjectLifecycleFeedback(feedback, sizeof(feedback), verb, slug);
    navigation_.setLifecycleFeedback(feedback);

    if (!result.success()) {
        OC_LOG_WARN("[Project] {} {} failed status={}",
                    rename ? "rename" : "save-as",
                    slug,
                    static_cast<unsigned>(result.status));
        return true;
    }

    OC_LOG_INFO("[Project] {} {} bytes={}",
                rename ? "rename" : "save-as",
                slug,
                result.bytes);
    back();
    return true;
}

FLASHMEM bool ProjectHandler::activateFocusedProjectAction() {
    using core::state::project::ProjectNodeId;

    const auto node = navigation_.currentNode.get();
    const uint8_t row = navigation_.focusedRow.get();
    if (isProjectNameEditorNode(node)) {
        if (!appendProjectNameKey(navigation_)) {
            navigation_.setLifecycleFeedback("Name too long");
        }
        return true;
    }

    if (node == ProjectNodeId::NEW_PROJECT_CONFIRM) {
        if (row == 0) {
            return saveAndResetProjectWithFeedback(!lifecycle_.currentProjectHasSavedIdentity());
        }
        if (row == 1) {
            resetProject();
            return true;
        }
        if (row == 2) {
            back();
            return true;
        }
        return false;
    }

    if (node == ProjectNodeId::LOAD_PROJECT) {
        if (row >= navigation_.loadProjects.count) return false;
        const char* projectId = navigation_.loadProjects.entries[row].id.data();
        if (lifecycle_.currentProjectDirty()) {
            core::state::project::openProjectLoadConfirmation(
                navigation_,
                projectId,
                lifecycle_.currentProjectHasSavedIdentity()
            );
            return true;
        }
        loadProjectWithFeedback(projectId);
        return true;
    }

    if (node == ProjectNodeId::LOAD_PROJECT_CONFIRM) {
        const char* projectId = navigation_.pendingLoadProjectId.data();
        if (row == 0) {
            const bool loaded = navigation_.pendingLoadCanSaveCurrent
                ? saveCurrentAndLoadProjectWithFeedback(projectId)
                : saveAsAndLoadProjectWithFeedback(projectId);
            if (loaded) {
                back();
            }
            return true;
        }
        if (row == 1) {
            if (navigation_.pendingLoadCanSaveCurrent) {
                if (saveAsAndLoadProjectWithFeedback(projectId)) {
                    back();
                }
                return true;
            }
            if (loadProjectWithFeedback(projectId)) {
                back();
            }
            return true;
        }
        if (row == 2) {
            if (navigation_.pendingLoadCanSaveCurrent) {
                if (loadProjectWithFeedback(projectId)) {
                    back();
                }
                return true;
            }
            back();
            return true;
        }
        if (row == 3) {
            if (!navigation_.pendingLoadCanSaveCurrent) return false;
            back();
            return true;
        }
        return true;
    }

    const bool newProjectAction =
        (node == ProjectNodeId::OVERVIEW_ROOT && row == 0) ||
        (node == ProjectNodeId::STORAGE_ROOT && row == 3);
    if (newProjectAction) {
        core::state::project::openNewProjectConfirmation(navigation_);
        return true;
    }

    const bool loadProjectAction =
        (node == ProjectNodeId::OVERVIEW_ROOT && row == 1) ||
        (node == ProjectNodeId::STORAGE_ROOT && row == 4);
    if (loadProjectAction) {
        navigation_.clearLifecycleFeedback();
        const auto result = lifecycle_.refreshLoadableProjects();
        if (!result.success()) {
            navigation_.setLifecycleFeedback(projectLifecycleFailureLabel(result.status, "List failed"));
            OC_LOG_WARN("[Project] list projects failed status={}",
                        static_cast<unsigned>(result.status));
            return true;
        }
        core::state::project::openProjectLoadPicker(navigation_);
        if (navigation_.loadProjects.count == 0) {
            navigation_.setLifecycleFeedback("No projects");
        } else {
            OC_LOG_INFO("[Project] list projects count={} truncated={}",
                        static_cast<unsigned>(navigation_.loadProjects.count),
                        navigation_.loadProjects.truncated ? 1 : 0);
        }
        return true;
    }

    const bool saveProjectAction =
        (node == ProjectNodeId::OVERVIEW_ROOT && row == 2) ||
        (node == ProjectNodeId::STORAGE_ROOT && row == 0);
    if (saveProjectAction) {
        const auto result = lifecycle_.saveCurrentProject();
        const char* projectId = lifecycle_.currentProjectId();
        char feedback[32] = {};
        const char* verb = result.success()
            ? "Saved"
            : projectLifecycleFailureLabel(result.status, "Save failed");
        formatProjectLifecycleFeedback(feedback, sizeof(feedback), verb, projectId);
        navigation_.setLifecycleFeedback(feedback);
        if (result.success()) {
            OC_LOG_INFO("[Project] save {} bytes={}", projectId, result.bytes);
        } else {
            OC_LOG_WARN("[Project] save {} failed status={}",
                        projectId,
                        static_cast<unsigned>(result.status));
        }
        return true;
    }

    const bool saveAsProjectAction =
        (node == ProjectNodeId::OVERVIEW_ROOT && row == 3) ||
        (node == ProjectNodeId::STORAGE_ROOT && row == 1);
    if (saveAsProjectAction) {
        navigation_.clearLifecycleFeedback();
        core::state::project::openProjectNameEditor(
            navigation_,
            ProjectNodeId::SAVE_AS_PROJECT_NAME,
            ""
        );
        return true;
    }

    const bool renameProjectAction =
        (node == ProjectNodeId::OVERVIEW_ROOT && row == 4) ||
        (node == ProjectNodeId::STORAGE_ROOT && row == 2);
    if (renameProjectAction) {
        navigation_.clearLifecycleFeedback();
        core::state::project::openProjectNameEditor(
            navigation_,
            ProjectNodeId::RENAME_PROJECT_NAME,
            lifecycle_.currentProjectHasSavedIdentity() ? lifecycle_.currentProjectId() : ""
        );
        return true;
    }

    return false;
}

FLASHMEM void ProjectHandler::resetProject() {
    lifecycle_.resetMusicalProject();
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
                    normalizedTurnsForStepRate(TEMPO_RANGE_STEPS, PROJECT_OPT_TEMPO_STEPS_PER_TURN)
                );
                return;
            case 1:
                configureOptDiscrete(
                    encoders_,
                    TRANSPORT_SWING_STEPS,
                    indexToNormalized(navigation_.transportSwingPercent, TRANSPORT_SWING_STEPS),
                    normalizedTurnsForStepRate(
                        TRANSPORT_SWING_STEPS,
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
                    TRANSPORT_RUN_MODE_COUNT,
                    indexToNormalized(navigation_.transportRunMode, TRANSPORT_RUN_MODE_COUNT)
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
            MIDI_CHANNEL_COUNT,
            indexToNormalized(channel, MIDI_CHANNEL_COUNT)
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

FLASHMEM void ProjectHandler::back() {
    core::state::project::backProjectNavigation(navigation_);
    syncFocusedEncoder();
}

FLASHMEM void ProjectHandler::consumeUndo() {
    history_.undo();
}

FLASHMEM void ProjectHandler::consumeRedo() {
    history_.redo();
}

}  // namespace core::handler

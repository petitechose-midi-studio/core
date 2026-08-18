#include "handler/sequencer/DrumLaneEditorHandler.hpp"

#include <algorithm>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>
#include <config/Timing.hpp>

#include <oc/api/MidiAPI.hpp>
#include <oc/time/Time.hpp>

#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/StatusBarState.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#if defined(MS_UX_RECORDER)
#include "validation/ux/SemanticUxRecorder.hpp"
#endif

namespace core::handler {
namespace {

namespace seq = core::state::sequencer;
namespace input_utils = core::handler::sequencer::input_utils;

constexpr uint8_t AUDITION_VELOCITY = 100U;
constexpr uint8_t AUDITION_NOTE_OFF_RETRY_LIMIT = 8U;
constexpr uint32_t AUDITION_DURATION_MS = 120U;
constexpr uint32_t AUDITION_MIN_INTERVAL_MS = 32U;
constexpr uint32_t AUDITION_NOTE_OFF_RETRY_MS = 8U;

FLASHMEM bool sendMidiNoteOn(
    void* context,
    uint8_t channel,
    uint8_t note,
    uint8_t velocity
) {
    return static_cast<oc::api::MidiAPI*>(context)->sendNoteOn(
        channel,
        note,
        velocity
    ) == oc::interface::MidiOutputAcceptance::ACCEPTED;
}

FLASHMEM bool sendMidiNoteOff(
    void* context,
    uint8_t channel,
    uint8_t note,
    uint8_t velocity
) {
    return static_cast<oc::api::MidiAPI*>(context)->sendNoteOff(
        channel,
        note,
        velocity
    ) == oc::interface::MidiOutputAcceptance::ACCEPTED;
}

FLASHMEM void sendMidiPanic(void* context) {
    static_cast<oc::api::MidiAPI*>(context)->allNotesOff();
}

constexpr DrumLaneAuditionServices::Operations kMidiAuditionOperations{
    .noteOn = &sendMidiNoteOn,
    .noteOff = &sendMidiNoteOff,
    .allNotesOff = &sendMidiPanic,
};

}  // namespace

FLASHMEM DrumLaneAuditionServices::DrumLaneAuditionServices(
    void* context,
    const Operations* operations,
    const core::state::project::ProjectTrackState& projectTracks,
    const core::state::StatusBarState& statusBar
)
    : context_(context)
    , operations_(operations)
    , project_tracks_(&projectTracks)
    , status_bar_(&statusBar) {}

FLASHMEM DrumLaneAuditionServices DrumLaneAuditionServices::fromMidi(
    oc::api::MidiAPI& midi,
    const core::state::project::ProjectTrackState& projectTracks,
    const core::state::StatusBarState& statusBar
) {
    return DrumLaneAuditionServices(
        &midi,
        &kMidiAuditionOperations,
        projectTracks,
        statusBar
    );
}

FLASHMEM bool DrumLaneAuditionServices::allowed(uint8_t track) const {
    return context_ != nullptr && operations_ != nullptr &&
        operations_->noteOn != nullptr && operations_->noteOff != nullptr &&
        project_tracks_ != nullptr && status_bar_ != nullptr &&
        !status_bar_->playing.get() &&
        core::state::project::validProjectTrackIndex(track);
}

FLASHMEM bool DrumLaneAuditionServices::channelForTrack(
    uint8_t track,
    uint8_t& channel
) const {
    if (!allowed(track)) return false;
    channel = core::state::project::projectTrackMidiChannel(
        *project_tracks_,
        track
    );
    return core::state::project::validProjectTrackMidiChannel(channel);
}

FLASHMEM bool DrumLaneAuditionServices::noteOn(
    uint8_t channel,
    uint8_t note,
    uint8_t velocity
) const {
    return context_ != nullptr && operations_ != nullptr &&
        operations_->noteOn != nullptr && channel < 16U && note < 128U &&
        velocity < 128U &&
        operations_->noteOn(context_, channel, note, velocity);
}

FLASHMEM bool DrumLaneAuditionServices::noteOff(
    uint8_t channel,
    uint8_t note,
    uint8_t velocity
) const {
    return context_ != nullptr && operations_ != nullptr &&
        operations_->noteOff != nullptr && channel < 16U && note < 128U &&
        velocity < 128U &&
        operations_->noteOff(context_, channel, note, velocity);
}

FLASHMEM void DrumLaneAuditionServices::allNotesOff() const {
    if (context_ != nullptr && operations_ != nullptr &&
        operations_->allNotesOff != nullptr) {
        operations_->allNotesOff(context_);
    }
}

FLASHMEM DrumLaneEditorHandler::DrumLaneEditorHandler(
    core::state::sequencer::SequencerState& sequencer,
    SequencerHistoryDomainServices history,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID overlayScope,
    DrumLaneAuditionServices audition
)
    : sequencer_(sequencer)
    , history_(history)
    , audition_(audition)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , overlay_scope_(overlayScope) {
    setupBindings();
}

FLASHMEM DrumLaneEditorHandler::~DrumLaneEditorHandler() {
    cancelNoteAudition(core::time_compat::millis());
    if (audition_active_) {
        audition_.allNotesOff();
        audition_active_ = false;
    }
}

FLASHMEM void DrumLaneEditorHandler::setupBindings() {
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(overlay_scope_)
        .when([this]() FLASHMEM {
            return sequencer_.drumSequencer.laneEditor.active;
        })
        .then([this](float delta) FLASHMEM {
            const auto& editor =
                sequencer_.drumSequencer.laneEditor;
            if (editor.textEditing) {
                moveField(delta);
            } else if (lane_switch_modifier_) {
                moveLane(delta);
            } else {
                moveField(delta);
            }
        });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(overlay_scope_)
        .when([this]() FLASHMEM {
            return sequencer_.drumSequencer.laneEditor.active;
        })
        .then([this](float normalized) FLASHMEM { editValue(normalized); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(overlay_scope_)
        .when([this]() FLASHMEM {
            return sequencer_.drumSequencer.laneEditor.active;
        })
        .then([this]() FLASHMEM { activateField(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(overlay_scope_)
        .when([this]() FLASHMEM {
            return sequencer_.drumSequencer.laneEditor.active;
        })
        .then([this]() FLASHMEM { close(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .press()
        .scope(overlay_scope_)
        .when([this]() FLASHMEM {
            const auto& editor =
                sequencer_.drumSequencer.laneEditor;
            return editor.active;
        })
        .then([this]() FLASHMEM {
            auto& drumUi = sequencer_.drumSequencer;
            if (drumUi.laneEditor.textEditing) {
                drumUi.setLaneNameShift(true);
            } else {
                lane_switch_modifier_ = true;
            }
        });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(overlay_scope_)
        .when([this]() FLASHMEM {
            const auto& editor =
                sequencer_.drumSequencer.laneEditor;
            return editor.active &&
                (lane_switch_modifier_ || editor.textShiftActive);
        })
        .then([this]() FLASHMEM {
            auto& drumUi = sequencer_.drumSequencer;
            lane_switch_modifier_ = false;
            drumUi.setLaneNameShift(false);
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(overlay_scope_)
        .when([this]() FLASHMEM {
            return sequencer_.drumSequencer.laneEditor.active;
        })
        .then([this]() FLASHMEM { apply(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(overlay_scope_)
        .when([this]() FLASHMEM {
            const auto& editor =
                sequencer_.drumSequencer.laneEditor;
            return editor.active && editor.textEditing;
        })
        .then([this]() FLASHMEM {
            sequencer_.drumSequencer.backspaceLaneName();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(overlay_scope_)
        .when([this]() FLASHMEM {
            const auto& editor =
                sequencer_.drumSequencer.laneEditor;
            return editor.active && !editor.textEditing &&
                editor.mode == seq::DrumLaneEditorMode::EDIT;
        })
        .then([this]() FLASHMEM { remove(); });
}

FLASHMEM bool DrumLaneEditorHandler::open(bool create) {
    auto& drumUi = sequencer_.drumSequencer;
    cancelNoteAudition(core::time_compat::millis());
    lane_switch_modifier_ = false;
    if (history_.commitCoalescedDrumEditOutcome() ==
        seq::SequencerPatternHistoryCommitOutcome::Failed) {
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable,
            oc::time::millis()
        );
        return false;
    }
    if (!drumUi.openLaneEditor(create)) return false;
    overlays_.show(core::ui::OverlayType::SEQ_DRUM_LANE_EDIT);
    configureOpt();
    return true;
}

FLASHMEM void DrumLaneEditorHandler::close() {
    auto& drumUi = sequencer_.drumSequencer;
    cancelNoteAudition(core::time_compat::millis());
    lane_switch_modifier_ = false;
    if (drumUi.laneEditor.textEditing) {
        drumUi.cancelLaneNameEditing();
        configureOpt();
        return;
    }
    if (drumUi.leaveLaneIdentityEditor()) {
        configureOpt();
        return;
    }
    drumUi.cancelLaneEditor();
    if (overlays_.isCurrent(core::ui::OverlayType::SEQ_DRUM_LANE_EDIT)) {
        overlays_.hide();
    }
}

FLASHMEM void DrumLaneEditorHandler::update(uint32_t nowMs) {
    updateNoteAudition(nowMs);
    auto& drumUi = sequencer_.drumSequencer;
    const bool current =
        overlays_.isCurrent(core::ui::OverlayType::SEQ_DRUM_LANE_EDIT);
    if (drumUi.laneEditor.active &&
        (!current || !drumUi.gridVisible() || drumUi.drumTrack == nullptr)) {
        close();
        return;
    }
    if (!drumUi.laneEditor.active &&
        drumUi.selector == seq::DrumSequencerSelector::LANE_EDITOR) {
        drumUi.cancelLaneEditor();
    }
}

FLASHMEM void DrumLaneEditorHandler::moveField(float delta) {
    auto& drumUi = sequencer_.drumSequencer;
    if (drumUi.laneEditor.field == seq::DrumLaneEditorField::NOTE) {
        cancelNoteAudition(core::time_compat::millis());
    }
    drumUi.moveLaneEditorField(delta);
    configureOpt();
}

FLASHMEM void DrumLaneEditorHandler::moveLane(float delta) {
    auto& drumUi = sequencer_.drumSequencer;
    const uint32_t nowMs = core::time_compat::millis();
    cancelNoteAudition(nowMs);
    if (!drumUi.retargetLaneEditor(delta)) return;
    configureOpt();
    if (drumUi.laneEditor.field == seq::DrumLaneEditorField::NOTE) {
        requestNoteAudition(drumUi.laneEditor.draft.midiNote, nowMs);
    }
}

FLASHMEM void DrumLaneEditorHandler::editValue(float normalized) {
    auto& drumUi = sequencer_.drumSequencer;
    if (drumUi.laneEditor.textEditing) {
        drumUi.moveLaneNameRow(normalized);
    } else {
        const bool noteField =
            drumUi.laneEditor.field == seq::DrumLaneEditorField::NOTE;
        const uint8_t previousNote = drumUi.laneEditor.draft.midiNote;
        drumUi.editLaneEditorValue(normalized);
        if (noteField && drumUi.laneEditor.draft.midiNote != previousNote) {
            requestNoteAudition(
                drumUi.laneEditor.draft.midiNote,
                core::time_compat::millis()
            );
        }
    }
}

FLASHMEM void DrumLaneEditorHandler::activateField() {
    auto& drumUi = sequencer_.drumSequencer;
    auto& editor = drumUi.laneEditor;
    if (editor.field == seq::DrumLaneEditorField::NAME &&
        editor.textEditing) {
        drumUi.insertLaneNameKey();
        return;
    }
    if (editor.field == seq::DrumLaneEditorField::NAME) {
        drumUi.toggleLaneNameEditing();
        configureOpt();
    } else if (editor.field == seq::DrumLaneEditorField::IDENTITY) {
        if (drumUi.enterLaneIdentityEditor()) configureOpt();
    } else if (editor.field ==
               seq::DrumLaneEditorField::USE_PRESET_DEFAULTS) {
        if (drumUi.resetLaneIdentityOverrides()) configureOpt();
    }
}

FLASHMEM bool DrumLaneEditorHandler::beginHistory(
    seq::SequencerHistoryDescriptor descriptor
) {
    const auto outcome = history_.beginCoalescedDrumEdit(
        descriptor,
        oc::time::millis()
    );
    if (seq::sequencerHistoryOpenAccepted(outcome)) return true;
    sequencer_.historyFeedback.showRejection(outcome, oc::time::millis());
    return false;
}

FLASHMEM bool DrumLaneEditorHandler::sealHistory(
    bool changed,
    seq::SequencerHistoryDescriptor descriptor
) {
    if (!history_.sealCoalescedDrumEdit(changed, descriptor)) {
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable,
            oc::time::millis()
        );
        return false;
    }
    if (history_.commitCoalescedDrumEditOutcome() !=
        seq::SequencerPatternHistoryCommitOutcome::Failed) {
        return true;
    }
    sequencer_.historyFeedback.showRejection(
        seq::SequencerHistoryRejectionReason::HistoryUnavailable,
        oc::time::millis()
    );
    return false;
}

FLASHMEM void DrumLaneEditorHandler::apply() {
    auto& drumUi = sequencer_.drumSequencer;
    if (drumUi.laneEditor.textEditing) {
        drumUi.acceptLaneNameEditing();
        configureOpt();
        return;
    }
    cancelNoteAudition(core::time_compat::millis());
    const auto& editor = drumUi.laneEditor;
    const auto kind = editor.mode == seq::DrumLaneEditorMode::CREATE ||
            editor.targetLane != editor.sourceLane
        ? seq::SequencerHistoryActionKind::DrumLaneStructure
        : seq::SequencerHistoryActionKind::DrumLaneEdit;
    seq::SequencerHistoryDescriptor descriptor{
        .kind = kind,
        .trackIndex = drumUi.targetTrack,
        .laneIndex = editor.sourceLane,
    };
    if (!beginHistory(descriptor)) return;
    const bool changed = drumUi.applyLaneEditor();
    (void)sealHistory(changed, descriptor);
    lane_switch_modifier_ = false;
    if (overlays_.isCurrent(core::ui::OverlayType::SEQ_DRUM_LANE_EDIT)) {
        overlays_.hide();
    }
}

FLASHMEM void DrumLaneEditorHandler::remove() {
    auto& drumUi = sequencer_.drumSequencer;
    cancelNoteAudition(core::time_compat::millis());
    seq::SequencerHistoryDescriptor descriptor{
        .kind = seq::SequencerHistoryActionKind::DrumLaneStructure,
        .trackIndex = drumUi.targetTrack,
        .laneIndex = drumUi.laneEditor.sourceLane,
    };
    if (!beginHistory(descriptor)) return;
    const bool changed = drumUi.removeLaneFromEditor();
    (void)sealHistory(changed, descriptor);
    lane_switch_modifier_ = false;
    if (overlays_.isCurrent(core::ui::OverlayType::SEQ_DRUM_LANE_EDIT)) {
        overlays_.hide();
    }
}

FLASHMEM void DrumLaneEditorHandler::requestNoteAudition(
    uint8_t note,
    uint32_t nowMs
) {
    auto& drumUi = sequencer_.drumSequencer;
    if (!drumUi.laneEditor.active || drumUi.laneEditor.textEditing ||
        drumUi.laneEditor.field != seq::DrumLaneEditorField::NOTE ||
        note >= 128U || !audition_.allowed(drumUi.targetTrack)) {
        cancelNoteAudition(nowMs);
        return;
    }

    if (audition_active_ && audition_note_ == note) {
        audition_pending_ = false;
        audition_off_at_ms_ = nowMs + AUDITION_DURATION_MS;
        return;
    }

    audition_pending_note_ = note;
    audition_pending_ = true;
    if (audition_active_) {
        (void)stopActiveNoteAudition(nowMs);
    }
    updateNoteAudition(nowMs);
}

FLASHMEM void DrumLaneEditorHandler::cancelNoteAudition(uint32_t nowMs) {
    audition_pending_ = false;
    if (audition_active_) {
        (void)stopActiveNoteAudition(nowMs);
    }
}

FLASHMEM bool DrumLaneEditorHandler::stopActiveNoteAudition(uint32_t nowMs) {
    if (!audition_active_) return true;
    if (audition_.noteOff(audition_channel_, audition_note_, 0U)) {
        audition_active_ = false;
        audition_off_retry_count_ = 0U;
        audition_retry_at_ms_ = 0U;
        return true;
    }

    ++audition_off_retry_count_;
    audition_retry_at_ms_ = nowMs + AUDITION_NOTE_OFF_RETRY_MS;
    if (audition_off_retry_count_ < AUDITION_NOTE_OFF_RETRY_LIMIT) {
        return false;
    }

    // The preview is only admitted while Transport is stopped. Escalating to
    // the transport panic after a bounded retry window is therefore safer
    // than allowing an editor-generated note to survive its owner.
    audition_.allNotesOff();
    audition_active_ = false;
    audition_off_retry_count_ = 0U;
    audition_retry_at_ms_ = 0U;
    audition_next_on_at_ms_ = nowMs + AUDITION_MIN_INTERVAL_MS;
    return true;
}

FLASHMEM void DrumLaneEditorHandler::updateNoteAudition(uint32_t nowMs) {
    auto& drumUi = sequencer_.drumSequencer;
    const bool contextActive = drumUi.laneEditor.active &&
        !drumUi.laneEditor.textEditing &&
        drumUi.laneEditor.field == seq::DrumLaneEditorField::NOTE &&
        audition_.allowed(drumUi.targetTrack);

    if (!contextActive) {
        audition_pending_ = false;
        if (audition_active_ &&
            oc::time::deadlineReachedMs(nowMs, audition_retry_at_ms_)) {
            (void)stopActiveNoteAudition(nowMs);
        }
        return;
    }

    if (audition_active_) {
        const bool replacementPending =
            audition_pending_ && audition_pending_note_ != audition_note_;
        if ((replacementPending ||
             oc::time::deadlineReachedMs(nowMs, audition_off_at_ms_)) &&
            oc::time::deadlineReachedMs(nowMs, audition_retry_at_ms_)) {
            (void)stopActiveNoteAudition(nowMs);
        }
    }

    if (audition_active_ || !audition_pending_ ||
        !oc::time::deadlineReachedMs(nowMs, audition_next_on_at_ms_)) {
        return;
    }

    uint8_t channel = 0U;
    if (!audition_.channelForTrack(drumUi.targetTrack, channel) ||
        !audition_.noteOn(channel, audition_pending_note_, AUDITION_VELOCITY)) {
        // Note-on rejection is a safe drop: no output ownership was acquired,
        // so retrying stale encoder samples would only flood the transport.
        audition_pending_ = false;
        audition_next_on_at_ms_ = nowMs + AUDITION_MIN_INTERVAL_MS;
        return;
    }

    audition_active_ = true;
    audition_channel_ = channel;
    audition_note_ = audition_pending_note_;
    audition_pending_ = false;
    audition_off_retry_count_ = 0U;
    audition_retry_at_ms_ = nowMs;
    audition_off_at_ms_ = nowMs + AUDITION_DURATION_MS;
    audition_next_on_at_ms_ = nowMs + AUDITION_MIN_INTERVAL_MS;
}

FLASHMEM void DrumLaneEditorHandler::configureOpt() {
    const auto& drumUi = sequencer_.drumSequencer;
    const auto& editor = drumUi.laneEditor;
    if (!editor.active || drumUi.drumTrack == nullptr) return;

    if (editor.textEditing) {
        encoders_.setMode(
            Config::EncoderID::OPT,
            oc::interface::EncoderMode::RAW
        );
        encoders_.setPosition(Config::EncoderID::OPT, 0.0f);
#if defined(MS_UX_RECORDER)
        core::validation::ux::recordEncoderContractTrace(
            core::validation::ux::EncoderContractOwner::DrumLaneEditor,
            core::validation::ux::EncoderContractMode::Raw,
            static_cast<oc::type::EncoderID>(Config::EncoderID::OPT),
            -1.0f,
            1.0f,
            0U,
            0U,
            0.0f,
            0.0f
        );
#endif
        return;
    }

    // Lane-name editing owns OPT in RAW mode. Re-establish the complete
    // normalized contract before configuring any discrete editor field so a
    // keyboard close cannot leak raw -1/+1 values into the next field or the
    // next overlay that acquires OPT.
    encoders_.setMode(
        Config::EncoderID::OPT,
        oc::interface::EncoderMode::NORMALIZED
    );
    encoders_.setBounds(Config::EncoderID::OPT, 0.0f, 1.0f);
    encoders_.setDiscreteTicksPerStep(
        Config::EncoderID::OPT,
        input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP
    );
    encoders_.setNormalizedTurns(
        Config::EncoderID::OPT,
        input_utils::DEFAULT_NORMALIZED_TURNS
    );

    int count = 1;
    int index = 0;
    switch (editor.field) {
        case seq::DrumLaneEditorField::PRESET:
            count = static_cast<int>(seq::DrumLaneRole::PERCUSSION) + 1;
            index = static_cast<int>(editor.draft.role);
            break;
        case seq::DrumLaneEditorField::IDENTITY:
        case seq::DrumLaneEditorField::NAME:
        case seq::DrumLaneEditorField::USE_PRESET_DEFAULTS:
            count = 1;
            index = 0;
            break;
        case seq::DrumLaneEditorField::ICON:
            count = static_cast<int>(seq::DrumLaneIcon::COUNT);
            index = static_cast<int>(
                seq::drumLaneDisplayIcon(editor.draft)
            );
            break;
        case seq::DrumLaneEditorField::COLOR:
            count = seq::DRUM_LANE_COLOR_COUNT;
            index = seq::drumLaneDisplayColorIndex(editor.draft);
            break;
        case seq::DrumLaneEditorField::NOTE:
            count = 128;
            index = editor.draft.midiNote;
            break;
        case seq::DrumLaneEditorField::POSITION: {
            const int laneCount = drumUi.drumTrack->kit.laneCount;
            count = editor.mode == seq::DrumLaneEditorMode::CREATE
                ? laneCount + 1
                : laneCount;
            index = editor.targetLane;
            break;
        }
        case seq::DrumLaneEditorField::COUNT:
        default:
            break;
    }
    const auto discreteSteps = static_cast<uint8_t>(count);
    const float position = input_utils::indexToNormalized(index, count);
    encoders_.setDiscreteSteps(Config::EncoderID::OPT, discreteSteps);
    encoders_.setPosition(Config::EncoderID::OPT, position);
#if defined(MS_UX_RECORDER)
    core::validation::ux::recordEncoderContractTrace(
        core::validation::ux::EncoderContractOwner::DrumLaneEditor,
        core::validation::ux::EncoderContractMode::Normalized,
        static_cast<oc::type::EncoderID>(Config::EncoderID::OPT),
        0.0f,
        1.0f,
        discreteSteps,
        input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP,
        input_utils::DEFAULT_NORMALIZED_TURNS,
        position
    );
#endif
}

}  // namespace core::handler

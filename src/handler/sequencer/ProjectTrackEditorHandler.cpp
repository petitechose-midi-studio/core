#include "handler/sequencer/ProjectTrackEditorHandler.hpp"

#include <algorithm>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include <oc/time/Time.hpp>

#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackEditorOps.hpp"

namespace core::handler {
namespace {

namespace input_utils = core::handler::sequencer::input_utils;
using EditorProperty = core::state::project::ProjectTrackEditorProperty;
using HistoryKind = core::state::project::ProjectTrackHistoryActionKind;

FLASHMEM int direction(float delta) {
    return delta > 0.0f ? 1 : (delta < 0.0f ? -1 : 0);
}

FLASHMEM uint8_t channelFromNormalized(float normalized) {
    return static_cast<uint8_t>(input_utils::normalizedToIndex(normalized, 16));
}

FLASHMEM int16_t delayFromNormalized(float normalized) {
    constexpr int count =
        core::state::project::PROJECT_TRACK_DELAY_MAX_MS -
        core::state::project::PROJECT_TRACK_DELAY_MIN_MS + 1;
    return static_cast<int16_t>(
        core::state::project::PROJECT_TRACK_DELAY_MIN_MS +
        input_utils::normalizedToIndex(normalized, count)
    );
}

FLASHMEM HistoryKind historyKind(EditorProperty property) {
    return property == EditorProperty::DELAY
        ? HistoryKind::Delay
        : HistoryKind::MidiChannel;
}

}  // namespace

FLASHMEM ProjectTrackEditorHandler::ProjectTrackEditorHandler(
    StateRefs state,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID overlayScope
)
    : editor_(state.editor)
    , tracks_(state.tracks)
    , sequencer_tracks_(state.sequencerTracks)
    , shared_tracks_(state.sharedTracks)
    , track_domain_(state.trackDomain)
    , history_(state.history)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , overlay_scope_(overlayScope) {
    setupBindings();
}

FLASHMEM void ProjectTrackEditorHandler::setupBindings() {
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(overlay_scope_)
        .when([this]() { return editor_.active; })
        .then([this](float delta) {
            if (buttons_.isPressed(Config::ButtonID::LEFT_CENTER)) {
                moveTrack(delta);
            } else {
                moveProperty(delta);
            }
        });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(overlay_scope_)
        .when([this]() { return editor_.active && ownsActiveTrack(); })
        .then([this](float normalized) { setFocusedValue(normalized); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(overlay_scope_)
        .when([this]() { return editor_.active; })
        .then([this]() { close(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(overlay_scope_)
        .when([this]() { return editor_.active && ownsActiveTrack(); })
        .then([this]() {
            if (core::state::project::projectTrackEditorKindDraftDirty(editor_)) {
                cancelTrackKindDraft();
            } else {
                toggleMute();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(overlay_scope_)
        .when([this]() { return editor_.active && ownsActiveTrack(); })
        .then([this]() {
            if (editor_.selectedProperty == EditorProperty::TYPE &&
                editor_.draftKind != editor_.currentKind) {
                applyTrackKind();
            } else {
                toggleSolo();
            }
        });

}

FLASHMEM bool ProjectTrackEditorHandler::openActiveTrack() {
    commitPendingGesture();
    const uint8_t track = shared_tracks_.activeTrack();
    const uint16_t enabled = shared_tracks_.enabledMask();
    const auto result = core::state::project::openProjectTrackEditor(
        editor_,
        track,
        enabled
    );
    if (!result.changed() &&
        result.status != core::state::project::ProjectTrackEditorMutationStatus::NO_CHANGE) {
        return false;
    }
    syncKindDraft();
    overlays_.show(core::ui::OverlayType::SEQ_TRACK_EDIT);
    configureOpt();
    return true;
}

FLASHMEM void ProjectTrackEditorHandler::close() {
    commitPendingGesture();
    cancelTrackKindDraft();
    if (overlays_.isCurrent(core::ui::OverlayType::SEQ_TRACK_EDIT)) {
        overlays_.hide();
    }
    (void)core::state::project::closeProjectTrackEditor(editor_);
}

void ProjectTrackEditorHandler::update(uint32_t nowMs) {
    if (track_domain_.hasActiveGesture() &&
        static_cast<int32_t>(nowMs - gesture_commit_deadline_ms_) >= 0) {
        commitPendingGesture();
    }
    if (!editor_.active) return;

    const uint8_t active = shared_tracks_.activeTrack();
    const uint16_t enabled = shared_tracks_.enabledMask();
    if (!core::state::project::projectTrackEditorTrackEnabled(
            enabled,
            editor_.trackIndex
        )) {
        close();
        return;
    }
    if (editor_.trackIndex != active) {
        commitPendingGesture();
        if (core::state::project::projectTrackEditorKindDraftDirty(editor_)) {
            // The retained editor owns its opening target while its destructive
            // type draft is dirty. Restore any non-authoritative cursor drift
            // instead of silently moving or discarding the draft.
            (void)shared_tracks_.setState(enabled, editor_.trackIndex);
            return;
        }
        const auto retargeted = core::state::project::retargetProjectTrackEditor(
            editor_,
            active,
            enabled
        );
        if (retargeted.changed()) {
            syncKindDraft();
            configureOpt();
        }
    } else {
        const auto currentKind = sequencer_tracks_.isDrumTrack(active)
            ? core::state::project::ProjectTrackEditorKind::DRUM
            : core::state::project::ProjectTrackEditorKind::INSTRUMENT;
        if (editor_.currentKind != currentKind) {
            (void)core::state::project::syncProjectTrackEditorKind(
                editor_,
                currentKind
            );
            configureOpt();
        }
    }
}

FLASHMEM void ProjectTrackEditorHandler::moveTrack(float delta) {
    const int move = direction(delta);
    if (move == 0) return;
    commitPendingGesture();
    if (core::state::project::projectTrackEditorKindDraftDirty(editor_)) {
        return;
    }
    const uint16_t enabled = shared_tracks_.enabledMask();
    const uint8_t target = core::state::project::nextEnabledProjectTrack(
        enabled,
        editor_.trackIndex,
        move
    );
    if (target >= core::state::project::PROJECT_TRACK_COUNT ||
        target == editor_.trackIndex ||
        !shared_tracks_.setState(enabled, target)) {
        return;
    }
    (void)core::state::project::retargetProjectTrackEditor(editor_, target, enabled);
    syncKindDraft();
    configureOpt();
}

FLASHMEM void ProjectTrackEditorHandler::moveProperty(float delta) {
    commitPendingGesture();
    if (core::state::project::moveProjectTrackEditorProperty(
            editor_,
            direction(delta)
        ).changed()) {
        configureOpt();
    }
}

FLASHMEM void ProjectTrackEditorHandler::setFocusedValue(float normalized) {
    const auto property = editor_.selectedProperty;
    if (property == EditorProperty::TYPE) {
        (void)core::state::project::selectProjectTrackEditorDraftKind(
            editor_,
            normalized >= 0.5f
                ? core::state::project::ProjectTrackEditorKind::DRUM
                : core::state::project::ProjectTrackEditorKind::INSTRUMENT
        );
        return;
    }
    const auto kind = historyKind(property);
    const bool began = !track_domain_.hasActiveGesture();
    if (began && !track_domain_.beginGesture(kind, editor_.trackIndex)) return;

    bool changed = false;
    if (property == EditorProperty::CHANNEL) {
        changed = track_domain_.setMidiChannel(
            editor_.trackIndex,
            channelFromNormalized(normalized)
        );
    } else if (property == EditorProperty::DELAY) {
        changed = track_domain_.setDelayMs(
            editor_.trackIndex,
            delayFromNormalized(normalized)
        );
    }
    if (!changed && began) {
        cancelPendingGesture();
        return;
    }
    if (changed) {
        gesture_commit_deadline_ms_ =
            oc::time::millis() + GESTURE_IDLE_COMMIT_MS;
    }
}

FLASHMEM void ProjectTrackEditorHandler::toggleMute() {
    commitPendingGesture();
    const bool muted = core::state::project::projectTrackMuted(
        tracks_,
        editor_.trackIndex
    );
    (void)track_domain_.setMuted(editor_.trackIndex, !muted);
}

FLASHMEM void ProjectTrackEditorHandler::toggleSolo() {
    commitPendingGesture();
    const bool soloed = core::state::project::projectTrackSoloed(
        tracks_,
        editor_.trackIndex
    );
    (void)track_domain_.setSoloed(editor_.trackIndex, !soloed);
}

FLASHMEM void ProjectTrackEditorHandler::cancelTrackKindDraft() {
    if (!core::state::project::projectTrackEditorKindDraftDirty(editor_)) return;
    (void)core::state::project::selectProjectTrackEditorDraftKind(
        editor_, editor_.currentKind
    );
    configureOpt();
}

FLASHMEM void ProjectTrackEditorHandler::applyTrackKind() {
    commitPendingGesture();
    if (!ownsActiveTrack() || editor_.draftKind == editor_.currentKind) return;
    const bool drum = editor_.draftKind ==
        core::state::project::ProjectTrackEditorKind::DRUM;
    core::state::sequencer::SequencerHistoryDescriptor descriptor{
        .kind = core::state::sequencer::SequencerHistoryActionKind::DrumTrackKind,
        .trackIndex = editor_.trackIndex,
        .hasValue = true,
        .beforeValue = editor_.currentKind ==
                core::state::project::ProjectTrackEditorKind::DRUM
            ? 1
            : 0,
        .afterValue = drum ? 1 : 0,
    };
    const auto opened = history_.beginCoalescedDrumEdit(
        descriptor,
        oc::time::millis()
    );
    if (!core::state::sequencer::sequencerHistoryOpenAccepted(opened)) return;
    const bool changed = sequencer_tracks_.setTrackKind(
        editor_.trackIndex,
        drum
            ? core::state::sequencer::SequencerTrackKind::DRUM
            : core::state::sequencer::SequencerTrackKind::INSTRUMENT,
        true,
        core::state::sequencer::DrumKitPreset::EMPTY
    );
    if (!history_.sealCoalescedDrumEdit(changed, descriptor) ||
        history_.commitCoalescedDrumEditOutcome() ==
            core::state::sequencer::SequencerPatternHistoryCommitOutcome::Failed) {
        return;
    }
    if (!changed) return;
    (void)core::state::project::syncProjectTrackEditorKind(
        editor_,
        drum
            ? core::state::project::ProjectTrackEditorKind::DRUM
            : core::state::project::ProjectTrackEditorKind::INSTRUMENT
    );
    configureOpt();
}

FLASHMEM void ProjectTrackEditorHandler::syncKindDraft() {
    (void)core::state::project::syncProjectTrackEditorKind(
        editor_,
        sequencer_tracks_.isDrumTrack(editor_.trackIndex)
            ? core::state::project::ProjectTrackEditorKind::DRUM
            : core::state::project::ProjectTrackEditorKind::INSTRUMENT
    );
}

FLASHMEM void ProjectTrackEditorHandler::configureOpt() {
    encoders_.setDiscreteTicksPerStep(
        Config::EncoderID::OPT,
        input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP
    );
    encoders_.setNormalizedTurns(
        Config::EncoderID::OPT,
        input_utils::DEFAULT_NORMALIZED_TURNS
    );

    if (editor_.selectedProperty == EditorProperty::CHANNEL) {
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, 16U);
        encoders_.setPosition(
            Config::EncoderID::OPT,
            input_utils::indexToNormalized(
                core::state::project::projectTrackMidiChannel(
                    tracks_,
                    editor_.trackIndex
                ),
                16
            )
        );
        return;
    }

    if (editor_.selectedProperty == EditorProperty::TYPE) {
        encoders_.setDiscreteSteps(Config::EncoderID::OPT, 2U);
        encoders_.setPosition(
            Config::EncoderID::OPT,
            editor_.draftKind == core::state::project::ProjectTrackEditorKind::DRUM
                ? 1.0f
                : 0.0f
        );
        return;
    }

    constexpr int delayCount =
        core::state::project::PROJECT_TRACK_DELAY_MAX_MS -
        core::state::project::PROJECT_TRACK_DELAY_MIN_MS + 1;
    encoders_.setDiscreteSteps(
        Config::EncoderID::OPT,
        static_cast<uint8_t>(delayCount)
    );
    encoders_.setPosition(
        Config::EncoderID::OPT,
        input_utils::indexToNormalized(
            core::state::project::projectTrackDelayMs(
                tracks_,
                editor_.trackIndex
            ) - core::state::project::PROJECT_TRACK_DELAY_MIN_MS,
            delayCount
        )
    );
}

FLASHMEM void ProjectTrackEditorHandler::commitPendingGesture() {
    if (!track_domain_.hasActiveGesture()) return;
    (void)track_domain_.endGesture();
    gesture_commit_deadline_ms_ = 0U;
}

FLASHMEM void ProjectTrackEditorHandler::cancelPendingGesture() {
    if (!track_domain_.hasActiveGesture()) return;
    (void)track_domain_.cancelGesture();
    gesture_commit_deadline_ms_ = 0U;
}

FLASHMEM bool ProjectTrackEditorHandler::ownsActiveTrack() const {
    return editor_.active && editor_.trackIndex == shared_tracks_.activeTrack();
}

}  // namespace core::handler

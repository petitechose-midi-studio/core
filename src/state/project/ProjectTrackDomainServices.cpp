#include "state/project/ProjectTrackDomainServices.hpp"

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"

namespace core::state::project {

namespace {

FLASHMEM uint32_t nextNonZero(uint32_t current) {
    const uint32_t next = current + 1U;
    return next == 0U ? 1U : next;
}

FLASHMEM void publishCommittedTrackMutation(void* context) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;

    state->configRevision.set(
        core::state::macro::nextMacroConfigRevision(
            state->configRevision.get(),
            core::state::macro::kMacroConfigDirtyAll
        )
    );
    state->sequencerRuntimeProjectRevision.set(
        nextNonZero(state->sequencerRuntimeProjectRevision.get())
    );
    state->markProjectMutated();
}

FLASHMEM ProjectTrackMutationResult mutateMidiChannel(
    ProjectTrackState& state,
    uint8_t track,
    int32_t value
) {
    if (value < 0 || value > UINT8_MAX) {
        return {.status = ProjectTrackMutationStatus::INVALID_MIDI_CHANNEL,
                .trackIndex = track};
    }
    return setProjectTrackMidiChannel(
        state,
        track,
        static_cast<uint8_t>(value)
    );
}

FLASHMEM ProjectTrackMutationResult mutateDelay(
    ProjectTrackState& state,
    uint8_t track,
    int32_t value
) {
    return setProjectTrackDelayMs(state, track, value);
}

FLASHMEM ProjectTrackMutationResult mutateMute(
    ProjectTrackState& state,
    uint8_t track,
    int32_t value
) {
    return setProjectTrackMuted(state, track, value != 0);
}

FLASHMEM ProjectTrackMutationResult mutateMuteMask(
    ProjectTrackState& state,
    uint8_t track,
    int32_t value
) {
    (void)track;
    return setProjectTrackMutedMask(
        state,
        static_cast<uint16_t>(value)
    );
}

FLASHMEM ProjectTrackMutationResult mutateSolo(
    ProjectTrackState& state,
    uint8_t track,
    int32_t value
) {
    return setProjectTrackSoloed(state, track, value != 0);
}

}  // namespace

FLASHMEM ProjectTrackDomainServices::ProjectTrackDomainServices(
    StateRefs state,
    Operations operations
)
    : tracks_(&state.tracks)
    , history_(&state.history)
    , operations_(operations) {}

FLASHMEM ProjectTrackDomainServices ProjectTrackDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return ProjectTrackDomainServices{
        StateRefs{state.projectTracks, state.projectTrackHistory},
        Operations{
            .context = &state,
            .publishCommittedMutation = &publishCommittedTrackMutation,
        },
    };
}

FLASHMEM bool ProjectTrackDomainServices::setMidiChannel(
    uint8_t track,
    uint8_t channel0Based
) {
    return mutate_(
        ProjectTrackHistoryActionKind::MidiChannel,
        track,
        channel0Based,
        &mutateMidiChannel
    );
}

FLASHMEM bool ProjectTrackDomainServices::setDelayMs(
    uint8_t track,
    int32_t delayMs
) {
    return mutate_(
        ProjectTrackHistoryActionKind::Delay,
        track,
        delayMs,
        &mutateDelay
    );
}

FLASHMEM bool ProjectTrackDomainServices::setMuted(
    uint8_t track,
    bool muted
) {
    return mutate_(
        ProjectTrackHistoryActionKind::Mute,
        track,
        muted ? 1 : 0,
        &mutateMute
    );
}

FLASHMEM bool ProjectTrackDomainServices::setMutedMask(
    uint16_t mutedMask,
    uint8_t historyTrack
) {
    return mutate_(
        ProjectTrackHistoryActionKind::Mute,
        historyTrack,
        mutedMask,
        &mutateMuteMask
    );
}

FLASHMEM bool ProjectTrackDomainServices::setSoloed(
    uint8_t track,
    bool soloed
) {
    return mutate_(
        ProjectTrackHistoryActionKind::Solo,
        track,
        soloed ? 1 : 0,
        &mutateSolo
    );
}

FLASHMEM bool ProjectTrackDomainServices::beginGesture(
    ProjectTrackHistoryActionKind kind,
    uint8_t track
) {
    if (tracks_ == nullptr || history_ == nullptr ||
        !validProjectTrackIndex(track)) {
        return false;
    }
    return history_->beginGesture(*tracks_, kind, track);
}

FLASHMEM bool ProjectTrackDomainServices::endGesture() {
    if (tracks_ == nullptr || history_ == nullptr ||
        !history_->hasPendingGesture()) {
        return false;
    }
    if (!history_->commitGesture(*tracks_)) return false;
    publishCommittedMutation_();
    return true;
}

FLASHMEM bool ProjectTrackDomainServices::cancelGesture() {
    if (tracks_ == nullptr || history_ == nullptr) return false;
    const bool changed = history_->cancelGesture(*tracks_);
    return changed;
}

FLASHMEM bool ProjectTrackDomainServices::undo() {
    if (tracks_ == nullptr || history_ == nullptr ||
        history_->hasPendingGesture() ||
        !history_->undo(*tracks_)) {
        return false;
    }
    publishCommittedMutation_();
    return true;
}

FLASHMEM bool ProjectTrackDomainServices::redo() {
    if (tracks_ == nullptr || history_ == nullptr ||
        history_->hasPendingGesture() ||
        !history_->redo(*tracks_)) {
        return false;
    }
    publishCommittedMutation_();
    return true;
}

FLASHMEM bool ProjectTrackDomainServices::mutate_(
    ProjectTrackHistoryActionKind kind,
    uint8_t track,
    int32_t value,
    MutationFn mutation
) {
    if (tracks_ == nullptr || history_ == nullptr || mutation == nullptr ||
        !validProjectTrackIndex(track)) {
        return false;
    }

    const bool ownsGesture = !history_->hasPendingGesture();
    if (ownsGesture && !beginGesture(kind, track)) return false;
    if (!history_->gestureMatches(kind, track)) {
        return false;
    }

    const auto result = mutation(*tracks_, track, value);
    if (!result.changed()) {
        if (ownsGesture) (void)history_->cancelGesture(*tracks_);
        return false;
    }
    return ownsGesture ? endGesture() : true;
}

FLASHMEM void ProjectTrackDomainServices::publishCommittedMutation_() const {
    if (operations_.publishCommittedMutation != nullptr) {
        operations_.publishCommittedMutation(operations_.context);
    }
}

}  // namespace core::state::project

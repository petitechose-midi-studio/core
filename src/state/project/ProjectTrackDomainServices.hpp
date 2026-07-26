#pragma once

#include <cstdint>

#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/project/ProjectTrackHistory.hpp"

namespace core::state {
struct CoreState;
}

namespace core::state::project {

/**
 * Singular mutation boundary for Track controls.
 *
 * UI handlers must use this service instead of writing ProjectTrackState or
 * secondary mirrors. A gesture captures one before snapshot and publishes
 * exactly one history command when it ends, while intermediate values remain
 * visible through the canonical ProjectTrackState revision.
 */
class ProjectTrackDomainServices {
public:
    using PublishCommittedMutationFn = void (*)(void* context);

    struct StateRefs {
        ProjectTrackState& tracks;
        ProjectTrackHistoryService& history;
    };

    struct Operations {
        void* context = nullptr;
        PublishCommittedMutationFn publishCommittedMutation = nullptr;
    };

    ProjectTrackDomainServices(StateRefs state, Operations operations);
    static ProjectTrackDomainServices fromCoreState(core::state::CoreState& state);

    [[nodiscard]] bool setMidiChannel(uint8_t track, uint8_t channel0Based);
    [[nodiscard]] bool setDelayMs(uint8_t track, int32_t delayMs);
    [[nodiscard]] bool setMuted(uint8_t track, bool muted);
    /**
     * Atomically replaces the authored Mute mask as one global Track-history
     * command. `historyTrack` is presentation metadata for the gesture; the
     * complete Track snapshot remains the authoritative Undo/Redo payload.
     */
    [[nodiscard]] bool setMutedMask(
        uint16_t mutedMask,
        uint8_t historyTrack
    );
    [[nodiscard]] bool setSoloed(uint8_t track, bool soloed);

    [[nodiscard]] bool beginGesture(
        ProjectTrackHistoryActionKind kind,
        uint8_t track
    );
    [[nodiscard]] bool endGesture();
    [[nodiscard]] bool cancelGesture();
    [[nodiscard]] bool hasActiveGesture() const {
        return history_ != nullptr && history_->hasPendingGesture();
    }

    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

private:
    using MutationFn = ProjectTrackMutationResult (*)(
        ProjectTrackState& state,
        uint8_t track,
        int32_t value
    );

    [[nodiscard]] bool mutate_(
        ProjectTrackHistoryActionKind kind,
        uint8_t track,
        int32_t value,
        MutationFn mutation
    );
    void publishCommittedMutation_() const;

    ProjectTrackState* tracks_ = nullptr;
    ProjectTrackHistoryService* history_ = nullptr;
    Operations operations_{};
};

}  // namespace core::state::project

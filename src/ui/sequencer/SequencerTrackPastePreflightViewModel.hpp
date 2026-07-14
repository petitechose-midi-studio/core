#pragma once

#include <array>
#include <cstdint>

#include "state/StructureClipboardPastePlan.hpp"
#include "state/contextual/ContextActionSpec.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"

namespace core::ui::sequencer {

/**
 * One immutable projection shared by the Track action strip and the temporary
 * paste card. It is rebuilt from live state; the UI never caches route facts.
 */
struct SequencerTrackPasteProjection {
    core::state::ClipboardTransferPlan plan{};
    core::state::contextual::ContextActionSpec action{};
    uint8_t targetTrack =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    bool copyAvailable = false;
    core::state::contextual::GuardedActionState guard{};
    core::state::contextual::OperationFeedbackState feedback{};
    uint32_t operationGeneration = 0;
    uint32_t activationGeneration = 0;
    uint8_t focusedIndex = 0;
    bool detailVisible = false;
};

enum class SequencerTrackPastePreflightPhase : uint8_t {
    HIDDEN = 0,
    READY,
    HOLDING,
    BLOCKED,
    QUEUED,
    APPLIED,
    CANCELLED,
};

enum class SequencerTrackPastePreflightTone : uint8_t {
    NEUTRAL = 0,
    CONSTRUCTIVE,
    WARNING,
    ERROR,
    SUCCESS,
};

struct SequencerTrackPastePreflightViewModel {
    bool visible = false;
    SequencerTrackPastePreflightPhase phase =
        SequencerTrackPastePreflightPhase::HIDDEN;
    SequencerTrackPastePreflightTone tone =
        SequencerTrackPastePreflightTone::NEUTRAL;
    uint32_t activationGeneration = 0;
    uint32_t operationGeneration = 0;
    uint8_t mappingIndex = 0;
    uint8_t mappingCount = 0;
    uint8_t sourceTrack =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t targetTrack =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    uint8_t inheritedLaneCount = 0;
    uint8_t pinnedLaneCount = 0;
    core::state::ClipboardTransferTargetKind targetKind =
        core::state::ClipboardTransferTargetKind::FREE;
    bool targetRouteValid = false;
    uint8_t targetMidiChannel = 0;
    core::state::sequencer::SequencerTrackActivationOrigin activationOrigin =
        core::state::sequencer::SequencerTrackActivationOrigin::UNSPECIFIED;
    core::state::sequencer::SequencerTrackActivationStatus activationStatus =
        core::state::sequencer::SequencerTrackActivationStatus::IDLE;
    core::state::contextual::OperationFeedbackStatus operationStatus =
        core::state::contextual::OperationFeedbackStatus::NONE;
    std::array<char, 40> header{};
    std::array<char, 272> mapping{};
    std::array<char, 48> footprint{};
    std::array<char, 64> route{};
    std::array<char, 64> laneBindings{};
    std::array<char, 64> detail{};
};

/**
 * Pure formatting and lifecycle projection. Telemetry is supplied as values so
 * tests and presenters do not need a second mutable paste state.
 */
SequencerTrackPastePreflightViewModel buildSequencerTrackPastePreflightViewModel(
    const SequencerTrackPasteProjection& projection,
    bool pasteHoldActive,
    const std::array<
        core::state::sequencer::SequencerTrackActivationTelemetry,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT>& telemetry
);

/** Prevents persistent APPLIED telemetry from becoming permanent chrome. */
bool shouldShowSequencerTrackPasteAppliedConfirmation(
    const SequencerTrackPastePreflightViewModel& model,
    uint32_t dismissedGeneration
);

}  // namespace core::ui::sequencer

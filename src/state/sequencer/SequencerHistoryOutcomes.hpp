#pragma once

#include <cstdint>

namespace core::state::sequencer {

enum class SequencerHistoryRejectionReason : uint8_t {
    Blocked = 0,
    ResourceUnavailable,
    HistoryUnavailable,
};

enum class SequencerHistoryOpenOutcome : uint8_t {
    Blocked = 0,
    ResourceUnavailable,
    HistoryUnavailable,
    Started,
    Continued,
};

enum class SequencerHistoryGestureOutcome : uint8_t {
    Blocked = 0,
    ResourceUnavailable,
    HistoryUnavailable,
    NoChange,
    Committed,
};

constexpr bool sequencerHistoryOpenAccepted(SequencerHistoryOpenOutcome outcome) {
    return outcome == SequencerHistoryOpenOutcome::Started ||
           outcome == SequencerHistoryOpenOutcome::Continued;
}

constexpr bool sequencerHistoryGestureAccepted(SequencerHistoryGestureOutcome outcome) {
    return outcome == SequencerHistoryGestureOutcome::NoChange ||
           outcome == SequencerHistoryGestureOutcome::Committed;
}

constexpr SequencerHistoryRejectionReason sequencerHistoryRejectionFor(
    SequencerHistoryOpenOutcome outcome) {
    return outcome == SequencerHistoryOpenOutcome::ResourceUnavailable
               ? SequencerHistoryRejectionReason::ResourceUnavailable
               : (outcome == SequencerHistoryOpenOutcome::HistoryUnavailable
                      ? SequencerHistoryRejectionReason::HistoryUnavailable
                      : SequencerHistoryRejectionReason::Blocked);
}

constexpr SequencerHistoryRejectionReason sequencerHistoryRejectionFor(
    SequencerHistoryGestureOutcome outcome) {
    return outcome == SequencerHistoryGestureOutcome::ResourceUnavailable
               ? SequencerHistoryRejectionReason::ResourceUnavailable
               : (outcome == SequencerHistoryGestureOutcome::HistoryUnavailable
                      ? SequencerHistoryRejectionReason::HistoryUnavailable
                      : SequencerHistoryRejectionReason::Blocked);
}

static_assert(sizeof(SequencerHistoryRejectionReason) == 1U);
static_assert(sizeof(SequencerHistoryOpenOutcome) == 1U);
static_assert(sizeof(SequencerHistoryGestureOutcome) == 1U);

}  // namespace core::state::sequencer

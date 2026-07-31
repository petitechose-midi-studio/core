#pragma once

#include "state/shared/MidiCcDestinationResolver.hpp"

namespace core::sequencer::midi_cc {

inline bool sameIdentity(
    const core::state::shared::MidiCcDestinationIdentity& lhs,
    const core::state::shared::MidiCcDestinationIdentity& rhs
) {
    return core::state::shared::sameMidiCcDestinationIdentity(lhs, rhs);
}

inline bool sameDestination(
    const core::state::shared::MidiCcCandidate& lhs,
    const core::state::shared::MidiCcCandidate& rhs
) {
    return sameIdentity(
               lhs.destination.identity,
               rhs.destination.identity
           ) &&
           lhs.destination.routeValidity == rhs.destination.routeValidity;
}

inline bool sameCandidate(
    const core::state::shared::MidiCcCandidate& lhs,
    const core::state::shared::MidiCcCandidate& rhs
) {
    return sameDestination(lhs, rhs) &&
           lhs.author.candidateClass == rhs.author.candidateClass &&
           lhs.author.stableAddress == rhs.author.stableAddress &&
           lhs.localValue == rhs.localValue;
}

}  // namespace core::sequencer::midi_cc

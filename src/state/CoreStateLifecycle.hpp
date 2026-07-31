#pragma once

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state {

struct CoreState;

/**
 * Centralizes CoreState lifecycle side effects.
 *
 * Runtime code calls CoreState's public methods; this helper keeps delayed
 * Pending sequencer apply, factory reset, and transient UI reset behavior live
 * in one implementation boundary.
 */
struct CoreStateLifecycle {
    static void update(CoreState& state);
    static void flush(CoreState& state);
    static void flushProjectMutationCoalescing(CoreState& state);
    static void factoryReset(CoreState& state);
    static void resetStandaloneTransientUi(CoreState& state);
    static void resetMusicalProject(CoreState& state);

    [[nodiscard]] static bool queuePendingSequencerApply(
        CoreState& state,
        sequencer::SequencerState& staged,
        bool merge = false
    );
    [[nodiscard]] static bool queuePendingSequencerBankApply(
        CoreState& state,
        sequencer::SequencerTrackBankState& stagedBank,
        sequencer::SequencerState& staged
    );
    static void clearPendingSequencerApply(CoreState& state);

private:
    static void applyPendingSequencerApplyIfReady(CoreState& state);
    static void updateMutationCoalescers_(CoreState& state);
    static void flushMutationCoalescers_(CoreState& state);
    static void persistFactoryDefaults_(CoreState& state);
    static void resetMacroDomain_(CoreState& state);
    static void resetSequencerDomain_(CoreState& state);
    static void resetUiState_(CoreState& state);
};

}  // namespace core::state

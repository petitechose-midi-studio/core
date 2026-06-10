#pragma once

#include "sequencer/SequencerState.hpp"
#include "sequencer/SequencerTrackBankState.hpp"

namespace core::state {

struct CoreState;

/**
 * Centralizes CoreState lifecycle side effects.
 *
 * Runtime code calls CoreState's public methods; this helper keeps delayed
 * persistence, pending sequencer apply, factory reset, and transient UI reset
 * behavior in one implementation boundary.
 */
struct CoreStateLifecycle {
    static void update(CoreState& state);
    static void flush(CoreState& state);
    static void flushAutoPersist(CoreState& state);
    static void factoryReset(CoreState& state);
    static void resetStandaloneTransientUi(CoreState& state);
    static void resetMusicalProject(CoreState& state);

    static void queuePendingSequencerApply(CoreState& state,
                                           const sequencer::SequencerState& staged,
                                           bool merge = false);
    static void queuePendingSequencerBankApply(CoreState& state,
                                               const sequencer::SequencerTrackBankState& stagedBank,
                                               const sequencer::SequencerState& staged);
    static void clearPendingSequencerApply(CoreState& state);

private:
    static void applyPendingSequencerApplyIfReady(CoreState& state);
    static void updateAutoPersist_(CoreState& state);
    static void updatePendingSharedTrackPersist_(CoreState& state);
    static void flushAutoPersist_(CoreState& state);
    static void flushPendingSharedTrackPersist_(CoreState& state);
    static void persistFactoryDefaults_(CoreState& state);
    static void resetMacroDomain_(CoreState& state);
    static void resetSequencerDomain_(CoreState& state);
    static void resetUiState_(CoreState& state);
};

}  // namespace core::state

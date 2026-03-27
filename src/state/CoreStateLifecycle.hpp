#pragma once

#include "sequencer/SequencerState.hpp"

namespace core::state {

struct CoreState;

struct CoreStateLifecycle {
    static void update(CoreState& state);
    static void flush(CoreState& state);
    static void factoryReset(CoreState& state);

    static void queuePendingSequencerApply(CoreState& state,
                                           const sequencer::SequencerState& staged,
                                           bool merge = false);
    static void clearPendingSequencerApply(CoreState& state);

private:
    static void applyPendingSequencerApplyIfReady(CoreState& state);
};

}  // namespace core::state

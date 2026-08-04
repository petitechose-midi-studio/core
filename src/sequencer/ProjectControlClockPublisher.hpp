#pragma once

#include <cstdint>

#include "state/modulation/ProjectControlRuntime.hpp"

namespace core::sequencer {

/**
 * Publishes the Sequencer clock in Project Control's musical time domain.
 *
 * `publishLocked` is called while the owning realtime gate holds its interrupt
 * guard so the transport-resume fence and the clock snapshot change
 * atomically. Readers acquire their own short guard through `snapshot`.
 */
class ProjectControlClockPublisher final {
public:
    void publishLocked(
        uint32_t sequencerTick,
        bool playing,
        uint32_t nowUs,
        uint32_t sequencerTickPeriodUs
    );
    [[nodiscard]] core::state::modulation::ProjectControlTimeSnapshot
        snapshot() const;
    void reset();

private:
    core::state::modulation::ProjectControlTimeSnapshot snapshot_{};
    uint32_t tick_started_us_ = 0U;
    uint32_t last_sequencer_tick_ = 0U;
    bool initialized_ = false;
};

}  // namespace core::sequencer

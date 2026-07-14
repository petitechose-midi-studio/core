#pragma once

#include <cstdint>

namespace core::state::sequencer {

enum class SequencerContentViewKind : uint8_t {
    ROOT = 0,
    MICRO_SEQUENCE,
    CYCLE_STATES,
};

}  // namespace core::state::sequencer

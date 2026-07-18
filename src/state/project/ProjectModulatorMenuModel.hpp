#pragma once

#include <cstdint>

#include "state/modulation/ProjectModulationState.hpp"

namespace core::state::project::modulators {

enum class SourceDetailItem : uint8_t {
    PREVIEW = 0,
    ENABLED,
    SHAPE,
    RATE,
    TIMING,
    PHASE,
    RETRIGGER,
    LENGTH,
    SOURCE_DOMAIN,
    DESTINATIONS,
    OPTIONS,
    RENAME,
    ATTACK,
    DECAY,
    SUSTAIN,
    RELEASE,
    TRIGGER,
    CURVE,
    DEPTH,
};

enum class TriggerDetailItem : uint8_t {
    TRACK = 0,
    CHANNEL,
    NOTE,
};

inline constexpr uint8_t MODULATOR_SOURCE_KIND_COUNT = 2U;
inline constexpr uint8_t MODULATOR_TRIGGER_DETAIL_COUNT = 3U;

struct SourceDetailLayout {
    static constexpr uint8_t CAPACITY = 9;
    SourceDetailItem items[CAPACITY]{};
    uint8_t count = 0;

    void append(SourceDetailItem item) {
        if (count < CAPACITY) items[count++] = item;
    }

    [[nodiscard]] SourceDetailItem at(uint8_t index) const {
        return items[index < count ? index : 0U];
    }
};

SourceDetailLayout sourceDetailLayout(
    core::state::modulation::ModulatorKind kind
);

SourceDetailLayout sourceOptionsLayout(
    core::state::modulation::ModulatorKind kind
);

/**
 * Destination-first creation surface.
 *
 * Source parameters stay on the same visual/editor grammar as the durable
 * workspace. Only the implied destination's Depth is added; routing and
 * source-management actions remain hidden until Apply.
 */
SourceDetailLayout sourceAuditionLayout(
    core::state::modulation::ModulatorKind kind
);

SourceDetailLayout sourceAuditionOptionsLayout(
    core::state::modulation::ModulatorKind kind
);

uint16_t sourceDestinationCount(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId
);

const core::state::modulation::ModulationBindingState*
sourceBindingAtOrdinal(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId,
    uint16_t ordinal
);

core::state::modulation::ModulationBindingState*
sourceBindingAtOrdinal(
    core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId,
    uint16_t ordinal
);

}  // namespace core::state::project::modulators

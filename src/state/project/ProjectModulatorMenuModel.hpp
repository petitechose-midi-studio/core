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
    REACH,
    DESTINATIONS,
};

enum class ReachChoiceKind : uint8_t {
    TIGHTEST = 0,
    PROJECT,
    SPLIT_TRACK,
};

struct ReachChoice {
    ReachChoiceKind kind = ReachChoiceKind::TIGHTEST;
    uint8_t track = 0;
    uint16_t destinationCount = 0;
};

struct ReachChoiceLayout {
    static constexpr uint8_t CAPACITY =
        static_cast<uint8_t>(
            2U + core::state::modulation::PROJECT_MODULATION_TRACK_COUNT
        );
    ReachChoice choices[CAPACITY]{};
    uint8_t count = 0;

    void append(ReachChoice choice) {
        if (count < CAPACITY) choices[count++] = choice;
    }

    [[nodiscard]] ReachChoice at(uint8_t index) const {
        return choices[index < count ? index : 0U];
    }
};

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

uint16_t sourceDestinationCount(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId
);

uint16_t sourceDestinationCountOnTrack(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId,
    uint8_t track
);

core::state::modulation::ModulatorReach sourcePartitionReach(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId,
    uint8_t splitTrack,
    bool selectedPartition
);

ReachChoiceLayout sourceReachChoiceLayout(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId
);

core::state::modulation::ModulatorReach tightestSourceReach(
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

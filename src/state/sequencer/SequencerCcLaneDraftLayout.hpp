#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "state/sequencer/SequencerUiState.hpp"

namespace core::state::sequencer {

struct SequencerCcLaneDraftLayout {
    static constexpr size_t CAPACITY =
        static_cast<size_t>(SequencerCcLaneDraftField::COUNT);

    std::array<SequencerCcLaneDraftField, CAPACITY> fields{};
    uint8_t count = 0;

    [[nodiscard]] SequencerCcLaneDraftField fieldAt(uint8_t index) const {
        return count == 0
            ? SequencerCcLaneDraftField::CONTROLLER
            : fields[index < count ? index : static_cast<uint8_t>(count - 1U)];
    }

    [[nodiscard]] uint8_t indexOf(SequencerCcLaneDraftField field) const {
        for (uint8_t index = 0; index < count; ++index) {
            if (fields[index] == field) return index;
        }
        return 0;
    }
};

inline SequencerCcLaneDraftLayout buildSequencerCcLaneDraftLayout(
    SequencerCcLaneRoutePolicy routePolicy,
    bool advanced
) {
    SequencerCcLaneDraftLayout layout{};
    auto append = [&](SequencerCcLaneDraftField field) {
        if (layout.count >= layout.fields.size()) return;
        layout.fields[layout.count++] = field;
    };

    append(SequencerCcLaneDraftField::CONTROLLER);
    append(SequencerCcLaneDraftField::ROUTE_POLICY);
    if (routePolicy == SequencerCcLaneRoutePolicy::PINNED) {
        append(SequencerCcLaneDraftField::PINNED_CHANNEL);
    }
    if (advanced) {
        append(SequencerCcLaneDraftField::MINIMUM);
        append(SequencerCcLaneDraftField::MAXIMUM);
        append(SequencerCcLaneDraftField::INITIAL);
    }
    append(SequencerCcLaneDraftField::ADVANCED);
    return layout;
}

}  // namespace core::state::sequencer

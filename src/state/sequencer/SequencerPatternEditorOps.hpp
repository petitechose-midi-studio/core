#pragma once

#include <cstdint>

#include "state/sequencer/SequencerPatternEditorState.hpp"

namespace core::state::sequencer {

struct SequencerState;

struct SequencerPatternEditorValueRange {
    int16_t minimum = 0;
    int16_t maximum = 0;

    [[nodiscard]] constexpr bool editable() const {
        return maximum >= minimum;
    }

    [[nodiscard]] constexpr uint16_t count() const {
        return editable()
            ? static_cast<uint16_t>(maximum - minimum) + 1U
            : 0U;
    }
};

/** Open the root Pattern editor on the current eight-step Page. */
bool openPatternEditor(
    SequencerState& sequencer,
    uint8_t ownerTrack
);

void closePatternEditor(SequencerState& sequencer);

/** Wrapped navigation over the fields visible in the current progressive layer. */
bool movePatternEditorField(SequencerState& sequencer, int direction);

uint8_t patternEditorVisibleFieldCount(const SequencerState& sequencer);
SequencerPatternEditorField patternEditorVisibleFieldAt(
    const SequencerState& sequencer,
    uint8_t index
);

/** Progressive layer list: Notes, occupied lanes, one + Lane, then Region. */
uint8_t patternEditorVisibleLayerCount(const SequencerState& sequencer);
SequencerPatternEditorLayer patternEditorVisibleLayerAt(
    const SequencerState& sequencer,
    uint8_t index
);
bool patternEditorLayerIsAdd(
    const SequencerState& sequencer,
    SequencerPatternEditorLayer layer
);

/** Wrapped navigation over the progressive layer list. */
bool movePatternEditorLayer(SequencerState& sequencer, int direction);

/** Wrapped navigation over active eight-step windows without remounting. */
bool movePatternEditorWindow(SequencerState& sequencer, int direction);

bool setPatternEditorNavigationMode(
    SequencerState& sequencer,
    SequencerPatternEditorNavigationMode mode
);

SequencerPatternEditorValueRange patternEditorValueRange(
    const SequencerState& sequencer,
    SequencerPatternEditorField field
);

int16_t patternEditorFieldValue(
    const SequencerState& sequencer,
    SequencerPatternEditorField field
);

/**
 * Apply one exact native-unit value through canonical Pattern authorities.
 */
bool setPatternEditorFieldValue(
    SequencerState& sequencer,
    SequencerPatternEditorField field,
    int16_t value
);

}  // namespace core::state::sequencer

#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state::sequencer {

/** Stable top-level controls exposed by the retained Pattern Editor. */
enum class SequencerPatternEditorField : uint8_t {
    LENGTH = 0,
    DIVISION,
    SWING,
    NUDGE,
    PLAY_START,
    LOOP_START,
    LOOP_END,
    COUNT,
};

/** Mutually exclusive timeline layers. */
enum class SequencerPatternEditorLayer : uint8_t {
    NOTES = 0,
    CC1,
    CC2,
    CC3,
    CC4,
    REGION,
    COUNT,
};

/** Temporary NAV ownership while a physical modifier is held. */
enum class SequencerPatternEditorNavigationMode : uint8_t {
    FIELDS = 0,
    WINDOWS,
    LAYERS,
};

/**
 * Compact, session-only state for one retained Pattern Editor surface.
 *
 * Pattern data remains in SequencerPatternState.  This object only retains the
 * exact owner and viewport/focus facts needed to keep the editor mounted while
 * the musician traverses eight-step windows.
 */
struct SequencerPatternEditorState {
    // Overlay ownership and its presenter are the only subscribers. Every
    // other field is polled through the plain revision, avoiding a second
    // ~200-byte Signal.
    oc::state::Signal<bool, 2> active{false};
    uint32_t revision = 0;

    SequencerPatternEditorField focusedField =
        SequencerPatternEditorField::LENGTH;
    SequencerPatternEditorLayer focusedLayer =
        SequencerPatternEditorLayer::NOTES;
    SequencerPatternEditorNavigationMode navigationMode =
        SequencerPatternEditorNavigationMode::FIELDS;
    uint8_t windowStart = 0;
    uint8_t ownerTrack = 0;
    void bump();
    void open(uint8_t track, uint8_t firstStep);
    void close();
    void reset();
};

static_assert(sizeof(SequencerPatternEditorState) <= 128U);

}  // namespace core::state::sequencer

#include "state/sequencer/SequencerPatternEditorOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/util/Index.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerPatternRegionOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {
namespace {

constexpr uint8_t kWindowSize = SequencerState::STEPS_PER_PAGE;

FLASHMEM uint8_t windowCount(const SequencerState& sequencer) {
    const uint8_t length = sequencer.pattern.length.get();
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(length) + kWindowSize - 1U) / kWindowSize
    );
}

FLASHMEM uint8_t normalizedWindowStart(
    const SequencerState& sequencer,
    uint8_t requestedStart
) {
    const uint8_t count = std::max<uint8_t>(windowCount(sequencer), 1U);
    const uint8_t requestedWindow = static_cast<uint8_t>(requestedStart / kWindowSize);
    const uint8_t clampedWindow = std::min<uint8_t>(
        requestedWindow,
        static_cast<uint8_t>(count - 1U)
    );
    return static_cast<uint8_t>(clampedWindow * kWindowSize);
}

FLASHMEM bool launchStartDistinct(const SequencerState& sequencer) {
    const auto region = patternPlaybackRegion(sequencer.pattern);
    return region.playStart != region.loopStart;
}

FLASHMEM bool laneOccupied(const SequencerState& sequencer, uint8_t lane) {
    const auto* bank = sequencer.pattern.ccLanes.get();
    return bank != nullptr && lane < bank->lanes.size() &&
           bank->lanes[lane].occupied;
}

template <typename Enum>
FLASHMEM Enum wrappedEnum(Enum current, int direction, uint8_t count) {
    const int next = oc::util::wrapIndex(
        static_cast<int>(current) + (direction > 0 ? 1 : -1),
        count
    );
    return static_cast<Enum>(next);
}

FLASHMEM bool setRegionValue(
    SequencerPatternState& pattern,
    SequencerPatternEditorField field,
    int16_t value
) {
    auto region = patternPlaybackRegion(pattern);
    switch (field) {
        case SequencerPatternEditorField::PLAY_START:
            region.playStart = static_cast<uint8_t>(std::clamp<int>(
                value,
                0,
                region.loopStart
            ));
            break;
        case SequencerPatternEditorField::LOOP_START:
            region.loopStart = static_cast<uint8_t>(std::clamp<int>(
                value,
                region.playStart,
                static_cast<int>(region.loopEnd) - 1
            ));
            break;
        case SequencerPatternEditorField::LOOP_END:
            region.loopEnd = static_cast<uint8_t>(std::clamp<int>(
                value,
                static_cast<int>(region.loopStart) + 1,
                region.contentLength
            ));
            break;
        default:
            return false;
    }
    return setPatternPlaybackRegion(pattern, region);
}

}  // namespace

FLASHMEM bool openPatternEditor(
    SequencerState& sequencer,
    uint8_t ownerTrack
) {
    if (!isRootContentView(sequencer) || sequencer.patternEditor.active.get()) {
        return false;
    }
    const uint8_t start = normalizedWindowStart(
        sequencer,
        sequencer.pageStartStepClamped(sequencer.page.get())
    );
    sequencer.patternEditor.open(ownerTrack, start);
    sequencer.page.set(static_cast<uint8_t>(start / kWindowSize));
    const uint8_t windowEnd = static_cast<uint8_t>(std::min<uint16_t>(
        sequencer.pattern.length.get(),
        static_cast<uint16_t>(start) + kWindowSize
    ));
    if (sequencer.focusedStep.get() < start ||
        sequencer.focusedStep.get() >= windowEnd) {
        sequencer.focusedStep.set(start);
    }
    return true;
}

FLASHMEM void closePatternEditor(SequencerState& sequencer) {
    sequencer.patternEditor.close();
}

FLASHMEM bool movePatternEditorField(
    SequencerState& sequencer,
    int direction
) {
    auto& editor = sequencer.patternEditor;
    if (!editor.active.get() || direction == 0) return false;
    const uint8_t count = patternEditorVisibleFieldCount(sequencer);
    uint8_t current = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        if (patternEditorVisibleFieldAt(sequencer, index) ==
            editor.focusedField) {
            current = index;
            break;
        }
    }
    const int next = oc::util::wrapIndex(
        static_cast<int>(current) + (direction > 0 ? 1 : -1),
        count
    );
    editor.focusedField = patternEditorVisibleFieldAt(
        sequencer,
        static_cast<uint8_t>(next)
    );
    editor.bump();
    return true;
}

FLASHMEM uint8_t patternEditorVisibleFieldCount(
    const SequencerState& sequencer
) {
    if (sequencer.patternEditor.focusedLayer !=
        SequencerPatternEditorLayer::REGION) {
        return 4U;
    }
    return launchStartDistinct(sequencer) ? 3U : 2U;
}

FLASHMEM SequencerPatternEditorField patternEditorVisibleFieldAt(
    const SequencerState& sequencer,
    uint8_t index
) {
    if (sequencer.patternEditor.focusedLayer ==
        SequencerPatternEditorLayer::REGION) {
        if (launchStartDistinct(sequencer)) {
            constexpr SequencerPatternEditorField fields[] = {
                SequencerPatternEditorField::PLAY_START,
                SequencerPatternEditorField::LOOP_START,
                SequencerPatternEditorField::LOOP_END,
            };
            return index < 3U ? fields[index] : fields[0];
        }
        constexpr SequencerPatternEditorField fields[] = {
            SequencerPatternEditorField::LOOP_START,
            SequencerPatternEditorField::LOOP_END,
        };
        return index < 2U ? fields[index] : fields[0];
    }
    constexpr SequencerPatternEditorField fields[] = {
        SequencerPatternEditorField::LENGTH,
        SequencerPatternEditorField::DIVISION,
        SequencerPatternEditorField::SWING,
        SequencerPatternEditorField::NUDGE,
    };
    return index < 4U ? fields[index] : fields[0];
}

FLASHMEM bool movePatternEditorLayer(
    SequencerState& sequencer,
    int direction
) {
    auto& editor = sequencer.patternEditor;
    if (!editor.active.get() || direction == 0) return false;
    const uint8_t count = patternEditorVisibleLayerCount(sequencer);
    uint8_t current = 0U;
    for (uint8_t index = 0U; index < count; ++index) {
        if (patternEditorVisibleLayerAt(sequencer, index) == editor.focusedLayer) {
            current = index;
            break;
        }
    }
    const int next = oc::util::wrapIndex(
        static_cast<int>(current) + (direction > 0 ? 1 : -1),
        count
    );
    editor.focusedLayer = patternEditorVisibleLayerAt(
        sequencer,
        static_cast<uint8_t>(next)
    );
    editor.focusedField = patternEditorVisibleFieldAt(sequencer, 0U);
    editor.bump();
    return true;
}

FLASHMEM uint8_t patternEditorVisibleLayerCount(
    const SequencerState& sequencer
) {
    uint8_t occupied = 0U;
    for (uint8_t lane = 0U; lane < 4U; ++lane) {
        if (laneOccupied(sequencer, lane)) ++occupied;
    }
    const bool hasFreeLane = occupied < 4U;
    return static_cast<uint8_t>(2U + occupied + (hasFreeLane ? 1U : 0U));
}

FLASHMEM SequencerPatternEditorLayer patternEditorVisibleLayerAt(
    const SequencerState& sequencer,
    uint8_t index
) {
    if (index == 0U) return SequencerPatternEditorLayer::NOTES;
    uint8_t dense = 1U;
    for (uint8_t lane = 0U; lane < 4U; ++lane) {
        if (!laneOccupied(sequencer, lane)) continue;
        if (index == dense) {
            return static_cast<SequencerPatternEditorLayer>(
                static_cast<uint8_t>(SequencerPatternEditorLayer::CC1) + lane
            );
        }
        ++dense;
    }
    if (dense < patternEditorVisibleLayerCount(sequencer) - 1U) {
        for (uint8_t lane = 0U; lane < 4U; ++lane) {
            if (!laneOccupied(sequencer, lane)) {
                if (index == dense) {
                    return static_cast<SequencerPatternEditorLayer>(
                        static_cast<uint8_t>(SequencerPatternEditorLayer::CC1) + lane
                    );
                }
                break;
            }
        }
    }
    return SequencerPatternEditorLayer::REGION;
}

FLASHMEM bool patternEditorLayerIsAdd(
    const SequencerState& sequencer,
    SequencerPatternEditorLayer layer
) {
    const auto raw = static_cast<uint8_t>(layer);
    const auto first = static_cast<uint8_t>(SequencerPatternEditorLayer::CC1);
    if (raw < first || raw >= first + 4U) return false;
    return !laneOccupied(sequencer, static_cast<uint8_t>(raw - first));
}

FLASHMEM bool movePatternEditorWindow(
    SequencerState& sequencer,
    int direction
) {
    auto& editor = sequencer.patternEditor;
    const uint8_t count = windowCount(sequencer);
    if (!editor.active.get() || direction == 0 || count <= 1U) return false;

    const int current = static_cast<int>(editor.windowStart / kWindowSize);
    const int next = oc::util::wrapIndex(
        current + (direction > 0 ? 1 : -1),
        count
    );
    editor.windowStart = static_cast<uint8_t>(next * kWindowSize);
    sequencer.page.set(static_cast<uint8_t>(next));
    sequencer.focusedStep.set(editor.windowStart);
    editor.bump();
    return true;
}

FLASHMEM bool setPatternEditorNavigationMode(
    SequencerState& sequencer,
    SequencerPatternEditorNavigationMode mode
) {
    auto& editor = sequencer.patternEditor;
    if (!editor.active.get() || editor.navigationMode == mode) return false;
    editor.navigationMode = mode;
    editor.bump();
    return true;
}

FLASHMEM SequencerPatternEditorValueRange patternEditorValueRange(
    const SequencerState& sequencer,
    SequencerPatternEditorField field
) {
    const auto region = patternPlaybackRegion(sequencer.pattern);
    switch (field) {
        case SequencerPatternEditorField::LENGTH:
            return {1, SequencerState::MAX_STEPS};
        case SequencerPatternEditorField::DIVISION:
            return {
                0,
                static_cast<int16_t>(PATTERN_STEPS_PER_BEAT_CHOICES.size() - 1U),
            };
        case SequencerPatternEditorField::SWING:
            return {
                SequencerPatternState::MIN_PATTERN_SWING_OFFSET_PERCENT,
                SequencerPatternState::MAX_PATTERN_SWING_OFFSET_PERCENT,
            };
        case SequencerPatternEditorField::NUDGE:
            return {
                SequencerPatternState::MIN_PATTERN_NUDGE_PERCENT,
                SequencerPatternState::MAX_PATTERN_NUDGE_PERCENT,
            };
        case SequencerPatternEditorField::PLAY_START:
            return {0, region.loopStart};
        case SequencerPatternEditorField::LOOP_START:
            return {
                region.playStart,
                static_cast<int16_t>(region.loopEnd - 1U),
            };
        case SequencerPatternEditorField::LOOP_END:
            return {
                static_cast<int16_t>(region.loopStart + 1U),
                region.contentLength,
            };
        case SequencerPatternEditorField::COUNT:
        default:
            return {1, 0};
    }
}

FLASHMEM int16_t patternEditorFieldValue(
    const SequencerState& sequencer,
    SequencerPatternEditorField field
) {
    const auto region = patternPlaybackRegion(sequencer.pattern);
    switch (field) {
        case SequencerPatternEditorField::LENGTH:
            return region.contentLength;
        case SequencerPatternEditorField::DIVISION:
            for (uint8_t index = 0U;
                 index < PATTERN_STEPS_PER_BEAT_CHOICES.size();
                 ++index) {
                if (PATTERN_STEPS_PER_BEAT_CHOICES[index] ==
                    sequencer.pattern.stepsPerBeat.get()) {
                    return index;
                }
            }
            return 1;
        case SequencerPatternEditorField::SWING:
            return sequencer.pattern.swingOffsetPercent.get();
        case SequencerPatternEditorField::NUDGE:
            return sequencer.pattern.patternNudgePercent.get();
        case SequencerPatternEditorField::PLAY_START:
            return region.playStart;
        case SequencerPatternEditorField::LOOP_START:
            return region.loopStart;
        case SequencerPatternEditorField::LOOP_END:
            return region.loopEnd;
        case SequencerPatternEditorField::COUNT:
        default:
            return 0;
    }
}

FLASHMEM bool setPatternEditorFieldValue(
    SequencerState& sequencer,
    SequencerPatternEditorField field,
    int16_t value
) {
    auto& editor = sequencer.patternEditor;
    if (!editor.active.get()) return false;

    const auto range = patternEditorValueRange(sequencer, field);
    if (!range.editable()) return false;
    const int16_t clamped = std::clamp(value, range.minimum, range.maximum);
    bool changed = false;

    switch (field) {
        case SequencerPatternEditorField::LENGTH:
            changed = resizePatternContent(
                sequencer.pattern,
                static_cast<uint8_t>(clamped)
            );
            if (changed) {
                editor.windowStart = normalizedWindowStart(
                    sequencer,
                    editor.windowStart
                );
                sequencer.page.set(static_cast<uint8_t>(
                    editor.windowStart / kWindowSize
                ));
                if (sequencer.focusedStep.get() >= sequencer.pattern.length.get()) {
                    sequencer.focusedStep.set(editor.windowStart);
                }
            }
            break;
        case SequencerPatternEditorField::DIVISION: {
            const uint8_t next = PATTERN_STEPS_PER_BEAT_CHOICES[
                static_cast<uint8_t>(clamped)
            ];
            if (sequencer.pattern.stepsPerBeat.get() != next) {
                sequencer.pattern.stepsPerBeat.set(next);
                changed = true;
            }
            break;
        }
        case SequencerPatternEditorField::SWING:
            changed = sequencer.setPatternSwingOffsetPercent(clamped);
            break;
        case SequencerPatternEditorField::NUDGE:
            changed = sequencer.setPatternNudgePercent(clamped);
            break;
        case SequencerPatternEditorField::PLAY_START:
        case SequencerPatternEditorField::LOOP_START:
        case SequencerPatternEditorField::LOOP_END:
            changed = setRegionValue(sequencer.pattern, field, clamped);
            break;
        case SequencerPatternEditorField::COUNT:
        default:
            return false;
    }

    if (changed) editor.bump();
    return changed;
}

}  // namespace core::state::sequencer

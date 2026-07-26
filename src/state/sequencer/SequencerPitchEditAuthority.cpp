#include "state/sequencer/SequencerPitchEditAuthority.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer::content_view_internal {

FLASHMEM bool usesScaleDegreePitchEdit(
    StepProperty property,
    SequencerPitchEditMode mode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    return property == StepProperty::NOTE &&
           (scaleSettings.isConstrained() || mode == SequencerPitchEditMode::SCALE_DEGREES) &&
           scaleSettings.type != oc::note::sequencer::StepSequencerScaleType::Chromatic;
}

FLASHMEM int countScaleNotes(oc::note::sequencer::StepSequencerScaleSettings scaleSettings) {
    scaleSettings.clamp();
    int count = 0;
    for (int note = 0; note <= 127; ++note) {
        if (oc::note::sequencer::scaleContainsNote(scaleSettings, static_cast<uint8_t>(note))) {
            ++count;
        }
    }
    return std::max(count, 1);
}

FLASHMEM int scaleDegreeIndexForNote(
    uint8_t note,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    const uint8_t resolved =
        oc::note::sequencer::resolveScaleNote(note, scaleSettings).outputNote;
    int index = 0;
    for (int candidate = 0; candidate <= 127; ++candidate) {
        if (!oc::note::sequencer::scaleContainsNote(
                scaleSettings,
                static_cast<uint8_t>(candidate)
            )) {
            continue;
        }
        if (candidate >= resolved) return index;
        ++index;
    }
    return std::max(0, index - 1);
}

FLASHMEM uint8_t scaleNoteForDegreeIndex(
    int index,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    const int clampedIndex = std::clamp(index, 0, countScaleNotes(scaleSettings) - 1);
    int current = 0;
    for (int note = 0; note <= 127; ++note) {
        if (!oc::note::sequencer::scaleContainsNote(
                scaleSettings,
                static_cast<uint8_t>(note)
            )) {
            continue;
        }
        if (current == clampedIndex) return static_cast<uint8_t>(note);
        ++current;
    }
    return 0;
}

}  // namespace core::state::sequencer::content_view_internal

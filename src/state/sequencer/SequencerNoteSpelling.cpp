#include "state/sequencer/SequencerNoteSpelling.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

namespace core::state::sequencer::note_spelling {
namespace {

FLASHMEM uint8_t parentMajorRoot(
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    using Scale = oc::note::sequencer::StepSequencerScaleType;
    uint8_t offset = 0;
    switch (settings.type) {
        case Scale::NaturalMinor:
        case Scale::HarmonicMinor:
        case Scale::MelodicMinor:
        case Scale::MinorPentatonic:
        case Scale::Blues:
            offset = 3;
            break;
        case Scale::Dorian:
            offset = 10;
            break;
        case Scale::Phrygian:
            offset = 8;
            break;
        case Scale::Lydian:
            offset = 7;
            break;
        case Scale::Mixolydian:
            offset = 5;
            break;
        case Scale::Locrian:
            offset = 1;
            break;
        default:
            break;
    }
    return static_cast<uint8_t>((settings.root + offset) % 12U);
}

FLASHMEM bool preferFlatSpelling(
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    if (settings.type == oc::note::sequencer::StepSequencerScaleType::Chromatic) {
        return false;
    }
    switch (parentMajorRoot(settings)) {
        case 1:   // Db
        case 3:   // Eb
        case 5:   // F
        case 8:   // Ab
        case 10:  // Bb
            return true;
        default:
            return false;
    }
}

FLASHMEM bool usesSevenLetterSpelling(
    oc::note::sequencer::StepSequencerScaleType type
) {
    using Scale = oc::note::sequencer::StepSequencerScaleType;
    return type >= Scale::Major && type <= Scale::Locrian;
}

FLASHMEM int scaleDegreeForPitchClass(
    uint8_t pitchClass,
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    settings.clamp();
    const uint8_t relative =
        static_cast<uint8_t>((pitchClass + 12U - settings.root) % 12U);
    const uint16_t mask = oc::note::sequencer::scaleMask(settings.type);
    int degree = 0;
    for (uint8_t interval = 0; interval < 12U; ++interval) {
        if ((mask & static_cast<uint16_t>(1U << interval)) == 0) continue;
        if (interval == relative) return degree;
        ++degree;
    }
    return -1;
}

FLASHMEM const char* diatonicPitchClassLabel(
    uint8_t pitchClass,
    oc::note::sequencer::StepSequencerScaleSettings settings,
    int scaleDegree
) {
    constexpr uint8_t NATURAL_PITCH_CLASSES[] = {0, 2, 4, 5, 7, 9, 11};
    constexpr uint8_t SHARP_ROOT_LETTERS[] = {
        0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6,
    };
    constexpr uint8_t FLAT_ROOT_LETTERS[] = {
        0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6,
    };
    static const char SPELLINGS[7][5][4] PROGMEM = {
        {"Cbb", "Cb", "C", "C#", "C##"},
        {"Dbb", "Db", "D", "D#", "D##"},
        {"Ebb", "Eb", "E", "E#", "E##"},
        {"Fbb", "Fb", "F", "F#", "F##"},
        {"Gbb", "Gb", "G", "G#", "G##"},
        {"Abb", "Ab", "A", "A#", "A##"},
        {"Bbb", "Bb", "B", "B#", "B##"},
    };

    const bool flats = preferFlatSpelling(settings);
    const uint8_t rootLetter = flats
        ? FLAT_ROOT_LETTERS[settings.root % 12U]
        : SHARP_ROOT_LETTERS[settings.root % 12U];
    const uint8_t letter = static_cast<uint8_t>(
        (rootLetter + static_cast<uint8_t>(scaleDegree)) % 7U
    );
    int accidental =
        static_cast<int>(pitchClass % 12U) -
        static_cast<int>(NATURAL_PITCH_CLASSES[letter]);
    if (accidental > 6) accidental -= 12;
    if (accidental < -6) accidental += 12;
    if (accidental < -2 || accidental > 2) return nullptr;
    return SPELLINGS[letter][static_cast<size_t>(accidental + 2)];
}

}  // namespace

FLASHMEM const char* pitchClassLabel(
    uint8_t pitchClass,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    static const char SHARP_LABELS[12][3] PROGMEM = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
    };
    static const char FLAT_LABELS[12][3] PROGMEM = {
        "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B",
    };
    if (usesSevenLetterSpelling(scaleSettings.type)) {
        const int degree = scaleDegreeForPitchClass(pitchClass, scaleSettings);
        if (degree >= 0) {
            const char* spelling =
                diatonicPitchClassLabel(pitchClass, scaleSettings, degree);
            if (spelling != nullptr) return spelling;
        }
    }
    return preferFlatSpelling(scaleSettings)
        ? FLAT_LABELS[pitchClass % 12U]
        : SHARP_LABELS[pitchClass % 12U];
}

FLASHMEM void formatNoteName(
    char* out,
    size_t outSize,
    uint8_t note,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (!out || outSize == 0) return;
    size_t pos = oc::type::text::appendString(
        out,
        outSize,
        0,
        pitchClassLabel(static_cast<uint8_t>(note % 12U), scaleSettings)
    );
    pos = oc::type::text::appendSigned(
        out,
        outSize,
        pos,
        static_cast<int>(note) / 12 - 1
    );
    oc::type::text::terminate(out, outSize, pos);
}

}  // namespace core::state::sequencer::note_spelling

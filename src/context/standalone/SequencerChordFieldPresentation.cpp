#include "context/standalone/SequencerChordFieldPresentation.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <oc/type/TextFormat.hpp>

#include "state/sequencer/SequencerNoteSpelling.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"

namespace core::context::standalone::sequencer_chord_field_presentation {
namespace {

using Field = core::state::sequencer::SequencerChordEditField;
using Tone = core::ui::sequencer::semantic::Tone;

FLASHMEM void copyText(
    char* out,
    size_t outSize,
    const char* text
) {
    if (!out || outSize == 0) return;
    const char* source = text ? text : "";
    std::strncpy(out, source, outSize - 1);
    out[outSize - 1] = '\0';
}

FLASHMEM const char* voicingLabel(
    oc::note::sequencer::StepSequencerChordVoicing voicing
) {
    using Voicing = oc::note::sequencer::StepSequencerChordVoicing;
    switch (voicing) {
        case Voicing::Open: return "Open";
        case Voicing::Wide: return "Wide";
        case Voicing::Close:
        default: return "Close";
    }
}

FLASHMEM void formatInversion(
    char* out,
    size_t outSize,
    uint8_t inversion
) {
    if (inversion == 0) {
        copyText(out, outSize, "Root");
        return;
    }
    const char* suffix = "th";
    if (inversion == 1) suffix = "st";
    else if (inversion == 2) suffix = "nd";
    else if (inversion == 3) suffix = "rd";
    std::snprintf(
        out,
        outSize,
        "%u%s",
        static_cast<unsigned>(inversion),
        suffix
    );
}

FLASHMEM void formatSigned(
    char* out,
    size_t outSize,
    int value,
    const char* suffix = ""
) {
    if (!out || outSize == 0) return;
    std::snprintf(
        out,
        outSize,
        "%+d%s",
        value,
        suffix ? suffix : ""
    );
}

FLASHMEM uint8_t clampMidi(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 127));
}

FLASHMEM oc::note::sequencer::StepSequencerChordFormula chordFormula(
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    return oc::note::sequencer::resolveChordFormula(
        chord.spec,
        chord.intervalsUseScaleDegrees
    );
}

FLASHMEM void appendFormulaInterval(
    char* out,
    size_t outSize,
    size_t& pos,
    int16_t interval,
    bool scaleBased
) {
    if (scaleBased) {
        pos = oc::type::text::appendUnsigned(
            out,
            outSize,
            pos,
            static_cast<unsigned>(std::max<int16_t>(0, interval) + 1)
        );
        return;
    }
    pos = oc::type::text::appendSigned(
        out,
        outSize,
        pos,
        interval
    );
}

}  // namespace

FLASHMEM const char* modeLabel(
    oc::note::sequencer::StepSequencerChordMode mode
) {
    using Mode = oc::note::sequencer::StepSequencerChordMode;
    switch (mode) {
        case Mode::Inherit: return "Inherit";
        case Mode::Local: return "Local";
        case Mode::Single:
        default: return "Single";
    }
}

FLASHMEM const char* shapeLabel(
    oc::note::sequencer::StepSequencerChordHarmony harmony
) {
    using Harmony = oc::note::sequencer::StepSequencerChordHarmony;
    switch (harmony) {
        case Harmony::DiatonicTriad: return "Triad";
        case Harmony::DiatonicSeventh: return "Seventh";
        case Harmony::Suspended: return "Sus 4";
        case Harmony::Quartal: return "Quartal";
        case Harmony::Major: return "Major";
        case Harmony::Minor: return "Minor";
        case Harmony::Diminished: return "Diminished";
        case Harmony::Augmented: return "Augmented";
        case Harmony::Sus2: return "Sus 2";
        case Harmony::Sus4: return "Sus 4";
        case Harmony::Dominant7: return "Dominant 7";
        case Harmony::Major7: return "Major 7";
        case Harmony::Minor7: return "Minor 7";
        case Harmony::Custom: return "Custom";
        case Harmony::Count:
        default: return "Shape";
    }
}

FLASHMEM const char* sourceLabel(
    core::state::sequencer::SequencerChordSourceChoice choice
) {
    using Choice =
        core::state::sequencer::SequencerChordSourceChoice;
    switch (choice) {
        case Choice::PARENT_CHORD: return "Parent chord";
        case Choice::LOCAL_CHORD: return "Local chord";
        case Choice::SINGLE_NOTE:
        default: return "Single note";
    }
}

FLASHMEM const char* label(Field field) {
    switch (field) {
        case Field::SHAPE: return "Shape";
        case Field::FORMULA: return "Formula";
        case Field::INVERSION: return "Inversion";
        case Field::VOICING: return "Voicing";
        case Field::STRUM: return "Strum";
        case Field::VELOCITY_CONTOUR: return "Velocity";
        case Field::PITCH_CONTEXT: return "Context";
        case Field::COUNT:
        default: return "Chord";
    }
}

FLASHMEM const char* icon(Field field) {
    switch (field) {
        case Field::SHAPE:
            return ::standalone::icons::CHORD_PROP_HARMONY;
        case Field::FORMULA:
            return ::standalone::icons::SCALE;
        case Field::INVERSION:
            return ::standalone::icons::CHORD_PROP_INVERSION;
        case Field::VOICING:
            return ::standalone::icons::CHORD_PROP_VOICING;
        case Field::STRUM:
            return ::standalone::icons::SWING;
        case Field::VELOCITY_CONTOUR:
            return ::standalone::icons::NOTE_PROP_VEL;
        case Field::PITCH_CONTEXT:
            return ::standalone::icons::SCALE;
        case Field::COUNT:
        default:
            return ::standalone::icons::CHORD;
    }
}

FLASHMEM uint32_t color(Field field) {
    switch (field) {
        case Field::SHAPE:
            return core::ui::sequencer::semantic::color(Tone::CHORD_SHAPE);
        case Field::FORMULA:
            return core::ui::sequencer::semantic::color(
                Tone::CHORD_FORMULA
            );
        case Field::INVERSION:
            return core::ui::sequencer::semantic::color(
                Tone::CHORD_INVERSION
            );
        case Field::VOICING:
            return core::ui::sequencer::semantic::color(Tone::CHORD_VOICING);
        case Field::STRUM:
            return core::ui::sequencer::semantic::color(Tone::CHORD_STRUM);
        case Field::VELOCITY_CONTOUR:
            return core::ui::sequencer::semantic::color(
                Tone::CHORD_VELOCITY
            );
        case Field::PITCH_CONTEXT:
            return core::ui::sequencer::semantic::color(
                Tone::CHORD_FORMULA
            );
        case Field::COUNT:
        default:
            return core::ui::sequencer::semantic::color(Tone::CHORD);
    }
}

FLASHMEM core::ui::SequencerStepEditVisualSlot visualSlot(Field field) {
    using Slot = core::ui::SequencerStepEditVisualSlot;
    switch (field) {
        case Field::SHAPE: return Slot::CHORD_SHAPE;
        case Field::FORMULA: return Slot::CHORD_FORMULA;
        case Field::INVERSION: return Slot::CHORD_INVERSION;
        case Field::VOICING: return Slot::CHORD_VOICING;
        case Field::STRUM: return Slot::CHORD_STRUM;
        case Field::VELOCITY_CONTOUR: return Slot::CHORD_VELOCITY;
        case Field::PITCH_CONTEXT: return Slot::CHORD_CONTEXT;
        case Field::COUNT:
        default: return Slot::AUTO;
    }
}

FLASHMEM void formatValue(
    char* out,
    size_t outSize,
    Field field,
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    if (!out || outSize == 0) return;
    switch (field) {
        case Field::SHAPE:
            copyText(
                out,
                outSize,
                shapeLabel(
                    chord.preview.valid
                        ? chord.preview.harmony
                        : chord.spec.harmony()
                )
            );
            return;
        case Field::FORMULA:
            formatFormula(out, outSize, chord);
            return;
        case Field::INVERSION:
            formatInversion(
                out,
                outSize,
                chord.preview.valid
                    ? chord.preview.effectiveInversion
                    : chord.spec.inversion()
            );
            return;
        case Field::VOICING:
            copyText(
                out,
                outSize,
                voicingLabel(chord.spec.voicing())
            );
            return;
        case Field::STRUM:
            formatSigned(out, outSize, chord.spec.strum, "%");
            return;
        case Field::VELOCITY_CONTOUR:
            formatSigned(out, outSize, chord.spec.velocityCurve);
            return;
        case Field::PITCH_CONTEXT:
            formatContext(out, outSize, chord);
            return;
        case Field::COUNT:
        default:
            copyText(out, outSize, "--");
            return;
    }
}

FLASHMEM void formatFormula(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    if (!out || outSize == 0) return;
    const auto formula = chordFormula(chord);
    if (!formula.valid || formula.count == 0) {
        copyText(out, outSize, "--");
        return;
    }

    size_t pos = 0;
    const uint8_t limit = std::min<uint8_t>(formula.count, 4U);
    for (uint8_t voice = 0; voice < limit; ++voice) {
        if (voice > 0) {
            pos = oc::type::text::appendString(out, outSize, pos, "-");
        }
        appendFormulaInterval(
            out,
            outSize,
            pos,
            formula.intervals[voice],
            formula.intervalUsesScaleDegrees
        );
    }
    if (formula.count > limit) {
        pos = oc::type::text::appendString(out, outSize, pos, "+");
    }
    oc::type::text::terminate(out, outSize, pos);
}

FLASHMEM void formatContext(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    copyText(
        out,
        outSize,
        chord.pitchFollowsScale
            ? (chord.intervalsUseScaleDegrees ? "Follow DEG"
                                               : "Follow ST")
            : "Chromatic ST"
    );
}

FLASHMEM void formatSource(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    using Choice =
        core::state::sequencer::SequencerChordSourceChoice;
    Choice choice = Choice::SINGLE_NOTE;
    if (chord.mode ==
        oc::note::sequencer::StepSequencerChordMode::Local) {
        choice = Choice::LOCAL_CHORD;
    } else if (!chord.rootContext &&
               chord.mode ==
                   oc::note::sequencer::StepSequencerChordMode::Inherit) {
        choice = Choice::PARENT_CHORD;
    }
    copyText(out, outSize, sourceLabel(choice));
}

FLASHMEM bool formulaVoiceActive(
    uint8_t voiceIndex,
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    const auto formula = chordFormula(chord);
    return formula.valid && voiceIndex < formula.count;
}

FLASHMEM void formatFormulaVoice(
    char* out,
    size_t outSize,
    uint8_t voiceIndex,
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    if (!out || outSize == 0) return;
    if (voiceIndex >=
        oc::note::sequencer::StepSequencerChordSpec::MAX_CUSTOM_VOICES) {
        copyText(out, outSize, "--");
        return;
    }
    const auto formula = chordFormula(chord);
    if (voiceIndex > 0 && !formulaVoiceActive(voiceIndex, chord)) {
        copyText(out, outSize, "+");
        return;
    }

    const int16_t interval = voiceIndex == 0
        ? 0
        : formula.intervals[voiceIndex];
    const uint8_t root = chord.preview.rootNote;
    const uint8_t note = chord.intervalsUseScaleDegrees
        ? oc::note::sequencer::moveByScaleDegrees(
              root,
              static_cast<int8_t>(interval),
              chord.preview.scaleSettings
          )
        : clampMidi(static_cast<int>(root) + interval);
    char noteName[8] = {};
    core::state::sequencer::note_spelling::formatNoteName(
        noteName,
        sizeof(noteName),
        note,
        chord.preview.scaleSettings
    );

    if (chord.intervalsUseScaleDegrees) {
        std::snprintf(
            out,
            outSize,
            "%u  %s",
            static_cast<unsigned>(interval + 1),
            noteName
        );
    } else if (interval == 0) {
        std::snprintf(out, outSize, "0  %s", noteName);
    } else {
        std::snprintf(
            out,
            outSize,
            "+%u  %s",
            static_cast<unsigned>(interval),
            noteName
        );
    }
}

FLASHMEM void formatFormulaVoiceInterval(
    char* out,
    size_t outSize,
    uint8_t voiceIndex,
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    if (!out || outSize == 0) return;
    if (voiceIndex >=
        oc::note::sequencer::StepSequencerChordSpec::MAX_CUSTOM_VOICES) {
        copyText(out, outSize, "--");
        return;
    }
    const auto formula = chordFormula(chord);
    if (!formula.valid || voiceIndex >= formula.count) {
        copyText(out, outSize, "+");
        return;
    }

    const auto interval = formula.intervals[voiceIndex];
    if (chord.intervalsUseScaleDegrees) {
        std::snprintf(
            out,
            outSize,
            "%u",
            static_cast<unsigned>(std::max<int16_t>(0, interval) + 1)
        );
    } else if (interval == 0) {
        copyText(out, outSize, "0");
    } else {
        std::snprintf(
            out,
            outSize,
            "+%u",
            static_cast<unsigned>(interval)
        );
    }
}

}  // namespace core::context::standalone::sequencer_chord_field_presentation

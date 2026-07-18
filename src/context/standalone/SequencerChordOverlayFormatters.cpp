#include "context/standalone/SequencerChordOverlayFormatters.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include <oc/type/TextFormat.hpp>

#include "state/sequencer/SequencerStepEditRows.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {
namespace {

namespace step_edit_rows = core::state::sequencer::step_edit_rows;

constexpr size_t CHIP_PITCH_INDEX = 0;
constexpr size_t CHIP_VELOCITY_INDEX = 1;
constexpr size_t CHIP_GATE_INDEX = 2;
constexpr size_t CHIP_NUDGE_INDEX = 3;

FLASHMEM void copyText(char* out, size_t outSize, const char* text) {
    if (!out || outSize == 0) return;
    const char* source = text ? text : "";
    std::strncpy(out, source, outSize - 1);
    out[outSize - 1] = '\0';
}

FLASHMEM ms::ui::KeyValueRow makeIconRow(
    const char* key,
    const char* value,
    const char* icon,
    uint32_t color
) {
    return {
        .key = key,
        .value = value,
        .icon = icon,
        .iconFont = standalone_fonts.icons_14,
        .iconColor = color,
    };
}

FLASHMEM void formatNoteName(char* out, size_t outSize, uint8_t note) {
    core::state::sequencer::formatStepPropertyValue(
        out,
        outSize,
        core::state::sequencer::StepProperty::NOTE,
        note,
        0,
        0
    );
}

FLASHMEM const char* pitchClassLabel(uint8_t pitchClass) {
    constexpr const char* LABELS[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
    };
    return LABELS[pitchClass % 12U];
}

FLASHMEM const char* chordQualitySuffix(oc::note::sequencer::StepSequencerChordQuality quality) {
    using Quality = oc::note::sequencer::StepSequencerChordQuality;
    switch (quality) {
        case Quality::Power:
            return "5";
        case Quality::Major:
            return "";
        case Quality::Minor:
            return "m";
        case Quality::Diminished:
            return "dim";
        case Quality::Augmented:
            return "aug";
        case Quality::Sus2:
            return "sus2";
        case Quality::Sus4:
            return "sus4";
        case Quality::Dominant7:
            return "7";
        case Quality::Major7:
            return "maj7";
        case Quality::Minor7:
            return "m7";
        case Quality::MinorMajor7:
            return "mMaj7";
        case Quality::Major6:
            return "6";
        case Quality::Minor6:
            return "m6";
        case Quality::Diminished7:
            return "dim7";
        case Quality::HalfDiminished7:
            return "m7b5";
        case Quality::Dominant9:
            return "9";
        case Quality::Major9:
            return "maj9";
        case Quality::Minor9:
            return "m9";
        case Quality::Add9:
            return "add9";
        case Quality::MinorAdd9:
            return "madd9";
        case Quality::Unknown:
        default:
            return "";
    }
}

FLASHMEM size_t appendText(char* out, size_t outSize, size_t pos, const char* text) {
    return oc::type::text::appendString(out, outSize, pos, text ? text : "");
}

FLASHMEM size_t appendUnsignedText(char* out, size_t outSize, size_t pos, unsigned value) {
    return oc::type::text::appendUnsigned(out, outSize, pos, value);
}

FLASHMEM uint8_t nonRootIntervalCount(
    const oc::note::sequencer::StepSequencerChordAnalysis& analysis
) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < analysis.intervalCount; ++i) {
        if (analysis.chromaticIntervals[i] != 0) ++count;
    }
    return count;
}

FLASHMEM void formatChordPreviewName(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerChordPreview& preview
) {
    if (!out || outSize == 0) return;
    if (!preview.valid) {
        copyText(out, outSize, "--");
        return;
    }

    const auto& analysis = preview.analysis;
    size_t pos = 0;
    pos = appendText(out, outSize, pos, pitchClassLabel(analysis.rootPitchClass));
    if (analysis.recognized) {
        pos = appendText(out, outSize, pos, chordQualitySuffix(analysis.quality));
        if (analysis.slash) {
            pos = appendText(out, outSize, pos, "/");
            pos = appendText(out, outSize, pos, pitchClassLabel(analysis.bassPitchClass));
        }
    } else if (nonRootIntervalCount(analysis) > 3) {
        pos = appendText(out, outSize, pos, " chord");
    } else {
        for (uint8_t i = 0; i < analysis.intervalCount; ++i) {
            const uint8_t interval = analysis.chromaticIntervals[i];
            if (interval == 0) continue;
            pos = appendText(out, outSize, pos, " +");
            pos = appendUnsignedText(out, outSize, pos, interval);
        }
    }
    oc::type::text::terminate(out, outSize, pos);
}

FLASHMEM uint8_t clampPercent(int value) {
    return static_cast<uint8_t>(std::clamp(value, 0, 100));
}

FLASHMEM uint32_t mixColor(uint32_t from, uint32_t to, uint8_t amount) {
    const uint32_t inv = static_cast<uint32_t>(255U - amount);
    const uint32_t r =
        (((from >> 16U) & 0xffU) * inv + ((to >> 16U) & 0xffU) * amount) / 255U;
    const uint32_t g =
        (((from >> 8U) & 0xffU) * inv + ((to >> 8U) & 0xffU) * amount) / 255U;
    const uint32_t b =
        ((from & 0xffU) * inv + (to & 0xffU) * amount) / 255U;
    return (r << 16U) | (g << 8U) | b;
}

FLASHMEM void populateChordPreviewMarkers(
    core::ui::SequencerChordPreviewProps& props,
    const core::state::sequencer::SequencerChordPreview& preview
) {
    if (!preview.valid || preview.voiceCount == 0) return;

    uint8_t minNote = 127;
    uint8_t maxNote = 0;
    uint8_t minVelocity = 127;
    uint8_t maxVelocity = 0;
    uint16_t minDelay = UINT16_MAX;
    uint16_t maxDelay = 0;
    for (uint8_t i = 0; i < preview.voiceCount; ++i) {
        const auto& voice = preview.voices[i];
        minNote = std::min(minNote, voice.note);
        maxNote = std::max(maxNote, voice.note);
        minVelocity = std::min(minVelocity, voice.velocity);
        maxVelocity = std::max(maxVelocity, voice.velocity);
        minDelay = std::min(minDelay, voice.delayTicks);
        maxDelay = std::max(maxDelay, voice.delayTicks);
    }

    const int noteRange = std::max<int>(1, static_cast<int>(maxNote) - static_cast<int>(minNote));
    const int velocityRange =
        std::max<int>(1, static_cast<int>(maxVelocity) - static_cast<int>(minVelocity));
    const int spanTicks = std::max<int>(1, static_cast<int>(preview.spanTicks));
    const uint32_t velocityLow = ::standalone::theme::color::TEXT_SECONDARY;
    const uint32_t velocityHigh = core::ui::sequencer::semantic::color(
        core::ui::sequencer::semantic::Tone::CHORD_VELOCITY
    );
    props.mapVisible = true;
    props.timingVisible = preview.voiceCount > 1 && maxDelay > minDelay;
    props.timingStart = clampPercent((static_cast<int>(minDelay) * 100) / spanTicks);
    props.timingEnd = clampPercent((static_cast<int>(maxDelay) * 100) / spanTicks);
    props.timingColor = core::ui::sequencer::semantic::color(
        core::ui::sequencer::semantic::Tone::CHORD_STRUM
    );

    for (uint8_t i = 0; i < preview.voiceCount && i < props.voices.size(); ++i) {
        const auto& voice = preview.voices[i];
        const int x = (static_cast<int>(voice.delayTicks) * 100) / spanTicks;
        const int y =
            100 - ((static_cast<int>(voice.note) - static_cast<int>(minNote)) * 100) / noteRange;
        const uint8_t velocityMix = minVelocity == maxVelocity
            ? 200
            : static_cast<uint8_t>(
                  90 +
                  ((static_cast<int>(voice.velocity) - static_cast<int>(minVelocity)) * 165) /
                      velocityRange
              );
        const uint8_t markerSize = i == 0 ? 6 : 5;

        props.voices[i] = core::ui::SequencerChordPreviewVoiceMarker{
            .active = true,
            .x = clampPercent(x),
            .y = clampPercent(y),
            .size = markerSize,
            .width = markerSize,
            .height = markerSize,
            .opa = 245,
            .color = mixColor(velocityLow, velocityHigh, velocityMix),
        };
    }
}

FLASHMEM const char* chordModeLabel(oc::note::sequencer::StepSequencerChordMode mode) {
    using oc::note::sequencer::StepSequencerChordMode;
    switch (mode) {
        case StepSequencerChordMode::Inherit:
            return "Inherit";
        case StepSequencerChordMode::Local:
            return "Local";
        case StepSequencerChordMode::Single:
        default:
            return "Single";
    }
}

FLASHMEM const char* chordHarmonyLabel(
    oc::note::sequencer::StepSequencerChordHarmony harmony
) {
    using Harmony = oc::note::sequencer::StepSequencerChordHarmony;
    switch (harmony) {
        case Harmony::DiatonicTriad: return "Diatonic triad";
        case Harmony::DiatonicSeventh: return "Diatonic 7th";
        case Harmony::Suspended: return "Suspended";
        case Harmony::Quartal: return "Quartal";
        case Harmony::Fifths: return "Fifths";
        case Harmony::Cluster: return "Cluster";
        case Harmony::Major: return "Major";
        case Harmony::Minor: return "Minor";
        case Harmony::Diminished: return "Diminished";
        case Harmony::Augmented: return "Augmented";
        case Harmony::Sus2: return "Sus 2";
        case Harmony::Sus4: return "Sus 4";
        case Harmony::Dominant7: return "Dominant 7";
        case Harmony::Major7: return "Major 7";
        case Harmony::Minor7: return "Minor 7";
        case Harmony::Count:
        default: return "Harmony";
    }
}

FLASHMEM const char* chordVoicingLabel(
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

FLASHMEM void formatInversion(char* out, size_t outSize, uint8_t inversion) {
    if (inversion == 0) {
        copyText(out, outSize, "Root");
        return;
    }
    const char* suffix = "th";
    if (inversion == 1) suffix = "st";
    else if (inversion == 2) suffix = "nd";
    else if (inversion == 3) suffix = "rd";
    std::snprintf(out, outSize, "%u%s", static_cast<unsigned>(inversion), suffix);
}

FLASHMEM const char* chordFieldLabel(core::state::sequencer::SequencerChordEditField field) {
    using Field = core::state::sequencer::SequencerChordEditField;
    switch (field) {
        case Field::MODE:
            return "Mode";
        case Field::HARMONY:
            return "Harmony";
        case Field::VOICES:
            return "Voices";
        case Field::INVERSION:
            return "Inversion";
        case Field::VOICING:
            return "Voicing";
        case Field::STRUM:
            return "Strum";
        case Field::VELOCITY_CONTOUR:
            return "Velocity contour";
        case Field::COUNT:
        default:
            return "Chord";
    }
}

FLASHMEM const char* chordFieldIcon(core::state::sequencer::SequencerChordEditField field) {
    using Field = core::state::sequencer::SequencerChordEditField;
    switch (field) {
        case Field::MODE:
            return ::standalone::icons::CHORD_PROP_MODE;
        case Field::HARMONY:
            return ::standalone::icons::CHORD_PROP_HARMONY;
        case Field::VOICES:
            return ::standalone::icons::CHORD_PROP_VOICE;
        case Field::INVERSION:
            return ::standalone::icons::CHORD_PROP_INVERSION;
        case Field::VOICING:
            return ::standalone::icons::CHORD_PROP_VOICING;
        case Field::STRUM:
            return ::standalone::icons::SWING;
        case Field::VELOCITY_CONTOUR:
            return ::standalone::icons::NOTE_PROP_VEL;
        case Field::COUNT:
        default:
            return ::standalone::icons::CHORD;
    }
}

FLASHMEM uint32_t chordFieldColor(core::state::sequencer::SequencerChordEditField field) {
    using Field = core::state::sequencer::SequencerChordEditField;
    using Tone = core::ui::sequencer::semantic::Tone;
    switch (field) {
        case Field::MODE:
            return core::ui::sequencer::semantic::color(Tone::CHORD_MODE);
        case Field::HARMONY:
            return core::ui::sequencer::semantic::color(Tone::CHORD_HARMONY);
        case Field::VOICES:
            return core::ui::sequencer::semantic::color(Tone::CHORD_VOICE);
        case Field::INVERSION:
            return core::ui::sequencer::semantic::color(Tone::CHORD_INVERSION);
        case Field::VOICING:
            return core::ui::sequencer::semantic::color(Tone::CHORD_VOICING);
        case Field::STRUM:
            return core::ui::sequencer::semantic::color(Tone::CHORD_STRUM);
        case Field::VELOCITY_CONTOUR:
            return core::ui::sequencer::semantic::color(Tone::CHORD_VELOCITY);
        case Field::COUNT:
        default:
            return chordColor();
    }
}

FLASHMEM core::ui::SequencerStepEditVisualSlot chordFieldVisualSlot(
    core::state::sequencer::SequencerChordEditField field
) {
    using Field = core::state::sequencer::SequencerChordEditField;
    using Slot = core::ui::SequencerStepEditVisualSlot;
    switch (field) {
        case Field::MODE:
            return Slot::CHORD_MODE;
        case Field::HARMONY:
            return Slot::CHORD_HARMONY;
        case Field::VOICES:
            return Slot::CHORD_VOICES;
        case Field::INVERSION:
            return Slot::CHORD_INVERSION;
        case Field::VOICING:
            return Slot::CHORD_VOICING;
        case Field::STRUM:
            return Slot::CHORD_STRUM;
        case Field::VELOCITY_CONTOUR:
            return Slot::CHORD_VELOCITY;
        case Field::COUNT:
        default:
            return Slot::AUTO;
    }
}

FLASHMEM void formatSigned(
    char* out,
    size_t outSize,
    int value,
    const char* suffix = ""
) {
    if (!out || outSize == 0) return;
    std::snprintf(out, outSize, "%+d%s", value, suffix ? suffix : "");
}

FLASHMEM void formatChordFieldValue(
    char* out,
    size_t outSize,
    core::state::sequencer::SequencerChordEditField field,
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    using Field = core::state::sequencer::SequencerChordEditField;
    if (!out || outSize == 0) return;

    switch (field) {
        case Field::MODE:
            copyText(out, outSize, chordModeLabel(chord.mode));
            return;
        case Field::HARMONY:
            if (!chord.spec.isSemantic()) {
                copyText(out, outSize, "Legacy recipe");
            } else {
                copyText(
                    out,
                    outSize,
                    chordHarmonyLabel(
                        chord.preview.valid ? chord.preview.harmony : chord.spec.harmony()
                    )
                );
            }
            return;
        case Field::VOICES:
            std::snprintf(
                out,
                outSize,
                "%u",
                static_cast<unsigned>(
                    chord.mode == oc::note::sequencer::StepSequencerChordMode::Local
                        ? chord.spec.voiceCount
                        : chord.effectiveVoiceCount
                )
            );
            return;
        case Field::INVERSION:
            if (!chord.spec.isSemantic()) {
                copyText(out, outSize, "Legacy recipe");
            } else {
                formatInversion(
                    out,
                    outSize,
                    chord.preview.valid
                        ? chord.preview.effectiveInversion
                        : chord.spec.inversion()
                );
            }
            return;
        case Field::VOICING:
            copyText(
                out,
                outSize,
                chord.spec.isSemantic()
                    ? chordVoicingLabel(chord.spec.voicing())
                    : "Legacy recipe"
            );
            return;
        case Field::STRUM:
            formatSigned(out, outSize, chord.spec.strum, "%");
            return;
        case Field::VELOCITY_CONTOUR:
            formatSigned(out, outSize, chord.spec.velocityCurve);
            return;
        case Field::COUNT:
        default:
            copyText(out, outSize, "--");
            return;
    }
}

FLASHMEM void formatChordFieldTitle(
    char* out,
    size_t outSize,
    core::state::sequencer::SequencerChordEditField field
) {
    if (!out || outSize == 0) return;
    copyText(out, outSize, chordFieldLabel(field));
}

FLASHMEM char* chordFieldBuffer(
    StepEditRenderData& data,
    core::state::sequencer::SequencerChordEditField field
) {
    const auto index = static_cast<size_t>(field);
    if (index < data.chordValueBuffers.size()) {
        return data.chordValueBuffers[index].data();
    }
    return data.chordValueBuffers[0].data();
}

FLASHMEM void setChordPropertyChip(
    core::ui::SequencerStepEditPropertyChip& chip,
    StepEditRenderData& data,
    core::state::sequencer::SequencerChordEditField field
) {
    chip = core::ui::SequencerStepEditPropertyChip{
        .key = chordFieldLabel(field),
        .value = chordFieldBuffer(data, field),
        .icon = chordFieldIcon(field),
        .color = chordFieldColor(field),
    };
}

}  // namespace

FLASHMEM uint32_t chordColor() {
    return core::ui::sequencer::semantic::color(core::ui::sequencer::semantic::Tone::CHORD);
}

FLASHMEM void formatChordValue(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerStepChordUiState& chord
) {
    if (!out || outSize == 0) return;

    using oc::note::sequencer::StepSequencerChordMode;
    switch (chord.mode) {
        case StepSequencerChordMode::Inherit:
            copyText(out, outSize, "Inherit");
            return;
        case StepSequencerChordMode::Local:
            if (chord.preview.valid) {
                char name[16] = {};
                formatChordPreviewName(name, sizeof(name), chord.preview);
                std::snprintf(
                    out,
                    outSize,
                    "%s %uv",
                    name,
                    static_cast<unsigned>(chord.preview.voiceCount)
                );
            } else {
                std::snprintf(
                    out,
                    outSize,
                    "%u voices",
                    static_cast<unsigned>(std::max<uint8_t>(chord.spec.voiceCount, 1))
                );
            }
            return;
        case StepSequencerChordMode::Single:
        default:
            copyText(out, outSize, "Single");
            return;
    }
}

FLASHMEM void formatChordPreviewNotes(
    char* out,
    size_t outSize,
    const core::state::sequencer::SequencerChordPreview& preview
) {
    if (!out || outSize == 0) return;
    if (!preview.valid || preview.voiceCount == 0) {
        copyText(out, outSize, "");
        return;
    }

    size_t pos = 0;
    uint8_t written = 0;
    constexpr uint8_t MAX_VISIBLE_NOTES = 5;
    const bool summarize = preview.voiceCount > MAX_VISIBLE_NOTES;
    const uint8_t noteLimit =
        summarize ? static_cast<uint8_t>(MAX_VISIBLE_NOTES - 1U) : preview.voiceCount;

    for (uint8_t i = 0; i < noteLimit; ++i) {
        char note[8] = {};
        formatNoteName(note, sizeof(note), preview.voices[i].note);
        const size_t noteLen = std::strlen(note);
        const size_t gap = pos > 0 ? 1U : 0U;
        const uint8_t remainingIfSkipped = static_cast<uint8_t>(preview.voiceCount - written);
        const uint8_t remainingAfterNote =
            static_cast<uint8_t>(preview.voiceCount - (written + 1U));
        char suffix[8] = {};
        std::snprintf(suffix, sizeof(suffix), "+%u", static_cast<unsigned>(remainingAfterNote));
        const size_t suffixGap = pos > 0 ? 1U : 0U;
        const size_t suffixLen =
            remainingAfterNote > 0 ? (std::strlen(suffix) + suffixGap) : 0U;

        if (pos + gap + noteLen + suffixLen >= outSize) {
            if (remainingIfSkipped > 0) {
                std::snprintf(
                    suffix,
                    sizeof(suffix),
                    "+%u",
                    static_cast<unsigned>(remainingIfSkipped)
                );
                if (pos > 0) pos = appendText(out, outSize, pos, " ");
                pos = appendText(out, outSize, pos, suffix);
            }
            oc::type::text::terminate(out, outSize, pos);
            return;
        }

        if (gap > 0) pos = appendText(out, outSize, pos, " ");
        pos = appendText(out, outSize, pos, note);
        ++written;
    }
    if (summarize) {
        char suffix[8] = {};
        std::snprintf(
            suffix,
            sizeof(suffix),
            "+%u",
            static_cast<unsigned>(preview.voiceCount - noteLimit)
        );
        if (pos > 0) pos = appendText(out, outSize, pos, " ");
        pos = appendText(out, outSize, pos, suffix);
    }
    oc::type::text::terminate(out, outSize, pos);
}

FLASHMEM void populateChordDetailOverlay(
    StepEditRenderData& data,
    const core::state::sequencer::SequencerStepChordUiState& chord,
    core::state::sequencer::SequencerChordEditField focusedField,
    bool enabled
) {
    using Field = core::state::sequencer::SequencerChordEditField;
    data.meta[0] = '\0';

    constexpr Field fields[] = {
        Field::MODE,
        Field::HARMONY,
        Field::VOICES,
        Field::INVERSION,
        Field::VOICING,
        Field::STRUM,
        Field::VELOCITY_CONTOUR,
    };
    for (auto field : fields) {
        formatChordFieldValue(
            chordFieldBuffer(data, field),
            data.chordValueBuffers[0].size(),
            field,
            chord
        );
    }

    if (chord.preview.valid) {
        formatChordPreviewName(data.chordName.data(), data.chordName.size(), chord.preview);
        formatChordPreviewNotes(data.chordDetail.data(), data.chordDetail.size(), chord.preview);
    } else {
        copyText(data.chordName.data(), data.chordName.size(), chordModeLabel(chord.mode));
        copyText(data.chordDetail.data(), data.chordDetail.size(), "");
    }

    if (!chord.spec.isSemantic() &&
        chord.mode != oc::note::sequencer::StepSequencerChordMode::Single) {
        copyText(data.meta.data(), data.meta.size(), "Legacy recipe");
    } else if (chord.preview.droppedVoiceCount > 0) {
        const auto dropped = static_cast<unsigned>(chord.preview.droppedVoiceCount);
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "%u voice%s lost",
            dropped,
            dropped == 1U ? "" : "s"
        );
    } else if (chord.preview.harmonyAdjustedForPitchMode) {
        copyText(data.meta.data(), data.meta.size(), "Pitch adapted");
    } else if (chord.preview.inversionClamped) {
        copyText(data.meta.data(), data.meta.size(), "Inv clamped");
    }

    const char* focusedValue = chordFieldBuffer(data, focusedField);
    data.rows[step_edit_rows::CHORD] = makeIconRow(
        chordFieldLabel(focusedField),
        focusedValue,
        chordFieldIcon(focusedField),
        chordFieldColor(focusedField)
    );
    copyText(data.focusLabel.data(), data.focusLabel.size(), chordFieldLabel(focusedField));
    formatChordFieldTitle(
        data.chordFieldTitle.data(),
        data.chordFieldTitle.size(),
        focusedField
    );

    data.overlayProps = {};
    data.overlayProps.visible = true;
    data.overlayProps.stepBadge = data.stepBadge.data();
    data.overlayProps.title = data.chordFieldTitle.data();
    data.overlayProps.meta = data.meta.data();
    data.overlayProps.focusLabel = "";
    data.overlayProps.titleCentered = true;
    data.overlayProps.focusLabelVisible = false;
    data.overlayProps.chordDetailLayout = true;
    data.overlayProps.enabled = enabled;
    data.overlayProps.selectedIndex = data.selectedIndex;
    data.overlayProps.actionsVisible = false;
    data.overlayProps.selectedVisualSlot = chordFieldVisualSlot(focusedField);
    data.overlayProps.focusColor = chordFieldColor(focusedField);
    data.overlayProps.titleColor = chordFieldColor(focusedField);
    data.overlayProps.chordPreview = core::ui::SequencerChordPreviewProps{
        .visible = true,
        .name = data.chordName.data(),
        .detail = data.chordDetail.data(),
        .color = chordColor(),
    };
    populateChordPreviewMarkers(data.overlayProps.chordPreview, chord.preview);

    setChordPropertyChip(data.overlayProps.properties[CHIP_PITCH_INDEX], data, Field::MODE);
    setChordPropertyChip(data.overlayProps.properties[CHIP_VELOCITY_INDEX], data, Field::HARMONY);
    setChordPropertyChip(data.overlayProps.properties[CHIP_GATE_INDEX], data, Field::VOICES);
    setChordPropertyChip(data.overlayProps.properties[CHIP_NUDGE_INDEX], data, Field::INVERSION);
    setChordPropertyChip(data.overlayProps.chordPerformance[0], data, Field::VOICING);
    setChordPropertyChip(data.overlayProps.chordPerformance[1], data, Field::STRUM);
    setChordPropertyChip(data.overlayProps.chordPerformance[2], data, Field::VELOCITY_CONTOUR);
}

}  // namespace core::context::standalone::sequencer_overlay_presenter

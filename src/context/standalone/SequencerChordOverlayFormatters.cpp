#include "context/standalone/SequencerChordOverlayFormatters.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include <oc/type/TextFormat.hpp>

#include "context/standalone/SequencerChordFieldPresentation.hpp"
#include "state/sequencer/SequencerNoteSpelling.hpp"
#include "state/sequencer/SequencerScaleCatalog.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {
namespace {

namespace step_edit_rows = core::state::sequencer::step_edit_rows;
namespace note_spelling = core::state::sequencer::note_spelling;
namespace chord_fields =
    core::context::standalone::sequencer_chord_field_presentation;

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

FLASHMEM StepEditKeyValueRow makeIconRow(
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
    pos = appendText(
        out,
        outSize,
        pos,
        note_spelling::pitchClassLabel(
            analysis.rootPitchClass,
            preview.scaleSettings
        )
    );
    if (analysis.recognized) {
        pos = appendText(out, outSize, pos, chordQualitySuffix(analysis.quality));
        if (analysis.slash) {
            pos = appendText(out, outSize, pos, "/");
            pos = appendText(
                out,
                outSize,
                pos,
                note_spelling::pitchClassLabel(
                    analysis.bassPitchClass,
                    preview.scaleSettings
                )
            );
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
            .opa = static_cast<uint8_t>(
                voice.inSelectedScale ? 245U : 140U
            ),
            .color = mixColor(velocityLow, velocityHigh, velocityMix),
        };
    }
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
        .key = chord_fields::label(field),
        .value = chordFieldBuffer(data, field),
        .icon = chord_fields::icon(field),
        .color = chord_fields::color(field),
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
                    static_cast<unsigned>(std::max<uint8_t>(chord.spec.voices(), 1))
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
        note_spelling::formatNoteName(
            note,
            sizeof(note),
            preview.voices[i].note,
            preview.scaleSettings
        );
        if (!preview.voices[i].inSelectedScale) {
            const size_t length = std::strlen(note);
            if (length + 1U < sizeof(note)) {
                note[length] = '!';
                note[length + 1U] = '\0';
            }
        }
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
    bool formulaEditorActive,
    uint8_t focusedFormulaItem,
    bool sourceSelectorActive,
    core::state::sequencer::SequencerChordSourceChoice focusedSourceChoice,
    bool enabled
) {
    using Field = core::state::sequencer::SequencerChordEditField;
    using SourceChoice =
        core::state::sequencer::SequencerChordSourceChoice;
    using Slot = core::ui::SequencerStepEditVisualSlot;
    data.meta[0] = '\0';

    constexpr Field fields[] = {
        Field::SHAPE,
        Field::FORMULA,
        Field::INVERSION,
        Field::VOICING,
        Field::STRUM,
        Field::VELOCITY_CONTOUR,
        Field::PITCH_CONTEXT,
    };
    for (auto field : fields) {
        chord_fields::formatValue(
            chordFieldBuffer(data, field),
            data.chordValueBuffers[0].size(),
            field,
            chord
        );
    }
    chord_fields::formatFormula(
        data.chordFormula.data(),
        data.chordFormula.size(),
        chord
    );
    chord_fields::formatContext(
        data.chordContext.data(),
        data.chordContext.size(),
        chord
    );
    for (uint8_t voice = 0;
         voice < data.chordFormulaValueBuffers.size();
         ++voice) {
        chord_fields::formatFormulaVoice(
            data.chordFormulaValueBuffers[voice].data(),
            data.chordFormulaValueBuffers[voice].size(),
            voice,
            chord
        );
        chord_fields::formatFormulaVoiceInterval(
            data.chordFormulaIntervalBuffers[voice].data(),
            data.chordFormulaIntervalBuffers[voice].size(),
            voice,
            chord
        );
        if (voice == 0U) {
            copyText(
                data.chordFormulaLabelBuffers[voice].data(),
                data.chordFormulaLabelBuffers[voice].size(),
                "R"
            );
        } else {
            std::snprintf(
                data.chordFormulaLabelBuffers[voice].data(),
                data.chordFormulaLabelBuffers[voice].size(),
                "V%u",
                static_cast<unsigned>(voice + 1U)
            );
        }
    }
    constexpr SourceChoice sourceChoices[] = {
        SourceChoice::PARENT_CHORD,
        SourceChoice::SINGLE_NOTE,
        SourceChoice::LOCAL_CHORD,
    };
    for (uint8_t index = 0;
         index < data.chordSourceValueBuffers.size();
         ++index) {
        copyText(
            data.chordSourceValueBuffers[index].data(),
            data.chordSourceValueBuffers[index].size(),
            chord_fields::sourceLabel(sourceChoices[index])
        );
    }

    if (chord.preview.valid) {
        formatChordPreviewName(data.chordName.data(), data.chordName.size(), chord.preview);
        formatChordPreviewNotes(data.chordDetail.data(), data.chordDetail.size(), chord.preview);
    } else {
        copyText(
            data.chordName.data(),
            data.chordName.size(),
            chord_fields::modeLabel(chord.mode)
        );
        copyText(data.chordDetail.data(), data.chordDetail.size(), "");
    }

    const bool outsideScale = std::any_of(
        chord.preview.voices.begin(),
        chord.preview.voices.begin() + chord.preview.voiceCount,
        [](const auto& voice) { return !voice.inSelectedScale; }
    );
    if (chord.preview.droppedVoiceCount > 0) {
        const auto dropped = static_cast<unsigned>(chord.preview.droppedVoiceCount);
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "%u voice%s lost",
            dropped,
            dropped == 1U ? "" : "s"
        );
    } else if (chord.preview.harmonyAdjustedForPitchMode ||
               chord.preview.intervalBasisAdjusted) {
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "Shape adapted | %s",
            chord.intervalsUseScaleDegrees ? "DEG" : "ST"
        );
    } else if (outsideScale) {
        copyText(data.meta.data(), data.meta.size(), "Outside scale | ST");
    } else if (chord.preview.inversionClamped) {
        copyText(data.meta.data(), data.meta.size(), "Inv clamped");
    } else if (chord.preview.valid) {
        const char* root = note_spelling::pitchClassLabel(
            chord.preview.scaleSettings.root,
            chord.preview.scaleSettings
        );
        const char* scale =
            core::state::sequencer::scale_catalog::scaleTypeLabel(
                chord.preview.scaleSettings.type
            );
        if (chord.intervalsUseScaleDegrees) {
            std::snprintf(
                data.meta.data(),
                data.meta.size(),
                "%s %s | DEG",
                root,
                scale
            );
        } else {
            copyText(data.meta.data(), data.meta.size(), "Chromatic | ST");
        }
    }

    const char* sourceStatus = "Single";
    if (chord.mode ==
        oc::note::sequencer::StepSequencerChordMode::Local) {
        sourceStatus = "Local";
    } else if (!chord.rootContext &&
               chord.mode ==
                   oc::note::sequencer::StepSequencerChordMode::Inherit) {
        sourceStatus = "Parent";
    }
    const size_t metaLength = std::strlen(data.meta.data());
    if (metaLength > 0U) {
        std::snprintf(
            data.meta.data() + metaLength,
            data.meta.size() - metaLength,
            " | %s",
            sourceStatus
        );
    } else {
        copyText(data.meta.data(), data.meta.size(), sourceStatus);
    }

    uint32_t selectedColor = chord_fields::color(focusedField);
    Slot selectedSlot = chord_fields::visualSlot(focusedField);
    if (sourceSelectorActive) {
        selectedColor = core::ui::sequencer::semantic::color(
            core::ui::sequencer::semantic::Tone::CHORD_MODE
        );
        switch (focusedSourceChoice) {
            case SourceChoice::PARENT_CHORD:
                selectedSlot = Slot::CHORD_SOURCE_PARENT;
                break;
            case SourceChoice::LOCAL_CHORD:
                selectedSlot = Slot::CHORD_SOURCE_LOCAL;
                break;
            case SourceChoice::SINGLE_NOTE:
            default:
                selectedSlot = Slot::CHORD_SOURCE_SINGLE;
                break;
        }
        copyText(
            data.chordFieldTitle.data(),
            data.chordFieldTitle.size(),
            "Source"
        );
        data.rows[step_edit_rows::CHORD] = makeIconRow(
            "Source",
            chord_fields::sourceLabel(focusedSourceChoice),
            ::standalone::icons::CHORD_PROP_MODE,
            selectedColor
        );
        copyText(data.focusLabel.data(), data.focusLabel.size(), "Source");
    } else if (formulaEditorActive) {
        const auto formula = oc::note::sequencer::resolveChordFormula(
            chord.spec,
            chord.intervalsUseScaleDegrees
        );
        const uint8_t voiceCount = formula.valid
            ? std::clamp<uint8_t>(
                  formula.count,
                  2U,
                  oc::note::sequencer::StepSequencerChordSpec::
                      MAX_CUSTOM_VOICES
              )
            : 2U;
        const bool addVisible =
            voiceCount <
            oc::note::sequencer::StepSequencerChordSpec::MAX_CUSTOM_VOICES;
        const uint8_t lastFocusable = addVisible
            ? voiceCount
            : static_cast<uint8_t>(voiceCount - 1U);
        focusedFormulaItem = std::clamp<uint8_t>(
            focusedFormulaItem,
            1U,
            lastFocusable
        );
        const bool addFocused =
            addVisible && focusedFormulaItem == voiceCount;
        const bool addEnabled =
            addVisible &&
            formula.valid &&
            formula.intervals[voiceCount - 1U] <
                oc::note::sequencer::StepSequencerChordSpec::
                    MAX_CUSTOM_INTERVAL;
        const char* focusedValue = addFocused
            ? (addEnabled ? "Add voice" : "Lower last first")
            : data.chordFormulaValueBuffers[focusedFormulaItem].data();
        selectedColor = core::ui::sequencer::semantic::color(
            core::ui::sequencer::semantic::Tone::CHORD_FORMULA
        );
        selectedSlot = Slot::CHORD_FORMULA_RAIL;
        if (addFocused) {
            copyText(
                data.chordFieldTitle.data(),
                data.chordFieldTitle.size(),
                focusedValue
            );
        } else {
            std::snprintf(
                data.chordFieldTitle.data(),
                data.chordFieldTitle.size(),
                "V%u | %s",
                static_cast<unsigned>(focusedFormulaItem + 1U),
                focusedValue
            );
        }
        data.rows[step_edit_rows::CHORD] = makeIconRow(
            "Formula",
            focusedValue,
            ::standalone::icons::SCALE,
            selectedColor
        );
        copyText(
            data.focusLabel.data(),
            data.focusLabel.size(),
            "Formula"
        );
    } else {
        const char* focusedValue = chordFieldBuffer(data, focusedField);
        data.rows[step_edit_rows::CHORD] = makeIconRow(
            chord_fields::label(focusedField),
            focusedValue,
            chord_fields::icon(focusedField),
            selectedColor
        );
        copyText(
            data.focusLabel.data(),
            data.focusLabel.size(),
            chord_fields::label(focusedField)
        );
        copyText(
            data.chordFieldTitle.data(),
            data.chordFieldTitle.size(),
            "Chord"
        );
    }

    data.overlayProps = {};
    data.overlayProps.visible = true;
    data.overlayProps.stepBadge = data.stepBadge.data();
    data.overlayProps.title = data.chordFieldTitle.data();
    data.overlayProps.meta = data.meta.data();
    data.overlayProps.focusLabel = data.focusLabel.data();
    data.overlayProps.titleCentered = true;
    data.overlayProps.focusLabelVisible =
        !formulaEditorActive && !sourceSelectorActive;
    data.overlayProps.chordDetailLayout = true;
    data.overlayProps.chordFormulaLayout = formulaEditorActive;
    data.overlayProps.chordSourceLayout = sourceSelectorActive;
    data.overlayProps.enabled = enabled;
    data.overlayProps.selectedIndex = data.selectedIndex;
    data.overlayProps.actionsVisible = false;
    data.overlayProps.selectedVisualSlot = selectedSlot;
    data.overlayProps.titleColor = selectedColor;
    data.overlayProps.chordPreview = core::ui::SequencerChordPreviewProps{
        .visible = true,
        .name = data.chordName.data(),
        .detail = data.chordDetail.data(),
        .color = chordColor(),
    };
    populateChordPreviewMarkers(data.overlayProps.chordPreview, chord.preview);

    if (sourceSelectorActive) {
        data.overlayProps.properties[0] =
            core::ui::SequencerStepEditPropertyChip{
                .key = chord.rootContext ? "" : "Parent",
                .value = chord.rootContext
                    ? ""
                    : data.chordSourceValueBuffers[0].data(),
                .icon = chord.rootContext
                    ? ""
                    : ::standalone::icons::CHORD,
                .color = selectedColor,
                .active = !chord.rootContext,
            };
        data.overlayProps.properties[1] =
            core::ui::SequencerStepEditPropertyChip{
                .key = "Single",
                .value = data.chordSourceValueBuffers[1].data(),
                .icon = ::standalone::icons::NOTE,
                .color = selectedColor,
            };
        data.overlayProps.properties[2] =
            core::ui::SequencerStepEditPropertyChip{
                .key = "Local",
                .value = data.chordSourceValueBuffers[2].data(),
                .icon = ::standalone::icons::CHORD,
                .color = selectedColor,
            };
        data.overlayProps.properties[3] = {};
        data.overlayProps.chordPerformance[0] =
            core::ui::SequencerStepEditPropertyChip{
                .key = "Choose",
                .value = "Turn NAV",
                .icon = ::standalone::icons::KNOB,
                .color = selectedColor,
            };
        data.overlayProps.chordPerformance[1] =
            core::ui::SequencerStepEditPropertyChip{
                .key = "Apply",
                .value = "Press NAV",
                .icon = ::standalone::icons::ACTION_VALIDATE,
                .color = selectedColor,
            };
        data.overlayProps.chordPerformance[2] =
            core::ui::SequencerStepEditPropertyChip{
                .key = "Cancel",
                .value = "Left top",
                .icon = ::standalone::icons::ACTION_CANCEL,
                .color = selectedColor,
            };
        data.overlayProps.chordPerformance[3] = {};
        return;
    }

    if (formulaEditorActive) {
        const auto formula = oc::note::sequencer::resolveChordFormula(
            chord.spec,
            chord.intervalsUseScaleDegrees
        );
        const uint8_t voiceCount = formula.valid
            ? std::clamp<uint8_t>(
                  formula.count,
                  2U,
                  oc::note::sequencer::StepSequencerChordSpec::
                      MAX_CUSTOM_VOICES
              )
            : 2U;
        const bool addVisible =
            voiceCount <
            oc::note::sequencer::StepSequencerChordSpec::MAX_CUSTOM_VOICES;
        const bool addEnabled =
            addVisible &&
            formula.valid &&
            formula.intervals[voiceCount - 1U] <
                oc::note::sequencer::StepSequencerChordSpec::
                    MAX_CUSTOM_INTERVAL;
        const uint8_t itemCount = static_cast<uint8_t>(
            voiceCount + (addVisible ? 1U : 0U)
        );
        for (auto& property : data.overlayProps.properties) {
            property = {};
        }
        data.overlayProps.chordVoiceRail.visible = true;
        data.overlayProps.chordVoiceRail.itemCount = itemCount;
        data.overlayProps.chordVoiceRail.focusedItem =
            std::min<uint8_t>(
                focusedFormulaItem,
                static_cast<uint8_t>(itemCount - 1U)
            );
        data.overlayProps.chordVoiceRail.color = selectedColor;
        for (uint8_t voice = 0U; voice < voiceCount; ++voice) {
            data.overlayProps.chordVoiceRail.items[voice] =
                core::ui::SequencerChordVoiceRailItem{
                    .label =
                        data.chordFormulaLabelBuffers[voice].data(),
                    .value =
                        data.chordFormulaIntervalBuffers[voice].data(),
                    .add = false,
                    .enabled = true,
                };
        }
        if (addVisible) {
            data.overlayProps.chordVoiceRail.items[voiceCount] =
                core::ui::SequencerChordVoiceRailItem{
                    .label = "",
                    .value = "+",
                    .add = true,
                    .enabled = addEnabled,
                };
        }
        data.overlayProps.chordPerformance[0] =
            core::ui::SequencerStepEditPropertyChip{
                .key = "Context",
                .value = data.chordContext.data(),
                .icon = ::standalone::icons::SCALE,
                .color = selectedColor,
            };
        data.overlayProps.chordPerformance[1] =
            core::ui::SequencerStepEditPropertyChip{
                .key =
                    focusedFormulaItem == voiceCount && addVisible
                        ? "Add"
                        : "Edit",
                .value =
                    focusedFormulaItem == voiceCount && addVisible
                        ? (addEnabled ? "Press NAV" : "Lower last")
                        : "Turn OPT",
                .icon = ::standalone::icons::KNOB,
                .color = selectedColor,
            };
        data.overlayProps.chordPerformance[2] =
            focusedFormulaItem == voiceCount && addVisible
                ? core::ui::SequencerStepEditPropertyChip{}
                : core::ui::SequencerStepEditPropertyChip{
                      .key = "Remove",
                      .value = "Trash",
                      .icon = ::standalone::icons::ACTION_REMOVE,
                      .color = selectedColor,
                  };
        data.overlayProps.chordPerformance[3] =
            core::ui::SequencerStepEditPropertyChip{
                .key = "Done",
                .value = "Press NAV",
                .icon = ::standalone::icons::ACTION_VALIDATE,
                .color = selectedColor,
            };
        return;
    }

    setChordPropertyChip(
        data.overlayProps.properties[CHIP_PITCH_INDEX],
        data,
        Field::SHAPE
    );
    setChordPropertyChip(
        data.overlayProps.properties[CHIP_VELOCITY_INDEX],
        data,
        Field::FORMULA
    );
    setChordPropertyChip(
        data.overlayProps.properties[CHIP_GATE_INDEX],
        data,
        Field::INVERSION
    );
    setChordPropertyChip(
        data.overlayProps.properties[CHIP_NUDGE_INDEX],
        data,
        Field::VOICING
    );
    setChordPropertyChip(
        data.overlayProps.chordPerformance[0],
        data,
        Field::STRUM
    );
    setChordPropertyChip(
        data.overlayProps.chordPerformance[1],
        data,
        Field::VELOCITY_CONTOUR
    );
    setChordPropertyChip(
        data.overlayProps.chordPerformance[2],
        data,
        Field::PITCH_CONTEXT
    );
    data.overlayProps.chordPerformance[3] = {};
}

}  // namespace core::context::standalone::sequencer_overlay_presenter

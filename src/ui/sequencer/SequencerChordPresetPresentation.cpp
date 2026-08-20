#include "ui/sequencer/SequencerChordPresetPresentation.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerNoteSpelling.hpp"
#include "state/sequencer/SequencerScaleCatalog.hpp"
#include "ui/sequencer/SequencerPresetLibraryPresentationCommon.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer {
namespace {

namespace seq = core::state::sequencer;
using Picker = seq::SequencerPresetLibrarySessionState;
using Presentation = SequencerPresetLibraryPresentation;

FLASHMEM const char* spreadLabel(
    oc::note::sequencer::StepSequencerChordVoicing spread
) {
    using Spread = oc::note::sequencer::StepSequencerChordVoicing;
    switch (spread) {
        case Spread::Open: return "Open";
        case Spread::Wide: return "Wide";
        case Spread::Close:
        default: return "Close";
    }
}

FLASHMEM void formatOutputNotes(
    char* out,
    size_t outSize,
    const seq::SequencerChordPresetDescriptor& descriptor,
    const seq::SequencerChordPresetTarget& target
) {
    if (out == nullptr || outSize == 0U) return;
    size_t position = static_cast<size_t>(
        std::snprintf(out, outSize, "Output  ")
    );
    const uint8_t count = std::min<uint8_t>(
        descriptor.resolution.count,
        oc::note::sequencer::StepSequencerChordResolution::MAX_VOICES
    );
    for (uint8_t voice = 0; voice < count && position < outSize; ++voice) {
        char note[8]{};
        seq::note_spelling::formatNoteName(
            note,
            sizeof(note),
            descriptor.resolution.voices[voice].note,
            target.scale
        );
        const int written = std::snprintf(
            out + position,
            outSize - position,
            "%s%s",
            voice == 0U ? "" : " ",
            note
        );
        if (written <= 0) break;
        position += std::min<size_t>(
            static_cast<size_t>(written),
            outSize - position - 1U
        );
    }
}

FLASHMEM void populateVoiceRail(
    Presentation& data,
    const seq::SequencerChordPresetDescriptor& descriptor
) {
    const auto formula = oc::note::sequencer::resolveChordFormula(
        descriptor.projectedFormula,
        descriptor.targetBasis ==
            oc::note::sequencer::StepSequencerChordIntervalBasis::
                ScaleDegrees
    );
    if (!formula.valid || formula.count < 2U) return;

    const uint8_t count = std::min<uint8_t>(
        formula.count,
        static_cast<uint8_t>(data.chordVoiceRail.items.size())
    );
    data.chordVoiceRail.visible = true;
    data.chordVoiceRail.itemCount = count;
    data.chordVoiceRail.focusedItem = 0U;
    data.chordVoiceRail.color =
        core::ui::sequencer::semantic::color(
            core::ui::sequencer::semantic::Tone::CHORD_FORMULA
        );
    const bool degrees = formula.intervalUsesScaleDegrees;
    for (uint8_t voice = 0; voice < count; ++voice) {
        std::snprintf(
            data.voiceRailLabels[voice].data(),
            data.voiceRailLabels[voice].size(),
            voice == 0U ? "R" : "V%u",
            static_cast<unsigned>(voice + 1U)
        );
        const int interval = formula.intervals[voice];
        if (degrees) {
            std::snprintf(
                data.voiceRailValues[voice].data(),
                data.voiceRailValues[voice].size(),
                "%u",
                static_cast<unsigned>(std::max(0, interval) + 1)
            );
        } else if (interval == 0) {
            std::snprintf(
                data.voiceRailValues[voice].data(),
                data.voiceRailValues[voice].size(),
                "0"
            );
        } else {
            std::snprintf(
                data.voiceRailValues[voice].data(),
                data.voiceRailValues[voice].size(),
                "%+d",
                interval
            );
        }
        data.chordVoiceRail.items[voice] = {
            .label = data.voiceRailLabels[voice].data(),
            .value = data.voiceRailValues[voice].data(),
            .add = false,
            .enabled = true,
        };
    }
}

FLASHMEM void formatDetail(
    Presentation& data,
    const Picker& picker
) {
    const auto& chord = picker.chord();
    const auto& descriptor = chord.descriptor;
    const char* title = descriptor.semanticName[0] != '\0'
        ? descriptor.semanticName
        : (descriptor.technicalId[0] != '\0'
               ? descriptor.technicalId
               : "Chord Preset");
    std::strncpy(data.title.data(), title, data.title.size() - 1U);
    data.title.back() = '\0';

    if (!seq::sequencerChordPresetCanApply(descriptor.compatibility)) {
        std::snprintf(
            data.itemBuffers[0].data(),
            data.itemBuffers[0].size(),
            "Preset  %s",
            descriptor.technicalId[0] != '\0'
                ? descriptor.technicalId
                : "Unavailable"
        );
        std::snprintf(
            data.itemBuffers[1].data(),
            data.itemBuffers[1].size(),
            "Status  %s",
            seq::sequencerChordPresetCompatibilityLabel(
                descriptor.compatibility
            )
        );
        std::snprintf(
            data.itemBuffers[2].data(),
            data.itemBuffers[2].size(),
            "Load  Unavailable"
        );
        for (uint8_t index = 0; index < 3U; ++index) {
            data.items[index] = data.itemBuffers[index].data();
        }
        data.itemCount = 3;
        data.selectedIndex = std::clamp<int>(
            picker.detailFocus.get(),
            0,
            data.itemCount - 1
        );
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "%s",
            seq::sequencerChordPresetCompatibilityLabel(
                descriptor.compatibility
            )
        );
        return;
    }

    // The first two virtual rows are intentionally empty: the retained
    // custom-draw voice rail occupies this space without creating per-voice
    // LVGL children.
    data.itemBuffers[0][0] = '\0';
    data.itemBuffers[1][0] = '\0';
    const char* basis = descriptor.targetBasis ==
            oc::note::sequencer::StepSequencerChordIntervalBasis::
                ScaleDegrees
        ? "DEG"
        : "ST";
    std::snprintf(
        data.itemBuffers[2].data(),
        data.itemBuffers[2].size(),
        "Formula  %s · %u voices",
        basis,
        static_cast<unsigned>(descriptor.resolution.count)
    );
    std::snprintf(
        data.itemBuffers[3].data(),
        data.itemBuffers[3].size(),
        "Transform  Inv %u · %s · Strum %+d",
        static_cast<unsigned>(descriptor.projectedFormula.inversion()),
        spreadLabel(descriptor.projectedFormula.voicing()),
        static_cast<int>(descriptor.projectedFormula.strum)
    );
    formatOutputNotes(
        data.itemBuffers[4].data(),
        data.itemBuffers[4].size(),
        descriptor,
        chord.target
    );
    for (uint8_t index = 0; index < 5U; ++index) {
        data.items[index] = data.itemBuffers[index].data();
    }
    data.itemCount = 5;
    data.selectedIndex = 2 + std::clamp<int>(
        picker.detailFocus.get(),
        0,
        2
    );
    std::snprintf(
        data.meta.data(),
        data.meta.size(),
        "%s",
        seq::sequencerChordPresetCompatibilityLabel(
            descriptor.compatibility
        )
    );
    populateVoiceRail(data, descriptor);
}

}  // namespace

FLASHMEM SequencerPresetLibraryPresentation
buildSequencerChordPresetPresentation(
    const seq::SequencerState& sequencer
) {
    Presentation data{};
    const auto& picker = sequencer.presetLibrary;
    if (!picker.visible.get() ||
        picker.libraryKind.get() != seq::SequencerPresetLibraryKind::CHORD) {
        return data;
    }
    const auto& chord = picker.chord();

    using Mode = seq::SequencerPresetLibraryMode;
    const bool saveMode = picker.mode.get() == Mode::SAVE;
    data.visible = true;
    if (picker.detailVisible.get() && chord.descriptor.valid) {
        formatDetail(data, picker);
    } else {
        char idleMeta[56]{};
        if (!chord.target.canSave) {
            std::snprintf(
                idleMeta,
                sizeof(idleMeta),
                "Single note · nothing to save"
            );
        } else if (chord.target.targetUsesScaleDegrees) {
            std::snprintf(
                idleMeta,
                sizeof(idleMeta),
                "Target · %s · DEG",
                seq::scale_catalog::scaleTypeLabel(
                    chord.target.scale.type
                )
            );
        } else {
            std::snprintf(
                idleMeta,
                sizeof(idleMeta),
                "Target · Chromatic · ST"
            );
        }
        preset_library_presentation_common::formatList(
            data,
            picker,
            saveMode,
            {
                .kindLabel = "Chord",
                .itemIcon = ::standalone::icons::CHORD,
                .newItemIcon = ::standalone::icons::ACTION_CREATE,
                .itemIconColor = ::standalone::theme::color::STEP_CHORD,
                .newItemIconColor = ::standalone::theme::color::FOCUS_EDIT,
                .loadedFeedback = "Loaded into draft",
                .queuedFeedback = "Queued",
                .compatibility = chord.descriptor.valid
                    ? seq::sequencerChordPresetCompatibilityLabel(
                          chord.descriptor.compatibility
                      )
                    : "",
                .idleMeta = idleMeta,
            }
        );
    }

    uint32_t revision =
        preset_library_presentation_common::baseRevision(picker);
    revision = preset_library_presentation_common::mixRevision(
        revision,
        static_cast<uint32_t>(
            chord.descriptor.compatibility
        )
    );
    revision = preset_library_presentation_common::mixRevision(
        revision,
        chord.descriptor.previewKey.assetFingerprint
    );
    data.dataRevision = revision;
    return data;
}

}  // namespace core::ui::sequencer

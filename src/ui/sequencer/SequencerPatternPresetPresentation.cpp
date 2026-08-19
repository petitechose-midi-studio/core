#include "ui/sequencer/SequencerPatternPresetPresentation.hpp"

#include <algorithm>
#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "ui/sequencer/SequencerPresetLibraryPresentationCommon.hpp"

namespace core::ui::sequencer {
namespace {

namespace seq = core::state::sequencer;
using Picker = seq::SequencerPresetLibrarySessionState;
using Presentation = SequencerPresetLibraryPresentation;

FLASHMEM const char* trackKindLabel(seq::SequencerTrackKind kind) {
    return kind == seq::SequencerTrackKind::DRUM
        ? "Drum"
        : "Instrument";
}

FLASHMEM void formatDetail(Presentation& data, const Picker& picker) {
    const auto& pattern = picker.pattern();
    const auto& descriptor = pattern.descriptor;
    std::snprintf(
        data.title.data(),
        data.title.size(),
        "%s",
        descriptor.metadata.semanticName[0] != '\0'
            ? descriptor.metadata.semanticName
            : "Pattern Preset"
    );
    std::snprintf(
        data.itemBuffers[0].data(),
        data.itemBuffers[0].size(),
        "Type     %s · %s",
        seq::sequencerPatternPresetSourceLabel(descriptor.source),
        trackKindLabel(descriptor.metadata.trackKind)
    );
    std::snprintf(
        data.itemBuffers[1].data(),
        data.itemBuffers[1].size(),
        "Timing   %u steps · 1/%u",
        static_cast<unsigned>(descriptor.patternLength),
        static_cast<unsigned>(4U * descriptor.stepsPerBeat)
    );
    if (descriptor.metadata.trackKind == seq::SequencerTrackKind::DRUM) {
        std::snprintf(
            data.itemBuffers[2].data(),
            data.itemBuffers[2].size(),
            "Content  %u lanes · advanced steps",
            static_cast<unsigned>(descriptor.drumLaneCount)
        );
    } else {
        std::snprintf(
            data.itemBuffers[2].data(),
            data.itemBuffers[2].size(),
            "Content  Steps · graph · CC"
        );
    }
    for (uint8_t index = 0U; index < 3U; ++index) {
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
        seq::sequencerPatternPresetCompatibilityLabel(
            descriptor.compatibility
        )
    );
}

}  // namespace

FLASHMEM SequencerPresetLibraryPresentation
buildSequencerPatternPresetPresentation(
    const seq::SequencerState& sequencer
) {
    Presentation data{};
    const auto& picker = sequencer.presetLibrary;
    if (!picker.visible.get() ||
        picker.libraryKind.get() !=
            seq::SequencerPresetLibraryKind::PATTERN) {
        return data;
    }
    const auto& pattern = picker.pattern();
    const bool saveMode = picker.mode.get() ==
        seq::SequencerPresetLibraryMode::SAVE;
    data.visible = true;
    if (picker.detailVisible.get() && pattern.descriptor.valid) {
        formatDetail(data, picker);
    } else {
        char idleMeta[48]{};
        char compatibility[48]{};
        std::snprintf(
            idleMeta,
            sizeof(idleMeta),
            "%s · Track %u · %s",
            seq::sequencerPatternPresetSourceFilterLabel(
                pattern.sourceFilter
            ),
            static_cast<unsigned>(pattern.target.trackIndex + 1U),
            trackKindLabel(pattern.target.trackKind)
        );
        if (pattern.descriptor.valid) {
            std::snprintf(
                compatibility,
                sizeof(compatibility),
                "%s · %s",
                seq::sequencerPatternPresetSourceFilterLabel(
                    pattern.sourceFilter
                ),
                seq::sequencerPatternPresetCompatibilityLabel(
                    pattern.descriptor.compatibility
                )
            );
        }
        preset_library_presentation_common::formatList(
            data,
            picker,
            saveMode,
            {
                .kindLabel = "Pattern",
                .loadedFeedback = "Loaded into pattern",
                .queuedFeedback = "Queued for next loop",
                .warningFeedback = "Check compatibility",
                .compatibility = compatibility,
                .idleMeta = idleMeta,
            }
        );
    }

    uint32_t revision =
        preset_library_presentation_common::baseRevision(picker);
    revision = preset_library_presentation_common::mixRevision(
        revision,
        static_cast<uint32_t>(pattern.descriptor.compatibility)
    );
    revision = preset_library_presentation_common::mixRevision(
        revision,
        static_cast<uint32_t>(pattern.sourceFilter)
    );
    revision = preset_library_presentation_common::mixRevision(
        revision,
        static_cast<uint32_t>(pattern.descriptor.source)
    );
    revision = preset_library_presentation_common::mixRevision(
        revision,
        pattern.descriptor.previewKey.assetHash
    );
    data.dataRevision = revision;
    return data;
}

}  // namespace core::ui::sequencer

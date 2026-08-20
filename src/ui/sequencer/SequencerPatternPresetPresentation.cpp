#include "ui/sequencer/SequencerPatternPresetPresentation.hpp"

#include <algorithm>
#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "ui/sequencer/SequencerPresetLibraryPresentationCommon.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer {
namespace {

namespace seq = core::state::sequencer;
using Picker = seq::SequencerPresetLibrarySessionState;
using Presentation = SequencerPresetLibraryPresentation;

FLASHMEM const char* trackKindLabel(seq::SequencerTrackKind kind) {
    return kind == seq::SequencerTrackKind::DRUM
        ? "Drums"
        : "Instrument";
}

FLASHMEM void formatBreadcrumb(
    Presentation& data,
    const seq::SequencerPatternPresetLibraryState& pattern
) {
    std::snprintf(
        data.breadcrumb.data(),
        data.breadcrumb.size(),
        pattern.location.root() ? "%s" : "%s / %s",
        trackKindLabel(pattern.target.trackKind),
        pattern.location.relativeDirectory.data()
    );
}

FLASHMEM void formatUserBreadcrumb(
    Presentation& data,
    const seq::SequencerPatternPresetLocation& location
) {
    std::snprintf(
        data.breadcrumb.data(),
        data.breadcrumb.size(),
        location.root() ? "User" : "User / %s",
        location.relativeDirectory.data()
    );
}

FLASHMEM void formatManagement(Presentation& data, const Picker& picker) {
    const auto& pattern = picker.pattern();
    const bool folder = pattern.managedEntryKind ==
        seq::SequencerPresetLibraryEntryKind::FOLDER;
    std::snprintf(
        data.title.data(),
        data.title.size(),
        "Manage %s",
        folder ? "Folder" : "Pattern"
    );
    std::snprintf(
        data.meta.data(),
        data.meta.size(),
        "%s",
        pattern.managedEntryName.data()
    );
    formatUserBreadcrumb(data, pattern.managedLocation);
    constexpr const char* LABELS[] = {"Rename", "Move", "Delete"};
    const char* icons[] = {
        ::standalone::icons::ACTION_RENAME,
        ::standalone::icons::ACTION_MOVE,
        ::standalone::icons::ACTION_REMOVE,
    };
    const uint32_t colors[] = {
        ::standalone::theme::color::FOCUS_EDIT,
        ::standalone::theme::color::STEP_PITCH,
        ::standalone::theme::color::DESTRUCTIVE,
    };
    for (uint8_t index = 0U; index < 3U; ++index) {
        std::snprintf(
            data.itemBuffers[index].data(),
            data.itemBuffers[index].size(),
            "%s",
            LABELS[index]
        );
        data.items[index] = data.itemBuffers[index].data();
        data.itemIcons[index] = icons[index];
        data.itemIconColors[index] = colors[index];
    }
    data.itemCount = 3;
    data.selectedIndex = static_cast<int>(pattern.managementAction);
}

FLASHMEM void formatMoveDestination(
    Presentation& data,
    const Picker& picker
) {
    const auto& pattern = picker.pattern();
    const bool folder = pattern.managedEntryKind ==
        seq::SequencerPresetLibraryEntryKind::FOLDER;
    std::snprintf(
        data.title.data(),
        data.title.size(),
        "Move %s",
        folder ? "Folder" : "Pattern"
    );
    std::snprintf(
        data.meta.data(),
        data.meta.size(),
        "%s",
        pattern.managedEntryName.data()
    );
    formatUserBreadcrumb(data, pattern.location);

    std::snprintf(
        data.itemBuffers[0].data(),
        data.itemBuffers[0].size(),
        "Move here"
    );
    data.items[0] = data.itemBuffers[0].data();
    data.itemIcons[0] = ::standalone::icons::ACTION_MOVE;
    data.itemIconColors[0] = ::standalone::theme::color::STEP_PITCH;
    int itemIndex = 1;
    for (uint8_t index = 0U;
         index < picker.entryCount.get() &&
         itemIndex < static_cast<int>(data.items.size());
         ++index, ++itemIndex) {
        const char* name = picker.entryName(index)[0] != '\0'
            ? picker.entryName(index)
            : (picker.entryId(index)[0] == '@'
                ? picker.entryId(index) + 1
                : picker.entryId(index));
        std::snprintf(
            data.itemBuffers[itemIndex].data(),
            data.itemBuffers[itemIndex].size(),
            "%s",
            name
        );
        data.items[itemIndex] = data.itemBuffers[itemIndex].data();
        data.itemIcons[itemIndex] = ::standalone::icons::FOLDER;
        data.itemIconColors[itemIndex] =
            ::standalone::theme::color::FOCUS_EDIT;
    }
    data.itemCount = itemIndex;
    data.selectedIndex = std::clamp<int>(
        picker.selectedIndex.get(),
        0,
        itemIndex - 1
    );
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
    data.itemCount = 0;
    data.selectedIndex = 0;
    data.patternPreviewVisible = descriptor.visual.valid;
    if (descriptor.source == seq::SequencerPatternPresetSource::FACTORY) {
        data.headerIcon = ::standalone::icons::LOCK;
        data.headerIconColor = ::standalone::theme::color::FOCUS_EDIT;
    }
    std::snprintf(
        data.meta.data(),
        data.meta.size(),
        "%s",
        seq::sequencerPatternPresetCanApply(descriptor.compatibility)
            ? seq::sequencerPatternPresetSourceLabel(descriptor.source)
            : seq::sequencerPatternPresetCompatibilityLabel(
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
    if (pattern.panel == seq::SequencerPatternPresetLibraryPanel::MANAGE) {
        formatManagement(data, picker);
    } else if (pattern.panel ==
               seq::SequencerPatternPresetLibraryPanel::MOVE_DESTINATION) {
        formatMoveDestination(data, picker);
    } else if (picker.detailVisible.get() && pattern.descriptor.valid) {
        formatBreadcrumb(data, pattern);
        formatDetail(data, picker);
    } else {
        formatBreadcrumb(data, pattern);
        char idleMeta[48]{};
        char compatibility[48]{};
        if (pattern.factoryCopyPending) {
            std::snprintf(
                idleMeta,
                sizeof(idleMeta),
                "Copy · %s",
                pattern.copySourceName.data()
            );
        } else {
            std::snprintf(
                idleMeta,
                sizeof(idleMeta),
                "%s",
                seq::sequencerPatternPresetSourceFilterLabel(
                    pattern.sourceFilter
                )
            );
        }
        if (pattern.descriptor.valid) {
            const char* compatibilityLabel =
                seq::sequencerPatternPresetCompatibilityLabel(
                    pattern.descriptor.compatibility
                );
            std::snprintf(
                compatibility,
                sizeof(compatibility),
                "%s%s%s",
                seq::sequencerPatternPresetSourceFilterLabel(
                    pattern.sourceFilter
                ),
                seq::sequencerPatternPresetCanApply(
                    pattern.descriptor.compatibility
                ) ? "" : " · ",
                seq::sequencerPatternPresetCanApply(
                    pattern.descriptor.compatibility
                ) ? "" : compatibilityLabel
            );
        }
        preset_library_presentation_common::formatList(
            data,
            picker,
            saveMode,
            {
                .kindLabel = "Pattern",
                .itemIcon = ::standalone::icons::PATTERN,
                .newItemIcon = ::standalone::icons::ACTION_CREATE,
                .folderIcon = ::standalone::icons::FOLDER,
                .newFolderIcon = ::standalone::icons::FOLDER_ADD,
                .itemIconColor = ::standalone::theme::color::STEP_PITCH,
                .newItemIconColor = ::standalone::theme::color::FOCUS_EDIT,
                .folderIconColor = ::standalone::theme::color::FOCUS_EDIT,
                .loadedFeedback = "Loaded into pattern",
                .queuedFeedback = "Queued for next loop",
                .compatibility = compatibility,
                .idleMeta = idleMeta,
            }
        );
        if (pattern.factoryCopyPending &&
            picker.newAssetItemOffset() > 0U) {
            std::snprintf(
                data.itemBuffers[0].data(),
                data.itemBuffers[0].size(),
                "Copy here"
            );
            data.items[0] = data.itemBuffers[0].data();
            data.itemIcons[0] = ::standalone::icons::ACTION_COPY;
            data.itemIconColors[0] =
                ::standalone::theme::color::POSITIVE;
        }
        if (pattern.sourceFilter ==
            seq::SequencerPatternPresetSourceFilter::FACTORY) {
            data.headerIcon = ::standalone::icons::LOCK;
            data.headerIconColor = ::standalone::theme::color::FOCUS_EDIT;
        }
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
    revision = preset_library_presentation_common::mixRevision(
        revision,
        static_cast<uint32_t>(pattern.panel)
    );
    revision = preset_library_presentation_common::mixRevision(
        revision,
        static_cast<uint32_t>(pattern.managementAction)
    );
    revision = preset_library_presentation_common::mixRevision(
        revision,
        static_cast<uint32_t>(pattern.textEdit)
    );
    revision = preset_library_presentation_common::mixRevision(
        revision,
        pattern.factoryCopyPending ? 1U : 0U
    );
    data.dataRevision = revision;
    return data;
}

}  // namespace core::ui::sequencer

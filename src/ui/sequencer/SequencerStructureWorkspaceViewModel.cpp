#include "ui/sequencer/SequencerStructureWorkspaceViewModel.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "state/shared/StructureSlotOps.hpp"

namespace core::ui::sequencer {
namespace slots = core::state::shared;

FLASHMEM SequencerStructureWorkspaceProps buildSequencerStructureWorkspaceProps(
    const SequencerViewModelSource& source
) {
    SequencerStructureWorkspaceProps props{};
    const auto& workspace = source.sequencer.structureUi.workspace;
    props.visible = workspace.active.get();
    if (!props.visible) return props;

    const bool tracks = workspace.level.get() ==
        core::state::sequencer::SequencerStructureWorkspaceLevel::TRACKS;
    const uint8_t activeTrack = source.sharedTrackActive.get();
    const uint8_t activePage = source.sequencer.visiblePage();
    const uint8_t focused = tracks
        ? core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
              source.trackNavigation.previewTrackIndex.get()
          )
        : source.sequencer.clampPage(
              source.sequencer.structureUi.previewPageIndex.get()
          );
    const bool focusedAdd = tracks
        ? source.trackNavigation.previewAddSlot.get()
        : source.sequencer.structureUi.previewAddPageSlot.get();

    if (tracks) {
        std::snprintf(props.breadcrumb.data(), props.breadcrumb.size(), "STRUCTURE / TRACKS");
        std::snprintf(
            props.context.data(),
            props.context.size(),
            focusedAdd ? "+ Track %u" : "Track %u",
            static_cast<unsigned>(focused + 1U)
        );
        const uint16_t enabledMask = source.sharedTrackEnabledMask.get();
        const int addIndex = slots::firstDisabledIndex(
            enabledMask,
            SequencerStructureWorkspaceProps::ITEM_COUNT
        );
        for (uint8_t i = 0; i < props.items.size(); ++i) {
            auto& item = props.items[i];
            const bool enabled = slots::isEnabled(enabledMask, i);
            const bool add = addIndex >= 0 && i == static_cast<uint8_t>(addIndex);
            item.visible = enabled || add;
            item.focused = i == focused && focusedAdd == add;
            item.active = enabled && i == activeTrack;
            item.add = add;
            std::snprintf(
                item.label.data(),
                item.label.size(),
                add ? "+ Track" : "Track %u",
                static_cast<unsigned>(i + 1U)
            );
        }
        return props;
    }

    std::snprintf(
        props.breadcrumb.data(),
        props.breadcrumb.size(),
        "TRACK %u / PATTERNS",
        static_cast<unsigned>(activeTrack + 1U)
    );
    std::snprintf(
        props.context.data(),
        props.context.size(),
        focusedAdd ? "+ Next Pattern" : "Pattern %u",
        static_cast<unsigned>(focused + 1U)
    );
    const uint8_t pageCount = source.sequencer.activePageCount();
    for (uint8_t i = 0; i < props.items.size(); ++i) {
        auto& item = props.items[i];
        const bool enabled = i < pageCount;
        const bool add = pageCount < props.items.size() && i == pageCount;
        item.visible = enabled || add;
        item.focused = i == focused && focusedAdd == add;
        item.active = enabled && i == activePage;
        item.add = add;
        std::snprintf(
            item.label.data(),
            item.label.size(),
            add ? "+ Next" : "Pattern %u",
            static_cast<unsigned>(i + 1U)
        );
    }
    return props;
}

}  // namespace core::ui::sequencer

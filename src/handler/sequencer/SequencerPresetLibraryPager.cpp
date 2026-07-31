#include "handler/sequencer/SequencerPresetLibraryPager.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"

namespace core::handler {

FLASHMEM SequencerPresetLibraryPager::SequencerPresetLibraryPager(
    PickerState& picker,
    void* context,
    PageLoader loader
) : picker_(picker),
    context_(context),
    loader_(loader) {}

FLASHMEM void SequencerPresetLibraryPager::configure(
    void* context,
    PageLoader loader
) {
    context_ = context;
    loader_ = loader;
}

FLASHMEM bool SequencerPresetLibraryPager::refreshFirstPage() {
    return refreshPage(nullptr, PageDirection::FORWARD, false);
}

FLASHMEM bool SequencerPresetLibraryPager::refreshPage(
    const char* anchorExclusive,
    PageDirection direction,
    bool selectLast
) {
    Entry entries[PickerState::ENTRY_CAPACITY]{};
    core::persistence::ProductAssetFileListResult page{};
    if (loader_ == nullptr ||
        !loader_(
            context_,
            entries,
            PickerState::ENTRY_CAPACITY,
            anchorExclusive,
            direction,
            page
        )) {
        for (uint8_t index = 0;
             index < PickerState::ENTRY_CAPACITY;
             ++index) {
            picker_.setEntry(index, nullptr);
        }
        picker_.entryCount.set(0);
        picker_.truncated.set(false);
        picker_.hasPreviousPage.set(false);
        picker_.hasNextPage.set(false);
        picker_.totalEntryCount.set(0);
        picker_.selectedIndex.set(0);
        picker_.setFeedback(
            core::state::sequencer::SequencerPresetLibraryFeedback::FAILED
        );
        return false;
    }

    for (uint8_t index = 0;
         index < PickerState::ENTRY_CAPACITY;
         ++index) {
        picker_.setEntry(
            index,
            index < page.count ? entries[index].id : nullptr,
            index < page.count ? entries[index].semanticName : nullptr,
            index < page.count && entries[index].metadataReadable
        );
    }
    picker_.entryCount.set(page.count);
    picker_.truncated.set(page.truncated);
    picker_.hasPreviousPage.set(page.hasPrevious);
    picker_.hasNextPage.set(page.hasNext);
    picker_.totalEntryCount.set(page.totalCount);
    picker_.detailVisible.set(false);
    picker_.detailFocus.set(0);
    picker_.operationFeedback.set({});

    if (picker_.itemCount() == 0U) {
        picker_.selectedIndex.set(0);
        picker_.setFeedback(
            core::state::sequencer::SequencerPresetLibraryFeedback::EMPTY
        );
        return true;
    }
    picker_.selectedIndex.set(
        selectLast
            ? static_cast<uint8_t>(picker_.itemCount() - 1U)
            : picker_.newAssetItemOffset()
    );
    picker_.feedback.set(
        core::state::sequencer::SequencerPresetLibraryFeedback::NONE
    );
    picker_.bump();
    return true;
}

FLASHMEM bool
SequencerPresetLibraryPager::refreshPageContainingAndSelect(
    const char* assetId
) {
    if (assetId == nullptr || assetId[0] == '\0') return false;

    char pageAnchor[PickerState::ID_SIZE]{};
    bool hasPageAnchor = false;
    Entry preceding[PickerState::ENTRY_CAPACITY]{};
    core::persistence::ProductAssetFileListResult page{};
    if (loader_ == nullptr ||
        !loader_(
            context_,
            preceding,
            PickerState::ENTRY_CAPACITY,
            assetId,
            PageDirection::BACKWARD,
            page
        )) {
        (void)refreshFirstPage();
        return false;
    }
    if (page.count == PickerState::ENTRY_CAPACITY) {
        std::strncpy(
            pageAnchor,
            preceding[0].id,
            sizeof(pageAnchor) - 1U
        );
        hasPageAnchor = true;
    }

    if (!refreshPage(
            hasPageAnchor ? pageAnchor : nullptr,
            PageDirection::FORWARD,
            false
        )) {
        return false;
    }
    for (uint8_t index = 0;
         index < picker_.entryCount.get();
         ++index) {
        if (std::strcmp(picker_.entryId(index), assetId) != 0) continue;
        picker_.selectedIndex.set(static_cast<uint8_t>(
            index + picker_.newAssetItemOffset()
        ));
        picker_.bump();
        return true;
    }
    return false;
}

FLASHMEM bool SequencerPresetLibraryPager::move(float delta) {
    if (!picker_.visible.get() || !nav::hasTurnDelta(delta)) return false;
    const uint8_t count = picker_.itemCount();
    if (count == 0U) return false;
    const bool forward = delta > 0.0f;
    const uint8_t current = picker_.selectedIndex.get();
    if (forward && static_cast<uint8_t>(current + 1U) < count) {
        picker_.selectedIndex.set(static_cast<uint8_t>(current + 1U));
        return true;
    }
    if (!forward && current > 0U) {
        picker_.selectedIndex.set(static_cast<uint8_t>(current - 1U));
        return true;
    }
    if (forward && picker_.hasNextPage.get() &&
        picker_.entryCount.get() > 0U) {
        return refreshPage(
            picker_.entryId(static_cast<uint8_t>(
                picker_.entryCount.get() - 1U
            )),
            PageDirection::FORWARD,
            false
        );
    }
    if (!forward && picker_.hasPreviousPage.get() &&
        picker_.entryCount.get() > 0U) {
        return refreshPage(
            picker_.entryId(0),
            PageDirection::BACKWARD,
            true
        );
    }
    return false;
}

FLASHMEM void
SequencerPresetLibraryPager::toggleModePreservingSelection() {
    using Mode =
        core::state::sequencer::SequencerPresetLibraryMode;
    const bool selectedNew = picker_.selectedItemIsNewAsset();
    const uint8_t selectedEntry =
        picker_.existingEntryIndexForSelectedItem();
    const bool toSave = picker_.mode.get() == Mode::LOAD;
    picker_.mode.set(toSave ? Mode::SAVE : Mode::LOAD);

    if (!toSave && selectedNew) {
        // The creation command has no Load-mode counterpart. Focus the first
        // existing asset on the same page when possible.
        picker_.selectedIndex.set(0);
    } else {
        picker_.selectedIndex.set(static_cast<uint8_t>(
            selectedEntry + picker_.newAssetItemOffset()
        ));
    }
    picker_.clampSelection();
    picker_.detailVisible.set(false);
    picker_.detailFocus.set(0);
    picker_.actionGuard.set({});
    picker_.operationFeedback.set({});
    picker_.feedback.set(
        core::state::sequencer::SequencerPresetLibraryFeedback::NONE
    );
    picker_.bump();
}

FLASHMEM bool
SequencerPresetLibraryPager::focusedExistingAsset() const {
    return picker_.selectedItemIsExistingAsset();
}

FLASHMEM const char*
SequencerPresetLibraryPager::selectedAssetId() const {
    if (!focusedExistingAsset()) return "";
    return picker_.entryId(
        picker_.existingEntryIndexForSelectedItem()
    );
}

}  // namespace core::handler

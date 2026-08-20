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
    clearPending_();
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPresetLibraryPager::refreshFirstPage() {
    return refreshPage(nullptr, PageDirection::FORWARD, false);
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPresetLibraryPager::refreshPage(
    const char* anchorExclusive,
    PageDirection direction,
    bool selectLast
) {
    clearPending_();
    rememberPage_(anchorExclusive, direction, selectLast);
    return requestPage_(anchorExclusive, direction, selectLast);
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPresetLibraryPager::requestPage_(
    const char* anchorExclusive,
    PageDirection direction,
    bool selectLast
) {
    Entry entries[PickerState::ENTRY_CAPACITY]{};
    core::persistence::ProductAssetFileListResult page{};
    const auto status = loader_ != nullptr
        ? loader_(
              context_,
              entries,
              PickerState::ENTRY_CAPACITY,
              anchorExclusive,
              direction,
              page
          )
        : PageLoadStatus::FAILED;
    if (status == PageLoadStatus::PENDING) {
        markPending_();
        return status;
    }
    if (status == PageLoadStatus::FAILED) return failPage_();
    clearPending_();
    return applyPage_(entries, page, selectLast);
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPresetLibraryPager::applyPage_(
    const Entry* entries,
    const core::persistence::ProductAssetFileListResult& page,
    bool selectLast
) {

    for (uint8_t index = 0;
         index < PickerState::ENTRY_CAPACITY;
         ++index) {
        picker_.setEntry(
            index,
            index < page.count ? entries[index].id : nullptr,
            index < page.count ? entries[index].semanticName : nullptr,
            index < page.count && entries[index].metadataReadable,
            index < page.count && entries[index].kind ==
                    core::persistence::ProductDirectoryAssetEntryKind::FOLDER
                ? core::state::sequencer::
                      SequencerPresetLibraryEntryKind::FOLDER
                : core::state::sequencer::
                      SequencerPresetLibraryEntryKind::ASSET,
            index < page.count ? entries[index].displayValue : nullptr
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
        return PageLoadStatus::READY;
    }
    if (picker_.entryCount.get() == 0U) {
        // In Save mode the synthetic "+ New" command is the sole row. The
        // existing-entry offset is therefore not a valid selection.
        picker_.selectedIndex.set(0);
        picker_.feedback.set(
            core::state::sequencer::SequencerPresetLibraryFeedback::NONE
        );
        picker_.bump();
        return PageLoadStatus::READY;
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
    return PageLoadStatus::READY;
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPresetLibraryPager::refreshPageContainingAndSelect(
    const char* assetId
) {
    clearPending_();
    if (assetId == nullptr || assetId[0] == '\0') {
        return PageLoadStatus::FAILED;
    }

    std::strncpy(
        pending_asset_id_,
        assetId,
        sizeof(pending_asset_id_) - 1U
    );
    pending_kind_ = PendingKind::CONTAINING_BACKWARD;
    return continueContainingBackward_();
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPresetLibraryPager::continueContainingBackward_() {
    Entry preceding[PickerState::ENTRY_CAPACITY]{};
    core::persistence::ProductAssetFileListResult page{};
    const auto status = loader_ != nullptr
        ? loader_(
              context_,
              preceding,
              PickerState::ENTRY_CAPACITY,
              pending_asset_id_,
              PageDirection::BACKWARD,
              page
          )
        : PageLoadStatus::FAILED;
    if (status == PageLoadStatus::PENDING) {
        markPending_();
        return status;
    }
    if (status == PageLoadStatus::FAILED) return failPage_();

    pending_anchor_[0] = '\0';
    if (page.count == PickerState::ENTRY_CAPACITY) {
        std::strncpy(
            pending_anchor_,
            preceding[0].id,
            sizeof(pending_anchor_) - 1U
        );
    }
    pending_kind_ = PendingKind::CONTAINING_FORWARD;
    return continueContainingForward_();
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPresetLibraryPager::continueContainingForward_() {
    Entry entries[PickerState::ENTRY_CAPACITY]{};
    core::persistence::ProductAssetFileListResult page{};
    const auto status = loader_ != nullptr
        ? loader_(
              context_,
              entries,
              PickerState::ENTRY_CAPACITY,
              pending_anchor_[0] != '\0' ? pending_anchor_ : nullptr,
              PageDirection::FORWARD,
              page
          )
        : PageLoadStatus::FAILED;
    if (status == PageLoadStatus::PENDING) {
        markPending_();
        return status;
    }
    if (status == PageLoadStatus::FAILED) return failPage_();

    char wanted[PickerState::ID_SIZE]{};
    std::strncpy(wanted, pending_asset_id_, sizeof(wanted) - 1U);
    (void)applyPage_(entries, page, false);
    for (uint8_t index = 0;
         index < picker_.entryCount.get();
         ++index) {
        if (std::strcmp(picker_.entryId(index), wanted) != 0) continue;
        picker_.selectedIndex.set(static_cast<uint8_t>(
            index + picker_.newAssetItemOffset()
        ));
        clearPending_();
        picker_.bump();
        return PageLoadStatus::READY;
    }
    clearPending_();
    picker_.setFeedback(
        core::state::sequencer::SequencerPresetLibraryFeedback::FAILED
    );
    return PageLoadStatus::FAILED;
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPresetLibraryPager::retryPending() {
    switch (pending_kind_) {
        case PendingKind::PAGE: {
            char anchor[PickerState::ID_SIZE]{};
            std::strncpy(anchor, pending_anchor_, sizeof(anchor) - 1U);
            return requestPage_(
                anchor[0] != '\0' ? anchor : nullptr,
                pending_direction_,
                pending_select_last_
            );
        }
        case PendingKind::CONTAINING_BACKWARD:
            return continueContainingBackward_();
        case PendingKind::CONTAINING_FORWARD:
            return continueContainingForward_();
        case PendingKind::NONE:
        default:
            return PageLoadStatus::READY;
    }
}

FLASHMEM SequencerPresetLibraryPager::PageLoadStatus
SequencerPresetLibraryPager::failPage_() {
    clearPending_();
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
    return PageLoadStatus::FAILED;
}

FLASHMEM void SequencerPresetLibraryPager::rememberPage_(
    const char* anchorExclusive,
    PageDirection direction,
    bool selectLast
) {
    pending_kind_ = PendingKind::PAGE;
    pending_direction_ = direction;
    pending_select_last_ = selectLast;
    pending_anchor_[0] = '\0';
    if (anchorExclusive != nullptr) {
        std::strncpy(
            pending_anchor_,
            anchorExclusive,
            sizeof(pending_anchor_) - 1U
        );
    }
}

FLASHMEM void SequencerPresetLibraryPager::markPending_() {
    using Feedback =
        core::state::sequencer::SequencerPresetLibraryFeedback;
    if (picker_.feedback.get() != Feedback::QUEUED) {
        picker_.feedback.set(Feedback::QUEUED);
        picker_.bump();
    }
}

FLASHMEM void SequencerPresetLibraryPager::clearPending_() {
    pending_kind_ = PendingKind::NONE;
    pending_direction_ = PageDirection::FORWARD;
    pending_select_last_ = false;
    pending_anchor_[0] = '\0';
    pending_asset_id_[0] = '\0';
}

FLASHMEM bool SequencerPresetLibraryPager::pending() const {
    return pending_kind_ != PendingKind::NONE;
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
        ) == PageLoadStatus::READY;
    }
    if (!forward && picker_.hasPreviousPage.get() &&
        picker_.entryCount.get() > 0U) {
        return refreshPage(
            picker_.entryId(0),
            PageDirection::BACKWARD,
            true
        ) == PageLoadStatus::READY;
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
    if (!picker_.selectedItemIsExistingAsset()) return false;
    return picker_.entryKind(
        picker_.existingEntryIndexForSelectedItem()
    ) == core::state::sequencer::SequencerPresetLibraryEntryKind::ASSET;
}

FLASHMEM bool SequencerPresetLibraryPager::focusedFolder() const {
    if (!picker_.selectedItemIsExistingAsset()) return false;
    return picker_.entryKind(
        picker_.existingEntryIndexForSelectedItem()
    ) == core::state::sequencer::SequencerPresetLibraryEntryKind::FOLDER;
}

FLASHMEM const char* SequencerPresetLibraryPager::selectedEntryId() const {
    if (!picker_.selectedItemIsExistingAsset()) return "";
    return picker_.entryId(
        picker_.existingEntryIndexForSelectedItem()
    );
}

FLASHMEM const char*
SequencerPresetLibraryPager::selectedAssetId() const {
    if (!focusedExistingAsset()) return "";
    return selectedEntryId();
}

}  // namespace core::handler

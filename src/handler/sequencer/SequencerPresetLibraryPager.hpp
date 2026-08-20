#pragma once

#include <cstdint>

#include "persistence/ProductAssetFileStore.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

class SequencerPresetLibraryPager {
public:
    using PickerState =
        core::state::sequencer::SequencerPresetLibrarySessionState;
    using Entry = core::persistence::ProductAssetFileListEntry;
    using PageDirection =
        core::persistence::ProductAssetFilePageDirection;
    enum class PageLoadStatus : uint8_t {
        READY = 0,
        PENDING,
        FAILED,
    };
    using PageLoader = PageLoadStatus (*)(
        void* context,
        Entry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        PageDirection direction,
        core::persistence::ProductAssetFileListResult& out
    );

    SequencerPresetLibraryPager(
        PickerState& picker,
        void* context,
        PageLoader loader
    );

    void configure(void* context, PageLoader loader);
    PageLoadStatus refreshFirstPage();
    PageLoadStatus refreshPage(
        const char* anchorExclusive,
        PageDirection direction,
        bool selectLast
    );
    PageLoadStatus refreshPageContainingAndSelect(const char* assetId);
    PageLoadStatus retryPending();
    bool move(float delta);
    void toggleModePreservingSelection();

    [[nodiscard]] bool pending() const;
    [[nodiscard]] bool focusedExistingAsset() const;
    [[nodiscard]] bool focusedFolder() const;
    [[nodiscard]] const char* selectedEntryId() const;
    [[nodiscard]] const char* selectedAssetId() const;

private:
    enum class PendingKind : uint8_t {
        NONE = 0,
        PAGE,
        CONTAINING_BACKWARD,
        CONTAINING_FORWARD,
    };

    PageLoadStatus requestPage_(
        const char* anchorExclusive,
        PageDirection direction,
        bool selectLast
    );
    PageLoadStatus continueContainingBackward_();
    PageLoadStatus continueContainingForward_();
    PageLoadStatus applyPage_(
        const Entry* entries,
        const core::persistence::ProductAssetFileListResult& page,
        bool selectLast
    );
    PageLoadStatus failPage_();
    void rememberPage_(
        const char* anchorExclusive,
        PageDirection direction,
        bool selectLast
    );
    void markPending_();
    void clearPending_();

    PickerState& picker_;
    void* context_ = nullptr;
    PageLoader loader_ = nullptr;
    PendingKind pending_kind_ = PendingKind::NONE;
    PageDirection pending_direction_ = PageDirection::FORWARD;
    bool pending_select_last_ = false;
    char pending_anchor_[PickerState::ID_SIZE] = {};
    char pending_asset_id_[PickerState::ID_SIZE] = {};
};

}  // namespace core::handler

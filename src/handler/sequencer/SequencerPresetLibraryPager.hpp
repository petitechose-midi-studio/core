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
    using PageLoader = bool (*)(
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
    bool refreshFirstPage();
    bool refreshPage(
        const char* anchorExclusive,
        PageDirection direction,
        bool selectLast
    );
    bool refreshPageContainingAndSelect(const char* assetId);
    bool move(float delta);
    void toggleModePreservingSelection();

    [[nodiscard]] bool focusedExistingAsset() const;
    [[nodiscard]] const char* selectedAssetId() const;

private:
    PickerState& picker_;
    void* context_ = nullptr;
    PageLoader loader_ = nullptr;
};

}  // namespace core::handler

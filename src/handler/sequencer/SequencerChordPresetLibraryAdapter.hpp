#pragma once

#include "handler/sequencer/SequencerChordPresetDomainServices.hpp"
#include "handler/sequencer/SequencerPresetLibraryWorkflow.hpp"

namespace core::handler {

class SequencerChordPresetLibraryAdapter {
public:
    SequencerChordPresetLibraryAdapter(
        core::state::sequencer::SequencerState& sequencer,
        SequencerChordPresetDomainServices& chordPresets
    );

    [[nodiscard]] SequencerPresetLibraryAdapter operations();

private:
    using PickerState =
        core::state::sequencer::SequencerPresetLibrarySessionState;
    using Mode =
        core::state::sequencer::SequencerPresetLibraryMode;

    static bool beginSession_(void* context);
    static SequencerPresetLibraryPager::PageLoadStatus loadPage_(
        void* context,
        SequencerPresetLibraryPager::Entry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        SequencerPresetLibraryPager::PageDirection direction,
        core::persistence::ProductAssetFileListResult& out
    );
    static void clearInspection_(void* context);
    static core::state::sequencer::SequencerPresetLibraryFeedback inspect_(
        void* context,
        const char* assetId,
        bool force
    );
    static uint8_t detailRowCount_(const void* context);
    static void adjustFocusedDetail_(
        void* context,
        const char* assetId,
        float delta
    );
    static core::state::contextual::ContextActionSpec actionSpec_(
        const void* context,
        bool saveMode,
        bool selectedNewAsset,
        bool hasFocusedAsset
    );
    static bool shouldCommitBeforeLoad_(
        const void* context,
        bool hasFocusedAsset
    );
    static SequencerPresetLibraryResult execute_(
        void* context,
        Mode mode,
        const char* assetId,
        bool createNew,
        bool overwriteAuthorized
    );
    static SequencerPresetLibraryResult update_(
        void* context,
        uint32_t nowMs
    );

    bool beginSession();
    SequencerPresetLibraryPager::PageLoadStatus loadPage(
        SequencerPresetLibraryPager::Entry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        SequencerPresetLibraryPager::PageDirection direction,
        core::persistence::ProductAssetFileListResult& out
    );
    void clearInspection();
    core::state::sequencer::SequencerPresetLibraryFeedback inspect(
        const char* assetId,
        bool force
    );
    core::state::contextual::ContextActionSpec actionSpec(
        bool saveMode,
        bool selectedNewAsset,
        bool hasFocusedAsset
    ) const;
    SequencerPresetLibraryResult execute(
        Mode mode,
        const char* assetId,
        bool createNew,
        bool overwriteAuthorized
    );

    core::state::sequencer::SequencerState& sequencer_;
    SequencerChordPresetDomainServices& chord_presets_;
};

}  // namespace core::handler

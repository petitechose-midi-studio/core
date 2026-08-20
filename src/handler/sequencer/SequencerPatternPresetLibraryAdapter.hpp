#pragma once

#include "handler/sequencer/SequencerPatternPresetDomainServices.hpp"
#include "handler/sequencer/SequencerPresetLibraryWorkflow.hpp"

namespace core::handler {

class SequencerPatternPresetLibraryAdapter {
public:
    SequencerPatternPresetLibraryAdapter(
        core::state::sequencer::SequencerState& sequencer,
        SequencerPatternPresetDomainServices& patternPresets
    );

    [[nodiscard]] SequencerPresetLibraryAdapter operations();
    [[nodiscard]] bool previewActive() const {
        return preview_session_.active();
    }
    SequencerPresetLibraryResult confirmPreview();
    SequencerPresetLibraryResult cancelPreview();
    void updatePreview();

private:
    using PickerState =
        core::state::sequencer::SequencerPresetLibrarySessionState;
    using Mode = core::state::sequencer::SequencerPresetLibraryMode;

    friend class SequencerPresetLibraryAdapterBinding<
        SequencerPatternPresetLibraryAdapter>;

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
    uint8_t detailRowCount() const;
    void adjustFocusedDetail(const char* assetId, float delta);
    core::state::contextual::ContextActionSpec actionSpec(
        bool saveMode,
        bool selectedNewAsset,
        bool hasFocusedAsset
    ) const;
    bool shouldCommitBeforeLoad(bool hasFocusedAsset) const;
    SequencerPresetLibraryResult execute(
        Mode mode,
        const char* assetId,
        bool createNew,
        bool overwriteAuthorized
    );
    SequencerPresetLibraryResult update(uint32_t nowMs);
    bool enterFolder(const char* entryId);
    bool leaveFolder();
    SequencerPresetLibraryResult createFolder(const char* folderName);
    bool beginManagement(
        core::state::sequencer::SequencerPresetLibraryEntryKind kind,
        const char* entryId,
        const char* entryName
    );
    SequencerPresetLibraryResult renameManaged(const char* newName);
    SequencerPresetLibraryResult moveManaged();
    SequencerPresetLibraryResult deleteManaged();

    static bool enterFolder_(void* context, const char* entryId);
    static bool leaveFolder_(void* context);
    static SequencerPresetLibraryResult createFolder_(
        void* context,
        const char* folderName
    );
    static bool beginManagement_(
        void* context,
        core::state::sequencer::SequencerPresetLibraryEntryKind kind,
        const char* entryId,
        const char* entryName
    );
    static SequencerPresetLibraryResult renameManaged_(
        void* context,
        const char* newName
    );
    static SequencerPresetLibraryResult moveManaged_(void* context);
    static SequencerPresetLibraryResult deleteManaged_(void* context);

    core::state::sequencer::SequencerState& sequencer_;
    SequencerPatternPresetDomainServices& pattern_presets_;
    SequencerPatternPresetPreviewSession preview_session_{};
};

}  // namespace core::handler

#pragma once

#include "handler/sequencer/SequencerPresetLibraryWorkflow.hpp"
#include "handler/sequencer/SequencerStepPresetDomainServices.hpp"

namespace core::handler {

class SequencerStepPresetLibraryAdapter {
public:
    SequencerStepPresetLibraryAdapter(
        core::state::sequencer::SequencerState& sequencer,
        SequencerStepPresetDomainServices& stepPresets
    );

    [[nodiscard]] SequencerPresetLibraryAdapter operations();

private:
    using PickerState =
        core::state::sequencer::SequencerPresetLibrarySessionState;
    using Mode =
        core::state::sequencer::SequencerPresetLibraryMode;

    friend class SequencerPresetLibraryAdapterBinding<
        SequencerStepPresetLibraryAdapter>;

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

    core::state::sequencer::SequencerState& sequencer_;
    SequencerStepPresetDomainServices& step_presets_;
};

}  // namespace core::handler

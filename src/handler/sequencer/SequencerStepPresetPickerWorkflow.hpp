#pragma once

#include <array>
#include <cstdint>

#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/sequencer/SequencerStepPresetDomainServices.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

enum class SequencerStepPresetPickerOutcome : uint8_t {
    NONE = 0,
    SAVED,
    LOAD_EMPTY,
    LOAD_FAILED,
    APPLIED,
    QUEUED,
    CANCELLED,
    BLOCKED,
};

class SequencerStepPresetPickerWorkflow {
public:
    SequencerStepPresetPickerWorkflow(
        core::state::sequencer::SequencerState& sequencer,
        SequencerStepPresetDomainServices& stepPresets,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays
    );

    void open();
    void close();
    void move(float delta);
    void toggleDetail();
    void movePreviewState(float delta);
    void toggleMode();
    bool shouldCommitBeforeLoad() const;
    core::state::contextual::ContextActionSpec actionSpec() const;
    bool actionGuardEngaged() const;
    bool beginActionGuard(uint32_t nowMs);
    SequencerStepPresetPickerOutcome update(uint32_t nowMs);
    bool cancelActionGuard(uint32_t nowMs);
    SequencerStepPresetPickerOutcome executeTap(uint32_t nowMs);
    SequencerStepPresetPickerOutcome commitActionGuard(uint32_t nowMs);

private:
    using PickerState =
        core::state::sequencer::SequencerStepPresetPickerState;

    struct LoadPageCache {
        bool valid = false;
        uint8_t selectedIndex = 0;
        uint8_t entryCount = 0;
        bool hasPrevious = false;
        bool hasNext = false;
        uint16_t totalCount = 0;
        uint8_t previewStateIndex = 0;
        core::state::sequencer::SequencerStepPresetDescriptor descriptor{};
        std::array<
            std::array<char, PickerState::ID_SIZE>,
            PickerState::ENTRY_CAPACITY
        > entryIds{};
        std::array<
            std::array<char, PickerState::NAME_SIZE>,
            PickerState::ENTRY_CAPACITY
        > entryNames{};
        std::array<bool, PickerState::ENTRY_CAPACITY> entryMetadataReadable{};
    };

    void refreshFirstPage();
    bool refreshPage(
        const char* anchorExclusive,
        core::persistence::StepPresetFilePageDirection direction,
        bool selectLast
    );
    bool refreshPageContainingAndSelect(const char* presetId);
    void cacheLoadPage();
    void restoreLoadPage();
    void inspectFocused(bool force = false);
    bool focusedExistingAsset() const;
    const char* selectedPresetId() const;
    void setFeedback(const SequencerStepPresetActionResult& result);
    void setInspectionFeedback(const SequencerStepPresetInspectResult& result);
    SequencerStepPresetPickerOutcome executeCurrentAction(
        bool overwriteAuthorized,
        uint32_t nowMs
    );
    void publishOperationFeedback(
        core::state::contextual::OperationFeedbackStatus status,
        core::state::contextual::ContextActionReason reason,
        core::state::contextual::OperationFeedbackExpiryPolicy expiry,
        uint32_t nowMs,
        uint32_t durationMs = 0,
        core::state::contextual::ContextActionId completedAction =
            core::state::contextual::ContextActionId::NONE
    );

    core::state::sequencer::SequencerState& sequencer_;
    SequencerStepPresetDomainServices& step_presets_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    LoadPageCache load_page_cache_{};
};

}  // namespace core::handler

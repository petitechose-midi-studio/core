#pragma once

#include <cstdint>

#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/sequencer/SequencerPresetLibraryPager.hpp"
#include "state/contextual/ContextActionSpec.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

enum class SequencerPresetLibraryOutcome : uint8_t {
    NONE = 0,
    SAVED,
    LOAD_EMPTY,
    LOAD_FAILED,
    LOADED,
    QUEUED,
    CANCELLED,
    BLOCKED,
};

struct SequencerPresetLibraryResult {
    SequencerPresetLibraryOutcome outcome =
        SequencerPresetLibraryOutcome::NONE;
    core::state::sequencer::SequencerPresetLibraryFeedback feedback =
        core::state::sequencer::SequencerPresetLibraryFeedback::NONE;
    core::state::contextual::ContextActionReason reason =
        core::state::contextual::ContextActionReason::NONE;
    bool refreshPublishedState = false;
    char assetId[
        core::state::sequencer::SequencerPresetLibrarySessionState::ID_SIZE
    ] = {};
};

/**
 * Allocation-free domain boundary for the shared preset-library workflow.
 *
 * The shell owns gesture semantics, paging, action guards and feedback.
 * Adapters own only target capture, domain inspection and the actual
 * save/load transaction.
 */
struct SequencerPresetLibraryAdapter {
    using Mode =
        core::state::sequencer::SequencerPresetLibraryMode;
    using Kind =
        core::state::sequencer::SequencerPresetLibraryKind;
    using Entry = SequencerPresetLibraryPager::Entry;
    using PageDirection = SequencerPresetLibraryPager::PageDirection;

    void* context = nullptr;
    Kind kind = Kind::STEP;
    bool (*beginSession)(void* context) = nullptr;
    bool (*loadPage)(
        void* context,
        Entry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        PageDirection direction,
        core::persistence::ProductAssetFileListResult& out
    ) = nullptr;
    void (*clearInspection)(void* context) = nullptr;
    core::state::sequencer::SequencerPresetLibraryFeedback (*inspect)(
        void* context,
        const char* assetId,
        bool force
    ) = nullptr;
    uint8_t (*detailRowCount)(const void* context) = nullptr;
    void (*adjustFocusedDetail)(
        void* context,
        const char* assetId,
        float delta
    ) = nullptr;
    core::state::contextual::ContextActionSpec (*actionSpec)(
        const void* context,
        bool saveMode,
        bool selectedNewAsset,
        bool hasFocusedAsset
    ) = nullptr;
    bool (*shouldCommitBeforeLoad)(
        const void* context,
        bool hasFocusedAsset
    ) = nullptr;
    SequencerPresetLibraryResult (*execute)(
        void* context,
        Mode mode,
        const char* assetId,
        bool createNew,
        bool overwriteAuthorized
    ) = nullptr;
    SequencerPresetLibraryResult (*update)(
        void* context,
        uint32_t nowMs
    ) = nullptr;

    [[nodiscard]] bool valid() const {
        return context != nullptr && beginSession != nullptr &&
               loadPage != nullptr && clearInspection != nullptr &&
               inspect != nullptr && actionSpec != nullptr &&
               execute != nullptr;
    }
};

class SequencerPresetLibraryWorkflow {
public:
    SequencerPresetLibraryWorkflow(
        core::state::sequencer::SequencerState& sequencer,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays
    );

    bool open(const SequencerPresetLibraryAdapter& adapter);
    void close();
    /**
     * Back is hierarchical: cancel an active guard, then Detail -> List,
     * then List -> invoking editor. Returns true only when the library closed.
     */
    bool back(uint32_t nowMs);
    void move(float delta, uint32_t nowMs);
    void enterDetail();
    void adjustFocusedDetail(float delta);
    void toggleMode();

    [[nodiscard]] bool active() const;
    [[nodiscard]] bool shouldCommitBeforeLoad(
        bool guardedAction = false
    ) const;
    [[nodiscard]] core::state::contextual::ContextActionSpec
        actionSpec() const;
    [[nodiscard]] bool actionGuardEngaged() const;

    bool beginActionGuard(uint32_t nowMs);
    SequencerPresetLibraryResult update(uint32_t nowMs);
    bool cancelActionGuard(uint32_t nowMs);
    SequencerPresetLibraryResult executeTap(uint32_t nowMs);
    SequencerPresetLibraryResult commitActionGuard(uint32_t nowMs);

private:
    bool refreshPage(
        const char* anchorExclusive,
        SequencerPresetLibraryPager::PageDirection direction,
        bool selectLast
    );
    bool refreshPageContainingAndSelect(const char* assetId);
    void scheduleFocusedInspection(uint32_t nowMs);
    void completePendingInspection();
    void inspectFocused(bool force = false);
    [[nodiscard]] bool operationPending() const;
    SequencerPresetLibraryResult executeCurrentAction(
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
    void publishTerminalResult(
        const SequencerPresetLibraryResult& result,
        uint32_t nowMs
    );
    static SequencerPresetLibraryResult blockedResult(
        core::state::contextual::ContextActionReason reason
    );

    core::state::sequencer::SequencerState& sequencer_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    SequencerPresetLibraryAdapter adapter_{};
    SequencerPresetLibraryPager pager_;
    bool inspection_pending_ = false;
    uint32_t inspection_due_at_ms_ = 0;
};

}  // namespace core::handler

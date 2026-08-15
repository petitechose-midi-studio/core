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
    /** Catalog/storage admission is accepted; retry the unchanged action. */
    RETRY_PENDING,
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
    SequencerPresetLibraryPager::PageLoadStatus (*loadPage)(
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

// Template instantiations need their own COMDAT-compatible section. Teensy's
// linker collects every .flashmem* section into cold Flash.
#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    #define MS_PRESET_BINDING_FLASHMEM \
        __attribute__((section(".flashmem.preset_binding")))
#else
    #define MS_PRESET_BINDING_FLASHMEM
#endif

/** Binds a concrete preset domain to the allocation-free workflow callbacks. */
template <typename DomainAdapter>
class SequencerPresetLibraryAdapterBinding {
public:
    static MS_PRESET_BINDING_FLASHMEM SequencerPresetLibraryAdapter bind(
        DomainAdapter& domain,
        SequencerPresetLibraryAdapter::Kind kind
    ) {
        SequencerPresetLibraryAdapter adapter{};
        adapter.context = &domain;
        adapter.kind = kind;
        adapter.beginSession = beginSession;
        adapter.loadPage = loadPage;
        adapter.clearInspection = clearInspection;
        adapter.inspect = inspect;
        adapter.detailRowCount = detailRowCount;
        adapter.adjustFocusedDetail = adjustFocusedDetail;
        adapter.actionSpec = actionSpec;
        adapter.shouldCommitBeforeLoad = shouldCommitBeforeLoad;
        adapter.execute = execute;
        adapter.update = update;
        return adapter;
    }

private:
    static MS_PRESET_BINDING_FLASHMEM bool beginSession(void* context) {
        return static_cast<DomainAdapter*>(context)->beginSession();
    }

    static MS_PRESET_BINDING_FLASHMEM
    SequencerPresetLibraryPager::PageLoadStatus loadPage(
        void* context,
        SequencerPresetLibraryAdapter::Entry* entries,
        uint8_t capacity,
        const char* anchorExclusive,
        SequencerPresetLibraryAdapter::PageDirection direction,
        core::persistence::ProductAssetFileListResult& out
    ) {
        return static_cast<DomainAdapter*>(context)->loadPage(
            entries,
            capacity,
            anchorExclusive,
            direction,
            out
        );
    }

    static MS_PRESET_BINDING_FLASHMEM void clearInspection(void* context) {
        static_cast<DomainAdapter*>(context)->clearInspection();
    }

    static MS_PRESET_BINDING_FLASHMEM
    core::state::sequencer::SequencerPresetLibraryFeedback
    inspect(void* context, const char* assetId, bool force) {
        return static_cast<DomainAdapter*>(context)->inspect(assetId, force);
    }

    static MS_PRESET_BINDING_FLASHMEM uint8_t detailRowCount(
        const void* context
    ) {
        return static_cast<const DomainAdapter*>(context)->detailRowCount();
    }

    static MS_PRESET_BINDING_FLASHMEM void adjustFocusedDetail(
        void* context,
        const char* assetId,
        float delta
    ) {
        static_cast<DomainAdapter*>(context)->adjustFocusedDetail(assetId, delta);
    }

    static MS_PRESET_BINDING_FLASHMEM
    core::state::contextual::ContextActionSpec actionSpec(
        const void* context,
        bool saveMode,
        bool selectedNewAsset,
        bool hasFocusedAsset
    ) {
        return static_cast<const DomainAdapter*>(context)->actionSpec(
            saveMode,
            selectedNewAsset,
            hasFocusedAsset
        );
    }

    static MS_PRESET_BINDING_FLASHMEM bool shouldCommitBeforeLoad(
        const void* context,
        bool hasFocusedAsset
    ) {
        return static_cast<const DomainAdapter*>(context)
            ->shouldCommitBeforeLoad(hasFocusedAsset);
    }

    static MS_PRESET_BINDING_FLASHMEM SequencerPresetLibraryResult execute(
        void* context,
        SequencerPresetLibraryAdapter::Mode mode,
        const char* assetId,
        bool createNew,
        bool overwriteAuthorized
    ) {
        return static_cast<DomainAdapter*>(context)->execute(
            mode,
            assetId,
            createNew,
            overwriteAuthorized
        );
    }

    static MS_PRESET_BINDING_FLASHMEM SequencerPresetLibraryResult update(
        void* context,
        uint32_t nowMs
    ) {
        return static_cast<DomainAdapter*>(context)->update(nowMs);
    }
};

#undef MS_PRESET_BINDING_FLASHMEM

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
    enum class PendingPagePurpose : uint8_t {
        NONE = 0,
        BROWSE,
        POST_SAVE,
    };

    SequencerPresetLibraryPager::PageLoadStatus refreshPage(
        const char* anchorExclusive,
        SequencerPresetLibraryPager::PageDirection direction,
        bool selectLast
    );
    SequencerPresetLibraryPager::PageLoadStatus
        refreshPageContainingAndSelect(const char* assetId);
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
    PendingPagePurpose pending_page_purpose_ = PendingPagePurpose::NONE;
    bool action_retry_pending_ = false;
    bool action_retry_overwrite_authorized_ = false;
};

}  // namespace core::handler

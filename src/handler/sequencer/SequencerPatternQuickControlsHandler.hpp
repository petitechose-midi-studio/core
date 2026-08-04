#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/StructureNavigationState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

/**
 * Binds sequencer pattern quick controls to buttons and encoders.
 *
 * A modal gesture edits one detached PSRAM Pattern preview. Apply publishes it
 * through the prepared History transaction; Cancel only discards it.
 */
class SequencerPatternQuickControlsHandler {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::sequencer::SequencerState& sequencer;
        core::state::TrackNavigationState& trackNavigation;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        SequencerHistoryDomainServices history;
    };

    SequencerPatternQuickControlsHandler(
        StateRefs state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID scopeId
    );

    SequencerPatternQuickControlsHandler(const SequencerPatternQuickControlsHandler&) = delete;
    SequencerPatternQuickControlsHandler& operator=(const SequencerPatternQuickControlsHandler&) = delete;

private:
    void setupBindings();
    void open();
    void closeApply();
    void closeCancel();
    void navigate(float delta);
    bool setFocusedValue(float normalized);
    void setFocusedValueDirect(float normalized);
    void configureOptForFocusedItem();
    void clampFocusToLength();
    void prepareQuickControlsForOpen();
    bool abortPreparedQuickControlsHistory();
    bool beginPreparedQuickControlsHistory();
    bool ensurePreparedQuickControlsHistory();
    core::state::sequencer::SequencerPreparedPatternEditCommitOutcome
    applyNestedStepDraftQuickControls();
    void showHistoryRejection(core::state::sequencer::SequencerHistoryRejectionReason reason);
    void closeTransientQuickControlsState();
    int focusedItemOrderIndex() const;
    void setFocusedItemByOrderIndex(int index);
    int currentOffsetMax() const;
    float offsetToNormalized(int offsetSteps) const;
    int normalizedToOffset(float normalized) const;
    bool applyOffsetDelta(int offsetSteps);

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::TrackNavigationState& track_ui_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    SequencerHistoryDomainServices history_;
    bool history_retry_required_ = false;
    bool nested_step_draft_ = false;
};

}  // namespace core::handler

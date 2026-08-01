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
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

/**
 * Binds sequencer pattern quick controls to buttons and encoders.
 *
 * The handler snapshots the pattern for cancel/offset behavior, applies length,
 * division, and offset changes through sequencer state ops, and leaves playback
 * runtime untouched.
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
    bool captureOffsetSnapshot();
    void discardModalSnapshots();
    void closeTransientQuickControlsState();
    int focusedItemOrderIndex() const;
    void setFocusedItemByOrderIndex(int index);
    int currentOffsetMax() const;
    float offsetToNormalized(int offsetSteps) const;
    int normalizedToOffset(float normalized) const;
    bool applyOffsetFromSnapshot(int offsetSteps);
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
    core::state::sequencer::SequencerHistoryPatternSnapshot cancel_snapshot_{};
    core::state::sequencer::SequencerHistoryPatternSnapshot offset_snapshot_{};
    core::state::sequencer::SequencerHistoryPatternSnapshot history_snapshot_{};
    SequencerHistoryDomainServices history_;
    bool cancel_snapshot_valid_ = false;
    bool offset_snapshot_valid_ = false;
    bool history_snapshot_valid_ = false;
    bool cancel_retry_required_ = false;
};

}  // namespace core::handler

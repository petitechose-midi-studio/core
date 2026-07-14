#pragma once

/**
 * @file SequencerStepHandler.hpp
 * @brief Standalone sequencer step editing bindings
 */

#include <cstdint>
#include <cstddef>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "handler/common/ButtonReleaseLatch.hpp"
#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/sequencer/SequencerStructureEditWorkflow.hpp"
#include "handler/sequencer/SequencerStructureNavigationWorkflow.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/StatusBarState.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::validation::ux {
struct StructureUxTraceState;
}

namespace core::handler {

/**
 * Sequencer view bindings:
 * - MACRO_1..MACRO_8 release: toggle step in current page
 * - NAV turn/release: structure navigation, add-slot preview, selection mode
 * - BOTTOM_LEFT / BOTTOM_RIGHT: focus and selection clear/remove/copy/paste
 */
class SequencerStepHandler {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        core::state::TrackNavigationState& trackNavigation;
        core::state::project::ProjectNavigationState& projectNavigation;
        core::state::StructureClipboardState& structureClipboard;
        SharedTrackDomainServices sharedTracks;
        SequencerHistoryDomainServices history;
        core::state::sequencer::SequencerTrackActivationQueue* trackActivations = nullptr;
        core::state::StatusBarState* statusBar = nullptr;
    };

    SequencerStepHandler(StateRefs state,
                        oc::api::EncoderAPI& encoders,
                        oc::api::ButtonAPI& buttons,
                        oc::type::ScopeID scopeId
#if defined(MS_UX_RECORDER)
                        ,
                        core::validation::ux::StructureUxTraceState* uxTraceState = nullptr
#endif
    );

    ~SequencerStepHandler();

    SequencerStepHandler(const SequencerStepHandler&) = delete;
    SequencerStepHandler& operator=(const SequencerStepHandler&) = delete;
    SequencerStepHandler(SequencerStepHandler&&) = delete;
    SequencerStepHandler& operator=(SequencerStepHandler&&) = delete;

    void update(uint32_t nowMs);

private:
    void setupBindings();

    void toggleStep(uint8_t indexInPage);
    bool selectionHasItems() const;
    bool childPatternContentActionsAvailable() const;
    bool currentStructureBottomActionsAvailable() const;
    bool focusedStepHasChildContent() const;
    bool canPasteFocusedStepContent() const;
    bool trackFocusActive() const;
    bool trackSelectionActive() const;
    void acquireDetailsTransportLock();
    void deferDetailsTransportUnlock();
    void restoreDetailsTransportLock();
    void clearFocusedStepContent();
    void copyFocusedStepContent();
    void pasteFocusedStepContent();
    void recordFocusedContentEdit(
        core::state::sequencer::SequencerHistoryPatternSnapshot before,
        bool beforeCaptured
    );

    core::state::sequencer::SequencerState& sequencer_;
    core::state::StructureClipboardState& structure_clipboard_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_ui_;
    core::state::StatusBarState* status_bar_ = nullptr;
    SequencerStructureNavigationWorkflow navigation_workflow_;
    SequencerStructureEditWorkflow edit_workflow_;
    SequencerHistoryDomainServices history_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    ButtonReleaseLatch<1> nav_release_latch_;
    ButtonReleaseLatch<8> step_selection_macro_release_latch_;
    ButtonReleaseLatch<2> bottom_action_release_latch_;
    bool details_button_owned_ = false;
    bool details_transport_lock_owned_ = false;
    bool details_unlock_pending_ = false;
#if defined(MS_UX_RECORDER)
    core::validation::ux::StructureUxTraceState* ux_trace_state_ = nullptr;
#endif
};

}  // namespace core::handler

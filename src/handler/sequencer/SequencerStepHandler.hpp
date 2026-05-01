#pragma once

/**
 * @file SequencerStepHandler.hpp
 * @brief Standalone sequencer step editing bindings
 */

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerStructureEditWorkflow.hpp"
#include "handler/sequencer/SequencerStructureNavigationWorkflow.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::validation::ux {
struct StructureUxTraceState;
}

namespace core::handler {

/**
 * Sequencer view bindings:
 * - MACRO_1..MACRO_8 release: toggle step in current page
 * - NAV turn/release: structure navigation, add-slot preview, selection mode
 * - BOTTOM_LEFT / BOTTOM_RIGHT: structure erase/remove/copy/paste/duplicate
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
        core::state::StructureClipboardState& structureClipboard;
        SharedTrackDomainServices sharedTracks;
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

    ~SequencerStepHandler() = default;

    SequencerStepHandler(const SequencerStepHandler&) = delete;
    SequencerStepHandler& operator=(const SequencerStepHandler&) = delete;
    SequencerStepHandler(SequencerStepHandler&&) = delete;
    SequencerStepHandler& operator=(SequencerStepHandler&&) = delete;

private:
    void setupBindings();

    void toggleStep(uint8_t indexInPage);

    core::state::sequencer::SequencerState& sequencer_;
    SequencerStructureNavigationWorkflow navigation_workflow_;
    SequencerStructureEditWorkflow edit_workflow_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    bool nav_long_press_used_ = false;
    bool ignore_next_bottom_left_release_ = false;
    bool ignore_next_bottom_right_release_ = false;
#if defined(MS_UX_RECORDER)
    core::validation::ux::StructureUxTraceState* ux_trace_state_ = nullptr;
#endif
};

}  // namespace core::handler

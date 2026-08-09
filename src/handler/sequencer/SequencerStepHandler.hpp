#pragma once

/**
 * @file SequencerStepHandler.hpp
 * @brief Standalone sequencer step editing bindings
 */

#include <cstddef>
#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "handler/common/ButtonReleaseLatch.hpp"
#include "handler/common/SharedTrackDomainServices.hpp"
#include "handler/sequencer/SequencerContextSelectorWorkflow.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/sequencer/SequencerStructureEditWorkflow.hpp"
#include "handler/sequencer/SequencerStructureNavigationWorkflow.hpp"
#include "state/StatusBarState.hpp"
#include "state/StructureSelectionInteractionPolicy.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::validation::ux {
struct StructureUxTraceState;
}

namespace core::handler {

class SequencerStepEditHandler;
class SequencerPatternEditorHandler;
class ProjectTrackEditorHandler;

/**
 * Sequencer view bindings:
 * - MACRO_1..MACRO_8 release: toggle step in current page
 * - NAV turn/release: structure navigation, add-slot preview, selection mode
 * - BOTTOM_LEFT / BOTTOM_RIGHT: contextual focus and selection actions
 */
class SequencerStepHandler {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        oc::state::Signal<core::state::StructureNavigationFocus,
                          core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        core::state::TrackNavigationState& trackNavigation;
        core::state::project::ProjectNavigationState& projectNavigation;
        core::state::project::ProjectTrackState& projectTracks;
        core::state::project::ProjectTrackDomainServices projectTrackDomain;
        core::state::StructureClipboardState& structureClipboard;
        SharedTrackDomainServices sharedTracks;
        SequencerHistoryDomainServices history;
        core::state::macro::MacroPagesState& macroPages;
        core::state::sequencer::SequencerTrackActivationQueue* trackActivations = nullptr;
        core::state::StatusBarState* statusBar = nullptr;
    };

    SequencerStepHandler(StateRefs state, oc::api::EncoderAPI& encoders,
                         oc::api::ButtonAPI& buttons, oc::type::ScopeID scopeId
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

    /** Explicit content-selection entry used by the Step action selector. */
    void enterSelectionModeForCurrentFocus();

    /** Late wiring avoids making the two view-scope handlers own each other. */
    void attachStepEditHandler(SequencerStepEditHandler& handler);
    void attachPatternEditorHandler(SequencerPatternEditorHandler& handler);
    void attachTrackEditorHandler(ProjectTrackEditorHandler& handler);
private:
    void setupBindings();

    void toggleStep(uint8_t indexInPage);
    bool selectionHasItems() const;
    core::state::StructureSelectionInteractionPolicy selectionInteractionPolicy() const;
    bool childPatternContentActionsAvailable() const;
    bool currentStructureBottomActionsAvailable() const;
    bool focusedStepHasChildContent() const;
    bool canPasteFocusedStepContent() const;
    bool trackFocusActive() const;
    void clearFocusedStepContent();
    void copyFocusedStepContent();
    void pasteFocusedStepContent();
    void applyStepContentDraft();
    void backFromStepContentDraft();
    void moveStepContentDraftExitChoice(float delta);
    void confirmStepContentDraftExitChoice();
    void continueStepContentDraft();
    void handleContextSelectorRelease();
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    void confirmDrumTrackUxPrototypeType();
    void handleDrumTrackUxPrototypeNavTurn(float delta);
    void handleDrumTrackUxPrototypeNavPress();
    void handleDrumTrackUxPrototypeNavRelease();
    void handleDrumTrackUxPrototypeBack();
    void editDrumTrackUxPrototypeOpt(float normalized);
    void editDrumTrackUxPrototypeStepProperty(
        uint8_t indexInPage,
        float normalized
    );
#endif

    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    core::state::StructureClipboardState& structure_clipboard_;
    oc::state::Signal<core::state::StructureNavigationFocus,
                      core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::TrackNavigationState& track_ui_;
    SequencerStructureEditWorkflow edit_workflow_;
    SequencerStructureNavigationWorkflow navigation_workflow_;
    SequencerHistoryDomainServices history_;
    SequencerContextSelectorWorkflow context_selector_workflow_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    ButtonReleaseLatch<8> step_selection_macro_release_latch_;
    ButtonReleaseLatch<2> bottom_action_release_latch_;
    SequencerStepEditHandler* step_edit_handler_ = nullptr;
    SequencerPatternEditorHandler* pattern_editor_handler_ = nullptr;
    ProjectTrackEditorHandler* track_editor_handler_ = nullptr;
#if defined(MS_UX_RECORDER)
    core::validation::ux::StructureUxTraceState* ux_trace_state_ = nullptr;
#endif
};

#if defined(MS_UX_RECORDER)
static_assert(
    sizeof(void*) != 4U || sizeof(SequencerStepHandler) == 260U,
    "Sequencer Step handler exceeds its ARM UX-recorder PSRAM contract"
);
#else
static_assert(
    sizeof(void*) != 4U || sizeof(SequencerStepHandler) == 256U,
    "Sequencer Step handler exceeds its ARM PSRAM contract"
);
#endif

}  // namespace core::handler

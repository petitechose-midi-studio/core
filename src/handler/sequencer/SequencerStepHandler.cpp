#include "SequencerStepHandler.hpp"

#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/TimeCompat.hpp>

#include "handler/sequencer/ProjectTrackEditorHandler.hpp"
#include "handler/sequencer/SequencerPatternEditorHandler.hpp"
#include "handler/sequencer/SequencerStepContentDraftWorkflow.hpp"
#include "handler/sequencer/SequencerStepEditHandler.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

#if defined(MS_UX_RECORDER)
#include "validation/ux/SemanticUxTraceState.hpp"
#endif

namespace core::handler {

namespace {

using SelectionAction = core::state::StructureSelectionInteractionAction;
namespace seq = core::state::sequencer;

constexpr auto kStepToggleOwner = seq::SequencerPreparedPatternEditOwner::StepToggle;

FLASHMEM bool commitPatternHistoryBarrier(SequencerHistoryDomainServices& history) {
    return history.commitCoalescedPatternEditOutcome() !=
           seq::SequencerPatternHistoryCommitOutcome::Failed;
}

FLASHMEM bool publishPatternHistoryBarrier(seq::SequencerState& sequencer, bool committed) {
    if (committed) return true;
    sequencer.historyFeedback.showRejection(
        seq::SequencerHistoryRejectionReason::HistoryUnavailable, core::time_compat::millis());
    return false;
}

FLASHMEM seq::SequencerHistoryDescriptor stepToggleDescriptor(uint8_t step, bool beforeEnabled,
                                                              bool afterEnabled) {
    return {
        .kind = seq::SequencerHistoryActionKind::StepToggle,
        .stepIndex = step,
        .property = seq::StepProperty::NOTE,
        .hasValue = beforeEnabled != afterEnabled,
        .beforeValue = beforeEnabled ? 1 : 0,
        .afterValue = afterEnabled ? 1 : 0,
    };
}

}  // namespace

FLASHMEM SequencerStepHandler::SequencerStepHandler(
    StateRefs state, oc::api::EncoderAPI& encoders, oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId
#if defined(MS_UX_RECORDER)
    ,
    core::validation::ux::StructureUxTraceState* uxTraceState
#endif
    )
    : sequencer_(state.sequencer), tracks_(state.tracks),
      structure_clipboard_(state.structureClipboard), navigation_focus_(state.navigationFocus),
      track_ui_(state.trackNavigation),
      edit_workflow_(SequencerStructureEditWorkflow::StateRefs{
          state.sequencer,
          state.tracks,
          state.navigationFocus,
          state.trackNavigation,
          state.projectNavigation,
          state.projectTracks,
          state.projectTrackDomain,
          state.structureClipboard,
          state.sharedTracks,
          state.history,
          state.macroPages,
          state.trackActivations,
          state.statusBar,
      }),
      navigation_workflow_(SequencerStructureNavigationWorkflow::StateRefs{
          state.sequencer,
          state.tracks,
          state.navigationFocus,
          state.trackNavigation,
          edit_workflow_.sharedTrackServices(),
      }),
      history_(state.history), context_selector_workflow_(state.sequencer.contextSelector),
      encoders_(encoders), buttons_(buttons), scope_id_(scopeId)
#if defined(MS_UX_RECORDER)
      ,
      ux_trace_state_(uxTraceState)
#endif
{
    setupBindings();
}

FLASHMEM SequencerStepHandler::~SequencerStepHandler() = default;

FLASHMEM void SequencerStepHandler::update(uint32_t nowMs) {
    edit_workflow_.update(nowMs);
    context_selector_workflow_.update();
}

FLASHMEM void SequencerStepHandler::attachStepEditHandler(SequencerStepEditHandler& handler) {
    step_edit_handler_ = &handler;
}

FLASHMEM void SequencerStepHandler::attachPatternEditorHandler(
    SequencerPatternEditorHandler& handler) {
    pattern_editor_handler_ = &handler;
}

FLASHMEM void SequencerStepHandler::attachTrackEditorHandler(ProjectTrackEditorHandler& handler) {
    track_editor_handler_ = &handler;
}

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
FLASHMEM void SequencerStepHandler::confirmDrumTrackUxPrototypeType() {
    auto& prototype = sequencer_.drumTrackUxPrototype;
    if (!prototype.pickerVisible()) return;

    const bool drum = prototype.selectedKind ==
        seq::DrumTrackUxPrototypeKind::DRUM;
    const auto result = edit_workflow_.createPreviewedTrackStructure();
    if (!result.settled()) return;

    // Track creation can rotate/reset the active editor owner. Keep the
    // experiment armed without making that transient state part of history.
    if (!prototype.armed) prototype.arm();
    if (drum) {
        prototype.enterGrid();
    } else {
        prototype.close();
    }
}
#endif

FLASHMEM void SequencerStepHandler::handleContextSelectorRelease() {
    const auto outcome = context_selector_workflow_.release();
    switch (outcome.action) {
        case SequencerContextSelectorAction::APPLY_CONTEXT:
            if (!publishPatternHistoryBarrier(sequencer_, commitPatternHistoryBarrier(history_))) {
                return;
            }
            navigation_workflow_.setNavigationFocus(outcome.focus);
            return;
        case SequencerContextSelectorAction::OPEN_STEP_EDITOR:
            if (outcome.focus != core::state::StructureNavigationFocus::STEP ||
                navigation_focus_.get() !=
                    core::state::StructureNavigationFocus::STEP ||
                sequencer_.focusedStep.get() != outcome.previewTarget) {
                return;
            }
            if (step_edit_handler_ != nullptr) {
                (void)step_edit_handler_->openFocusedStepAtRow(
                    core::state::sequencer::step_edit_rows::ACTIVATED);
            }
            return;
        case SequencerContextSelectorAction::OPEN_PATTERN_EDITOR:
            if (outcome.focus != core::state::StructureNavigationFocus::PAGE ||
                navigation_focus_.get() !=
                    core::state::StructureNavigationFocus::PAGE ||
                sequencer_.structureUi.previewPageIndex.get() !=
                    outcome.previewTarget || outcome.previewAddSlot) {
                return;
            }
            if (pattern_editor_handler_ != nullptr &&
                core::state::sequencer::isRootContentView(sequencer_)) {
                (void)pattern_editor_handler_->openFromCurrentPage();
            }
            return;
        case SequencerContextSelectorAction::OPEN_TRACK_EDITOR:
            if (outcome.focus != core::state::StructureNavigationFocus::TRACK ||
                !trackFocusActive() ||
                track_ui_.previewTrackIndex.get() != outcome.previewTarget) {
                return;
            }
            if (outcome.previewAddSlot) {
                if (!track_ui_.previewAddSlot.get() ||
                    track_ui_.previewTrackIndex.get() !=
                        outcome.previewTarget) {
                    return;
                }
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
                if (sequencer_.drumTrackUxPrototype.armed) {
                    sequencer_.drumTrackUxPrototype.openTypePicker(
                        outcome.previewTarget
                    );
                    return;
                }
#endif
                if (!edit_workflow_.createPreviewedTrackStructure().settled()) {
                    return;
                }
                return;
            }
            if (track_ui_.previewAddSlot.get()) return;
            if (track_editor_handler_ != nullptr &&
                core::state::sequencer::isRootContentView(sequencer_)) {
                (void)track_editor_handler_->openActiveTrack();
            }
            return;
        case SequencerContextSelectorAction::NONE:
        default: return;
    }
}

FLASHMEM bool SequencerStepHandler::trackFocusActive() const {
    return navigation_focus_.get() == core::state::StructureNavigationFocus::TRACK;
}

FLASHMEM void SequencerStepHandler::enterSelectionModeForCurrentFocus() {
    if (!publishPatternHistoryBarrier(sequencer_, commitPatternHistoryBarrier(history_))) return;
    navigation_workflow_.enterSelectionModeForCurrentFocus();
}

FLASHMEM void SequencerStepHandler::setupBindings() {
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    // The prototype owns a complete native-only interaction mode. Button
    // priority makes strict gesture routing deterministic; encoder bindings
    // are registered first and therefore win within this view scope.
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return sequencer_.drumTrackUxPrototype.pickerVisible(); })
        .then([this](float delta) {
            sequencer_.drumTrackUxPrototype.moveKind(delta);
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return sequencer_.drumTrackUxPrototype.gridVisible(); })
        .then([this](float delta) {
            sequencer_.drumTrackUxPrototype.moveLane(delta);
        });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when([this]() { return sequencer_.drumTrackUxPrototype.gridVisible(); })
        .then([this](float delta) {
            sequencer_.drumTrackUxPrototype.moveProperty(delta);
        });

    buttons_.button(Config::ButtonID::NAV)
        .press()
        .scope(scope_id_)
        .priority(100)
        .when([this]() { return sequencer_.drumTrackUxPrototype.active(); })
        .then([]() {});

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() { return sequencer_.drumTrackUxPrototype.pickerVisible(); })
        .then([this]() { confirmDrumTrackUxPrototypeType(); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() { return sequencer_.drumTrackUxPrototype.gridVisible(); })
        .then([]() {});

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() { return sequencer_.drumTrackUxPrototype.active(); })
        .then([this]() { sequencer_.drumTrackUxPrototype.close(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() { return sequencer_.drumTrackUxPrototype.gridVisible(); })
        .then([this]() { sequencer_.drumTrackUxPrototype.movePage(-1); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .priority(100)
        .when([this]() { return sequencer_.drumTrackUxPrototype.gridVisible(); })
        .then([this]() { sequencer_.drumTrackUxPrototype.movePage(1); });

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .priority(100)
            .when([this]() { return sequencer_.drumTrackUxPrototype.gridVisible(); })
            .then([this, i]() {
                sequencer_.drumTrackUxPrototype.toggleVisibleStep(i);
            });

        encoders_.encoder(Config::MACRO_ENCODERS[i])
            .turn()
            .scope(scope_id_)
            .when([this]() { return sequencer_.drumTrackUxPrototype.gridVisible(); })
            .then([this, i](float normalized) {
                if (sequencer_.drumTrackUxPrototype.property !=
                    seq::DrumTrackUxPrototypeProperty::VELOCITY) {
                    return;
                }
                sequencer_.drumTrackUxPrototype.setVisibleStepVelocity(
                    i,
                    normalized
                );
            });
    }
#endif

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return sequencer_.stepContentDraft.exitPromptVisible.get(); })
        .then([this](float delta) { moveStepContentDraftExitChoice(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() { return sequencer_.stepContentDraft.exitPromptVisible.get(); })
        .then([this]() { confirmStepContentDraftExitChoice(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() { return sequencer_.stepContentDraft.exitPromptVisible.get(); })
        .then([this]() { continueStepContentDraft(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isChildContentView(sequencer_) &&
                   sequencer_.stepContentDraft.active.get() &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get();
        })
        .then([this]() { applyStepContentDraft(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .when([this]() { return edit_workflow_.trackPastePlanInspectable(); })
        .then([this]() { edit_workflow_.toggleTrackPasteDetails(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() { return edit_workflow_.trackPasteNavigationBlocked(); })
        .then([this]() { edit_workflow_.cancelTrackPasteAction(core::time_compat::millis()); });

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
            .scope(scope_id_)
            .when([this]() { return sequencer_.structureUi.stepSelection.active.get(); })
            .then(
                [this, i]() { step_selection_macro_release_latch_.arm(Config::MACRO_BUTTONS[i]); });

        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .when([this]() { return sequencer_.structureUi.stepSelection.active.get(); })
            .then([this, i]() {
                if (step_selection_macro_release_latch_.consume(Config::MACRO_BUTTONS[i])) {
                    return;
                }
                navigation_workflow_.toggleStepSelectionAtVisibleIndex(i);
            });

        buttons_.button(Config::MACRO_BUTTONS[i])
            .release()
            .scope(scope_id_)
            .when([this]() { return navigation_workflow_.allowsMainBindings(); })
            .then([this, i]() {
                if (step_selection_macro_release_latch_.consume(Config::MACRO_BUTTONS[i])) {
                    return;
                }
                toggleStep(i);
            });
    }

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() { return context_selector_workflow_.active(); })
        .then([this](float delta) { (void)context_selector_workflow_.turn(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isChildContentView(sequencer_) &&
                   !context_selector_workflow_.active() &&
                   navigation_workflow_.allowsMainBindings() &&
                   !edit_workflow_.trackPasteNavigationBlocked() &&
                   !navigation_workflow_.stepFocusActive();
        })
        .then([this](float delta) {
            if (delta == 0.0f) return;
            const uint8_t pages = core::state::sequencer::activeContentPageCount(sequencer_);
            if (pages <= 1U) return;
            const int direction = delta > 0.0f ? 1 : -1;
            const int next = static_cast<int>(sequencer_.page.get()) + direction;
            sequencer_.page.set(core::state::sequencer::normalizeActiveContentPage(
                sequencer_, static_cast<uint8_t>((next + pages) % pages)));
            sequencer_.focusedStep.set(core::state::sequencer::activeContentPageStartStep(
                sequencer_, sequencer_.page.get()));
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return selectionInteractionPolicy().navTurn == SelectionAction::MOVE_CURSOR &&
                   !context_selector_workflow_.active() &&
                   !edit_workflow_.trackPasteNavigationBlocked() &&
                   !edit_workflow_.trackRemoveNavigationBlocked();
        })
        .then([this](float delta) { navigation_workflow_.navigateSelection(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isChildContentView(sequencer_) &&
                   !context_selector_workflow_.active() &&
                   navigation_workflow_.allowsMainBindings() &&
                   !edit_workflow_.trackPasteNavigationBlocked() &&
                   !edit_workflow_.trackRemoveNavigationBlocked() &&
                   navigation_workflow_.stepFocusActive();
        })
        .then([this](float delta) {
            if (!publishPatternHistoryBarrier(sequencer_, commitPatternHistoryBarrier(history_))) {
                return;
            }
            navigation_workflow_.moveByFocus(delta);
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isRootContentView(sequencer_) &&
                   !context_selector_workflow_.active() &&
                   navigation_workflow_.allowsMainBindings() &&
                   !edit_workflow_.trackPasteNavigationBlocked() &&
                   !edit_workflow_.trackRemoveNavigationBlocked();
        })
        .then([this](float delta) {
            if (!publishPatternHistoryBarrier(sequencer_, commitPatternHistoryBarrier(history_))) {
                return;
            }
            navigation_workflow_.moveByFocus(delta);
        });

    buttons_.button(Config::ButtonID::NAV)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.allowsMainBindings() &&
                   !navigation_workflow_.selectionActive() &&
                   !edit_workflow_.trackPasteNavigationBlocked() &&
                   !edit_workflow_.trackRemoveNavigationBlocked();
        })
        .then([this]() {
            const auto focus = navigation_focus_.get();
            const bool previewAddSlot =
                focus == core::state::StructureNavigationFocus::TRACK &&
                track_ui_.previewAddSlot.get();
            const uint8_t previewTarget =
                focus == core::state::StructureNavigationFocus::TRACK
                    ? track_ui_.previewTrackIndex.get()
                    : focus == core::state::StructureNavigationFocus::STEP
                        ? sequencer_.focusedStep.get()
                        : sequencer_.structureUi.previewPageIndex.get();
            context_selector_workflow_.press(
                focus,
                core::state::sequencer::isRootContentView(sequencer_),
                previewTarget,
                previewAddSlot
            );
        });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return context_selector_workflow_.active(); })
        .then([this]() {
            const auto focus = navigation_focus_.get();
            const bool previewAddSlot =
                focus == core::state::StructureNavigationFocus::TRACK &&
                track_ui_.previewAddSlot.get();
            const uint8_t previewTarget =
                focus == core::state::StructureNavigationFocus::TRACK
                    ? track_ui_.previewTrackIndex.get()
                    : focus == core::state::StructureNavigationFocus::STEP
                        ? sequencer_.focusedStep.get()
                        : sequencer_.structureUi.previewPageIndex.get();
            if (!context_selector_workflow_.holdForSelection(
                    focus,
                    previewTarget,
                    previewAddSlot
                )) {
                return;
            }
            enterSelectionModeForCurrentFocus();
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return context_selector_workflow_.active() &&
                   !edit_workflow_.trackPasteNavigationBlocked();
        })
        .then([this]() {
            if (context_selector_workflow_.active()) {
                handleContextSelectorRelease();
                return;
            }
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return selectionInteractionPolicy().navRelease == SelectionAction::TOGGLE_ITEM &&
                   !edit_workflow_.trackPasteNavigationBlocked();
        })
        .then([this]() { navigation_workflow_.toggleSelectionAtCursor(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.allowsMainBindings() &&
                   core::state::sequencer::isChildContentView(sequencer_) &&
                   !edit_workflow_.trackPasteNavigationBlocked();
        })
        .then([this]() {
            if (core::state::sequencer::isChildContentView(sequencer_)) {
                if (sequencer_.stepContentDraft.active.get()) {
                    backFromStepContentDraft();
                    return;
                }
                if (!publishPatternHistoryBarrier(sequencer_,
                                                  commitPatternHistoryBarrier(history_))) {
                    return;
                }
                core::state::sequencer::leaveContentView(sequencer_);
                return;
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() { return childPatternContentActionsAvailable(); })
        .then([this]() {
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_LEFT)) { return; }
            clearFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return childPatternContentActionsAvailable() && focusedStepHasChildContent();
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_LEFT);
            clearFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return childPatternContentActionsAvailable(); })
        .then([this]() {
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) { return; }
            copyFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return childPatternContentActionsAvailable() && canPasteFocusedStepContent();
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
            pasteFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return selectionInteractionPolicy().leftTopRelease != SelectionAction::NONE &&
                   !edit_workflow_.trackPasteNavigationBlocked();
        })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            (void)navigation_workflow_.backSelectionMode();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this]() {
            if (edit_workflow_.selectionHoldActionAvailable()) {
                edit_workflow_.beginSelectionHoldAction(
                    core::state::StructureHoldAction::REMOVE
                );
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            if (edit_workflow_.selectionTrackRemoveHoldPending()) return true;
            return navigation_workflow_.selectionActive() &&
                   !edit_workflow_.currentTrackRemoveHoldPending() &&
                   edit_workflow_.selectionHoldActionAvailable();
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_LEFT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = true;
#endif
            if (edit_workflow_.selectionTrackRemoveHoldPending()) {
                edit_workflow_.applyLatchedTrackSelectionLongPress();
                return;
            }
            if (track_ui_.selection.active.get()) {
                edit_workflow_.clearHoldAction();
            }
            edit_workflow_.applySelectionBottomLeftHold();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return currentStructureBottomActionsAvailable() &&
                   !navigation_workflow_.selectionActive();
        })
        .then([this]() {
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
            if (edit_workflow_.canRemoveCurrentStructure()) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::REMOVE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return bottom_action_release_latch_.isArmed(
                       Config::ButtonID::BOTTOM_LEFT
                   ) ||
                   edit_workflow_.selectionTrackRemoveHoldPending() ||
                   (navigation_workflow_.selectionActive() &&
                    !edit_workflow_.currentTrackRemoveHoldPending());
        })
        .then([this]() {
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_LEFT)) {
                edit_workflow_.settleConsumedBottomLeftRelease();
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
                return;
            }
            if (edit_workflow_.selectionTrackRemoveHoldPending()) {
                edit_workflow_.applyLatchedTrackSelectionShortPress();
                return;
            }
            if (track_ui_.selection.active.get()) {
                edit_workflow_.clearHoldAction();
                if (!publishPatternHistoryBarrier(sequencer_,
                                                  commitPatternHistoryBarrier(history_))) {
                    return;
                }
            }
            edit_workflow_.applySelectionBottomLeftTap();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            if (bottom_action_release_latch_.isArmed(
                    Config::ButtonID::BOTTOM_LEFT
                )) {
                return true;
            }
            const bool selectionActive =
                navigation_workflow_.selectionActive();
            if (edit_workflow_.currentTrackRemoveHoldPending()) return true;
            if (edit_workflow_.trackRemoveHoldPending()) {
                return !selectionActive;
            }
            return !selectionActive &&
                   currentStructureBottomActionsAvailable();
        })
        .then([this]() {
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_LEFT)) {
                edit_workflow_.settleConsumedBottomLeftRelease();
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
                return;
            }
            if (edit_workflow_.currentTrackRemoveHoldPending()) {
                edit_workflow_.applyLatchedCurrentTrackShortPress();
                return;
            }
            if (edit_workflow_.trackRemoveHoldPending()) {
                edit_workflow_.clearHoldAction();
                return;
            }
            // A physical release always terminates the STEP/PAGE hold, even
            // when the short action is a no-op (for example an empty step).
            edit_workflow_.clearHoldAction();
            if (trackFocusActive()) {
                if (!publishPatternHistoryBarrier(sequencer_,
                                                  commitPatternHistoryBarrier(history_))) {
                    return;
                }
            }
            edit_workflow_.applyCurrentStructureShortPress();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            const bool selectionActive =
                navigation_workflow_.selectionActive();
            if (edit_workflow_.currentTrackRemoveHoldPending()) return true;
            if (edit_workflow_.trackRemoveHoldPending()) {
                return !selectionActive;
            }
            return !selectionActive &&
                   currentStructureBottomActionsAvailable() &&
                   edit_workflow_.canRemoveCurrentStructure();
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_LEFT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = true;
#endif
            edit_workflow_.applyCurrentStructureLongPress();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.selectionActive() &&
                   !sequencer_.structureUi.stepSelection.active.get();
        })
        .then([this]() {
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) { ux_trace_state_->ignoreNextBottomRightRelease = false; }
#endif
            const bool placing = track_ui_.selection.placementActive() ||
                                 sequencer_.structureUi.pageSelection.placementActive();
            if (placing && selectionInteractionPolicy().bottomRightLongPress ==
                               SelectionAction::PASTE_SELECTION) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() { return sequencer_.structureUi.stepSelection.active.get(); })
        .then([this]() {
            if (selectionInteractionPolicy().bottomRightLongPress ==
                SelectionAction::PASTE_SELECTION) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
            edit_workflow_.beginStepPastePreview();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() { return currentStructureBottomActionsAvailable(); })
        .then([this]() {
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
            if (edit_workflow_.canPasteCurrentStructure()) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.selectionActive() &&
                   !sequencer_.structureUi.stepSelection.active.get();
        })
        .then([this]() {
            const bool trackSelection = track_ui_.selection.active.get();
            const bool placing = trackSelection
                                     ? track_ui_.selection.placementActive()
                                     : sequencer_.structureUi.pageSelection.placementActive();
            if (trackSelection && placing) {
                (void)edit_workflow_.releaseTrackPasteAction(core::time_compat::millis());
                return;
            }

            edit_workflow_.clearHoldAction();
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) {
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) { ux_trace_state_->ignoreNextBottomRightRelease = false; }
#endif
                return;
            }
            if (selectionInteractionPolicy().bottomRightRelease ==
                SelectionAction::COPY_SELECTION) {
                edit_workflow_.copyStructureSelection();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return sequencer_.structureUi.stepSelection.active.get(); })
        .then([this]() {
            edit_workflow_.clearStepPastePreview();
            edit_workflow_.clearHoldAction();
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) {
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
                return;
            }
            if (selectionInteractionPolicy().bottomRightRelease ==
                SelectionAction::COPY_SELECTION) {
                edit_workflow_.copyStepSelection();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return currentStructureBottomActionsAvailable(); })
        .then([this]() {
            if (trackFocusActive()) {
                const auto release =
                    edit_workflow_.releaseTrackPasteAction(core::time_compat::millis());
                if (release == core::state::contextual::GuardedActionRelease::TAP) {
                    edit_workflow_.copyCurrentStructure();
                }
                return;
            }
            edit_workflow_.clearHoldAction();
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) {
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
                return;
            }
            edit_workflow_.copyCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return sequencer_.structureUi.pageSelection.active.get() &&
                   selectionInteractionPolicy().bottomRightLongPress ==
                       SelectionAction::PASTE_SELECTION;
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) { ux_trace_state_->ignoreNextBottomRightRelease = true; }
#endif
            edit_workflow_.pasteStructureSelection();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return sequencer_.structureUi.stepSelection.active.get() &&
                   selectionInteractionPolicy().bottomRightLongPress ==
                       SelectionAction::PASTE_SELECTION;
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = true;
#endif
            edit_workflow_.pasteStepSelection();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return currentStructureBottomActionsAvailable() && !trackFocusActive(); })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = true;
#endif
            edit_workflow_.pasteCurrentStructure();
        });
}

FLASHMEM bool SequencerStepHandler::selectionHasItems() const {
    return navigation_workflow_.selectedItemsAvailable();
}

FLASHMEM core::state::StructureSelectionInteractionPolicy
SequencerStepHandler::selectionInteractionPolicy() const {
    const bool stepSelection = sequencer_.structureUi.stepSelection.active.get();
    const bool placement = stepSelection ? sequencer_.structureUi.stepSelection.placementActive()
                           : track_ui_.selection.active.get()
                               ? track_ui_.selection.placementActive()
                               : sequencer_.structureUi.pageSelection.placementActive();
    const bool pasteAvailable = stepSelection ? edit_workflow_.canPasteStepSelection()
                                              : edit_workflow_.canPasteStructureSelection();

    return core::state::buildStructureSelectionInteractionPolicy({
        .entryAvailable = false,
        .active = navigation_workflow_.selectionActive(),
        .placing = placement,
        .selectedItemsAvailable = selectionHasItems(),
        .pasteAvailable = pasteAvailable,
    });
}

FLASHMEM void SequencerStepHandler::applyStepContentDraft() {
    if (!publishPatternHistoryBarrier(sequencer_, commitPatternHistoryBarrier(history_))) return;
    (void)sequencer::step_content_draft_workflow::apply(sequencer_, tracks_, history_);
}

FLASHMEM void SequencerStepHandler::backFromStepContentDraft() {
    using Result = sequencer::step_content_draft_workflow::BackResult;
    if (!publishPatternHistoryBarrier(sequencer_, commitPatternHistoryBarrier(history_))) return;
    const auto result = sequencer::step_content_draft_workflow::requestBack(sequencer_);
    if (result == Result::DISCARDED || result == Result::SAVED) {
        (void)core::state::sequencer::leaveContentView(sequencer_);
    }
}

FLASHMEM void SequencerStepHandler::moveStepContentDraftExitChoice(float delta) {
    sequencer::step_content_draft_workflow::moveExitChoice(sequencer_, delta);
}

FLASHMEM void SequencerStepHandler::confirmStepContentDraftExitChoice() {
    using Result = sequencer::step_content_draft_workflow::BackResult;
    const auto result =
        sequencer::step_content_draft_workflow::applyExitChoice(sequencer_, tracks_, history_);
    if (result == Result::DISCARDED || result == Result::SAVED) {
        (void)core::state::sequencer::leaveContentView(sequencer_);
    }
}

FLASHMEM void SequencerStepHandler::continueStepContentDraft() {
    sequencer_.stepContentDraft.exitChoice.set(
        core::state::sequencer::SequencerStepContentDraftExitChoice::CONTINUE);
    confirmStepContentDraftExitChoice();
}

FLASHMEM bool SequencerStepHandler::childPatternContentActionsAvailable() const {
    return core::state::sequencer::isChildContentView(sequencer_) &&
           !sequencer_.stepContentDraft.active.get() && navigation_workflow_.allowsMainBindings() &&
           !navigation_workflow_.stepFocusActive();
}

FLASHMEM bool SequencerStepHandler::currentStructureBottomActionsAvailable() const {
    // A child-content draft owns the bottom strip exclusively: only Apply is
    // admissible until the unpublished authoring session is resolved. Do not
    // let hidden structure bindings mutate, copy, or paste behind it.
    if (sequencer_.stepContentDraft.active.get()) return false;
    if (!navigation_workflow_.allowsMainBindings()) return false;
    if (core::state::sequencer::isRootContentView(sequencer_)) return true;
    return core::state::sequencer::isChildContentView(sequencer_) &&
           navigation_workflow_.stepFocusActive();
}

FLASHMEM void SequencerStepHandler::toggleStep(uint8_t indexInPage) {
    uint8_t abs = 0;
    if (!seq::resolveActiveContentStepInPage(sequencer_, sequencer_.page.get(), indexInPage, abs)) {
        return;
    }

    const bool rootContext = seq::isRootContentView(sequencer_);

    // Micro/Cycle drafts publish one prepared entry when the draft is applied.
    // Their scratch-only toggles must remain allocation-free and must not open
    // an independent Pattern transaction.
    if (!rootContext && sequencer_.stepContentDraft.pattern() != nullptr) {
        sequencer_.focusedStep.set(abs);
        (void)seq::toggleActiveContentStep(sequencer_, abs);
        return;
    }

    const bool beforeEnabled = seq::activeContentStepEnabled(sequencer_, abs);
    auto descriptor = stepToggleDescriptor(abs, beforeEnabled, !beforeEnabled);
    const auto payloadPlan = rootContext
                                 ? seq::SequencerCoalescedPatternPayloadPlan::FlatOnly
                                 : seq::SequencerCoalescedPatternPayloadPlan::FullCurrentPayload;
    const auto begin =
        history_.beginPreparedPatternEdit(kStepToggleOwner, abs, payloadPlan, descriptor);
    if (!seq::sequencerHistoryOpenAccepted(begin)) {
        sequencer_.historyFeedback.showRejection(begin, core::time_compat::millis());
        return;
    }

    sequencer_.focusedStep.set(abs);
    const bool changed = seq::toggleActiveContentStep(sequencer_, abs);
    descriptor =
        stepToggleDescriptor(abs, beforeEnabled, seq::activeContentStepEnabled(sequencer_, abs));

    const auto seal = history_.sealPreparedPatternEdit(kStepToggleOwner, abs, changed, descriptor);
    if (seq::sequencerPreparedPatternEditSealFailed(seal)) {
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable, core::time_compat::millis());
        return;
    }
    if (seal != seq::SequencerPreparedPatternEditSealOutcome::Sealed) return;

    const auto commit = history_.commitPreparedPatternEdit(kStepToggleOwner);
    if (commit != seq::SequencerPreparedPatternEditCommitOutcome::Committed) {
        sequencer_.historyFeedback.showRejection(
            seq::SequencerHistoryRejectionReason::HistoryUnavailable, core::time_compat::millis());
        return; }
}

}  // namespace core::handler

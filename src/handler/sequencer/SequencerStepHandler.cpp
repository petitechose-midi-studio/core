#include "SequencerStepHandler.hpp"

#include <config/App.hpp>
#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include <utility>

#include "state/sequencer/SequencerContentViewOps.hpp"

#if defined(MS_UX_RECORDER)
#include "validation/ux/SemanticUxTraceState.hpp"
#endif

namespace core::handler {

FLASHMEM SequencerStepHandler::SequencerStepHandler(StateRefs state,
                                                    oc::api::EncoderAPI& encoders,
                                                    oc::api::ButtonAPI& buttons,
                                                    oc::type::ScopeID scopeId
#if defined(MS_UX_RECORDER)
                                           ,
                                           core::validation::ux::StructureUxTraceState* uxTraceState
#endif
)
    : sequencer_(state.sequencer)
    , structure_clipboard_(state.structureClipboard)
    , navigation_workflow_(
          SequencerStructureNavigationWorkflow::StateRefs{
              state.sequencer,
              state.tracks,
              state.navigationFocus,
              state.trackNavigation,
              state.sharedTracks,
              state.history,
          }
      )
    , edit_workflow_(
          SequencerStructureEditWorkflow::StateRefs{
              state.sequencer,
              state.tracks,
              state.navigationFocus,
              state.trackNavigation,
              state.projectNavigation,
              state.structureClipboard,
              state.sharedTracks,
              state.history,
          }
    )
    , history_(state.history)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId)
#if defined(MS_UX_RECORDER)
    , ux_trace_state_(uxTraceState)
#endif
{
    setupBindings();
}

FLASHMEM void SequencerStepHandler::setupBindings() {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        buttons_.button(Config::MACRO_BUTTONS[i])
            .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
            .scope(scope_id_)
            .when([this]() { return sequencer_.structureUi.stepSelection.active.get(); })
            .then([this, i]() {
                step_selection_macro_release_latch_.arm(Config::MACRO_BUTTONS[i]);
            });

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
        .when([this]() {
            return core::state::sequencer::isChildContentView(sequencer_) &&
                   navigation_workflow_.allowsMainBindings() &&
                   !navigation_workflow_.stepFocusActive();
        })
        .then([this](float delta) {
            if (delta == 0.0f) return;
            const uint8_t pages = core::state::sequencer::activeContentPageCount(sequencer_);
            if (pages <= 1U) return;
            const int direction = delta > 0.0f ? 1 : -1;
            const int next = static_cast<int>(sequencer_.page.get()) + direction;
            sequencer_.page.set(core::state::sequencer::normalizeActiveContentPage(
                sequencer_,
                static_cast<uint8_t>((next + pages) % pages)
            ));
            sequencer_.focusedStep.set(
                core::state::sequencer::activeContentPageStartStep(sequencer_, sequencer_.page.get())
            );
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.selectionActive();
        })
        .then([this](float delta) { navigation_workflow_.navigateSelection(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isChildContentView(sequencer_) &&
                   navigation_workflow_.allowsMainBindings() &&
                   navigation_workflow_.stepFocusActive();
        })
        .then([this](float delta) {
            history_.commitCoalescedPatternEdit();
            navigation_workflow_.moveByFocus(delta);
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isRootContentView(sequencer_) &&
                   navigation_workflow_.allowsMainBindings();
        })
        .then([this](float delta) {
            history_.commitCoalescedPatternEdit();
            navigation_workflow_.moveByFocus(delta);
        });

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.allowsMainBindings(); })
        .then([this]() {
            history_.commitCoalescedPatternEdit();
            nav_release_latch_.arm(Config::ButtonID::NAV);
            navigation_workflow_.enterSelectionModeForCurrentFocus();
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this]() {
            if (nav_release_latch_.consume(Config::ButtonID::NAV)) {
                return;
            }
            navigation_workflow_.toggleSelectionAtCursor();
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isRootContentView(sequencer_) &&
                   navigation_workflow_.allowsMainBindings();
        })
        .then([this]() {
            if (nav_release_latch_.consume(Config::ButtonID::NAV)) {
                return;
            }
            if (navigation_workflow_.previewingAddSlot()) {
                history_.commitCoalescedPatternEdit();
                navigation_workflow_.createPreviewedStructure();
                return;
            }
            navigation_workflow_.cycleNavigationFocus();
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isChildContentView(sequencer_) &&
                   navigation_workflow_.allowsMainBindings();
        })
        .then([this]() {
            if (nav_release_latch_.consume(Config::ButtonID::NAV)) {
                return;
            }
            navigation_workflow_.cycleNavigationFocus();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() {
            return core::state::sequencer::isChildContentView(sequencer_) &&
                   navigation_workflow_.allowsMainBindings();
        })
        .then([this]() {
            history_.commitCoalescedPatternEdit();
            core::state::sequencer::leaveContentView(sequencer_);
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() { return childPatternContentActionsAvailable(); })
        .then([this]() {
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_LEFT)) {
                return;
            }
            clearFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return childPatternContentActionsAvailable() &&
                   focusedStepHasChildContent();
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
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) {
                return;
            }
            copyFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return childPatternContentActionsAvailable() &&
                   canPasteFocusedStepContent();
        })
        .then([this]() {
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
            pasteFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this]() { navigation_workflow_.cancelSelectionMode(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this]() {
            if (selectionHasItems()) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::REMOVE);
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.selectionActive() && selectionHasItems();
        })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_LEFT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = true;
#endif
            history_.commitCoalescedPatternEdit();
            if (sequencer_.structureUi.stepSelection.active.get()) {
                edit_workflow_.resetStepSelectionDeep();
            } else {
                edit_workflow_.deleteSelection();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(scope_id_)
        .when([this]() { return currentStructureBottomActionsAvailable(); })
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
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_LEFT)) {
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
                return;
            }
            if (sequencer_.structureUi.stepSelection.active.get()) {
                history_.commitCoalescedPatternEdit();
                edit_workflow_.resetStepSelectionShallow();
            } else if (!sequencer_.structureUi.pageSelection.active.get()) {
                history_.commitCoalescedPatternEdit();
                edit_workflow_.toggleTrackSelectionMute();
            } else if (sequencer_.structureUi.pageSelection.active.get()) {
                history_.commitCoalescedPatternEdit();
                edit_workflow_.clearSelection();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(scope_id_)
        .when([this]() { return currentStructureBottomActionsAvailable(); })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_LEFT)) {
#if defined(MS_UX_RECORDER)
                if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = false;
#endif
                return;
            }
            history_.commitCoalescedPatternEdit();
            edit_workflow_.applyBottomLeftTapCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return currentStructureBottomActionsAvailable() &&
                   edit_workflow_.canRemoveCurrentStructure();
        })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_LEFT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomLeftRelease = true;
#endif
            history_.commitCoalescedPatternEdit();
            edit_workflow_.removeCurrentStructure();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() { return sequencer_.structureUi.stepSelection.active.get(); })
        .then([this]() {
            const bool canPaste =
                structure_clipboard_.hasSequencerSteps() &&
                structure_clipboard_.sequencerSteps.rootContext ==
                    core::state::sequencer::isRootContentView(sequencer_);
            if (canPaste) {
                edit_workflow_.beginHoldAction(core::state::StructureHoldAction::PASTE);
            }
            edit_workflow_.beginStepPastePreview();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.selectionActive() &&
                   !sequencer_.structureUi.stepSelection.active.get() &&
                   edit_workflow_.canPasteSelection();
        })
        .then([this]() {
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
            edit_workflow_.beginHoldAction(core::state::StructureHoldAction::PASTE);
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
        .when([this]() { return navigation_workflow_.selectionActive(); })
        .then([this]() {
            if (sequencer_.structureUi.stepSelection.active.get()) {
                edit_workflow_.clearStepPastePreview();
                edit_workflow_.clearHoldAction();
                if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) {
#if defined(MS_UX_RECORDER)
                    if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
                    return;
                }
                edit_workflow_.copyStepSelection();
            } else {
                edit_workflow_.clearHoldAction();
                if (bottom_action_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) {
#if defined(MS_UX_RECORDER)
                    if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = false;
#endif
                    return;
                }
                edit_workflow_.copySelection();
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(scope_id_)
        .when([this]() { return currentStructureBottomActionsAvailable(); })
        .then([this]() {
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
        .when([this]() { return sequencer_.structureUi.stepSelection.active.get(); })
        .then([this]() {
            edit_workflow_.clearStepPastePreview();
            edit_workflow_.clearHoldAction();
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = true;
#endif
            history_.commitCoalescedPatternEdit();
            edit_workflow_.pasteStepSelection();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() {
            return navigation_workflow_.selectionActive() &&
                   !sequencer_.structureUi.stepSelection.active.get() &&
                   edit_workflow_.canPasteSelection();
        })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = true;
#endif
            history_.commitCoalescedPatternEdit();
            edit_workflow_.pasteSelection();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(scope_id_)
        .when([this]() { return currentStructureBottomActionsAvailable(); })
        .then([this]() {
            edit_workflow_.clearHoldAction();
            bottom_action_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
#if defined(MS_UX_RECORDER)
            if (ux_trace_state_) ux_trace_state_->ignoreNextBottomRightRelease = true;
#endif
            history_.commitCoalescedPatternEdit();
            edit_workflow_.pasteCurrentStructure();
        });
}

FLASHMEM bool SequencerStepHandler::selectionHasItems() const {
    return navigation_workflow_.selectedItemsAvailable();
}

FLASHMEM bool SequencerStepHandler::childPatternContentActionsAvailable() const {
    return core::state::sequencer::isChildContentView(sequencer_) &&
           navigation_workflow_.allowsMainBindings() &&
           !navigation_workflow_.stepFocusActive();
}

FLASHMEM bool SequencerStepHandler::currentStructureBottomActionsAvailable() const {
    if (!navigation_workflow_.allowsMainBindings()) return false;
    if (core::state::sequencer::isRootContentView(sequencer_)) return true;
    return core::state::sequencer::isChildContentView(sequencer_) &&
           navigation_workflow_.stepFocusActive();
}

FLASHMEM void SequencerStepHandler::toggleStep(uint8_t indexInPage) {
    history_.commitCoalescedPatternEdit();

    uint8_t abs = 0;
    if (!core::state::sequencer::resolveActiveContentStepInPage(
            sequencer_,
            sequencer_.page.get(),
            indexInPage,
            abs
        )) {
        return;
    }

    const bool rootContext = core::state::sequencer::isRootContentView(sequencer_);
    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    bool beforeCaptured = true;
    if (rootContext) {
        core::state::sequencer::captureFlatHistorySnapshot(sequencer_, before);
    } else {
        beforeCaptured = core::state::sequencer::captureHistorySnapshot(sequencer_, before);
    }

    sequencer_.focusedStep.set(abs);
    core::state::sequencer::toggleActiveContentStep(sequencer_, abs);

    if (!beforeCaptured) return;

    core::state::sequencer::SequencerHistoryPatternSnapshot after;
    bool afterCaptured = true;
    if (rootContext) {
        core::state::sequencer::captureFlatHistorySnapshot(sequencer_, after);
    } else {
        afterCaptured = core::state::sequencer::captureHistorySnapshot(sequencer_, after);
    }
    if (afterCaptured) {
        const bool beforeEnabled = rootContext ? before.flat.enabledMask.test(abs) : false;
        const bool afterEnabled = rootContext ? after.flat.enabledMask.test(abs) : false;
        const auto descriptor = core::state::sequencer::SequencerHistoryDescriptor{
            .kind = core::state::sequencer::SequencerHistoryActionKind::StepToggle,
            .stepIndex = abs,
            .property = core::state::sequencer::StepProperty::NOTE,
            .hasValue = rootContext,
            .beforeValue = beforeEnabled ? 1 : 0,
            .afterValue = afterEnabled ? 1 : 0,
        };
        if (rootContext) {
            history_.recordFlatPattern(std::move(before), std::move(after), descriptor);
        } else {
            history_.recordPattern(std::move(before), std::move(after), descriptor);
        }
    }
}

FLASHMEM bool SequencerStepHandler::focusedStepHasChildContent() const {
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(
        sequencer_,
        sequencer_.focusedStep.get()
    );
    return core::state::sequencer::stepNodeHasAnyChildContent(sequencer_.pattern, nodeId);
}

FLASHMEM bool SequencerStepHandler::canPasteFocusedStepContent() const {
    return structure_clipboard_.hasSequencerStepContent(
               core::state::SequencerStepContentClipboardKind::ALL
           ) &&
           core::state::sequencer::activeContentStepCanReceiveChildContent(
               sequencer_,
               sequencer_.focusedStep.get()
           );
}

FLASHMEM void SequencerStepHandler::recordFocusedContentEdit(
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    bool beforeCaptured
) {
    if (!beforeCaptured) return;

    core::state::sequencer::SequencerHistoryPatternSnapshot after;
    if (!core::state::sequencer::captureHistorySnapshot(sequencer_, after)) return;
    if (core::state::sequencer::sameMusicalHistorySnapshot(before, after)) return;

    history_.recordPattern(
        std::move(before),
        std::move(after),
        core::state::sequencer::SequencerHistoryDescriptor{
            .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
            .stepIndex = sequencer_.focusedStep.get(),
            .property = core::state::sequencer::StepProperty::NOTE,
            .hasValue = false,
        }
    );
}

FLASHMEM void SequencerStepHandler::clearFocusedStepContent() {
    if (!focusedStepHasChildContent()) return;
    history_.commitCoalescedPatternEdit();

    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    const bool beforeCaptured = core::state::sequencer::captureHistorySnapshot(sequencer_, before);

    if (!core::state::sequencer::clearActiveContentChildren(
            sequencer_,
            sequencer_.focusedStep.get()
        )) {
        return;
    }
    recordFocusedContentEdit(std::move(before), beforeCaptured);
}

FLASHMEM void SequencerStepHandler::copyFocusedStepContent() {
    if (!focusedStepHasChildContent()) return;
    (void)core::state::sequencer::copyActiveContentChildrenToClipboard(
        sequencer_,
        sequencer_.focusedStep.get(),
        structure_clipboard_
    );
}

FLASHMEM void SequencerStepHandler::pasteFocusedStepContent() {
    if (!canPasteFocusedStepContent()) return;
    history_.commitCoalescedPatternEdit();

    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    const bool beforeCaptured = core::state::sequencer::captureHistorySnapshot(sequencer_, before);

    if (!core::state::sequencer::pasteActiveContentChildrenFromClipboard(
            sequencer_,
            sequencer_.focusedStep.get(),
            structure_clipboard_
        )) {
        return;
    }
    recordFocusedContentEdit(std::move(before), beforeCaptured);
}

}  // namespace core::handler

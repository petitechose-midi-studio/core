#include "SequencerStepEditHandler.hpp"

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

#include "config/Timing.hpp"
#include "SequencerInteractionPolicyAdapter.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::handler {
namespace interaction_policy = core::handler::sequencer::interaction_policy;

FLASHMEM void SequencerStepEditHandler::setupBindings() {
    // ===== SEQUENCER VIEW SCOPE =====
    // MACRO_i long press: open STEP EDIT for step i in the current page.
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        auto btn = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);
        buttons_.button(btn)
            .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
            .scope(sequencer_view_scope_)
            .when([this]() {
                const auto policy = interaction_policy::build(
                    sequencer_,
                    track_ui_,
                    navigation_focus_.get(),
                    overlay_state_.hasVisible()
                );
                return interaction_policy::canOpenStepEditor(policy);
            })
            .then([this, i]() { openForMacroInPage(i); });
    }

    // ===== OVERLAY SCOPE =====
    // LEFT_CENTER + NAV retargets the root editor across the real Pattern
    // length; ordinary NAV keeps row navigation.
    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(overlay_scope_)
        .when([this]() {
            return step_retarget_active_ &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get();
        })
        .then([this](float delta) { retargetEditedStep(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(overlay_scope_)
        .when([this]() {
            return !step_retarget_active_ ||
                   sequencer_.stepContentDraft.exitPromptVisible.get();
        })
        .then([this](float delta) { moveFocus(delta); });

    // OPT encoder: edit focused value
    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(overlay_scope_)
        .then([this](float value) { setFocusedValue(value); });

    // Pressing the currently edited step closes; value edits are already live.
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        auto btn = static_cast<oc::type::ButtonID>(Config::MACRO_BUTTONS[i]);
        buttons_.button(btn)
            .release()
            .scope(overlay_scope_)
            .then([this, i]() { maybeCloseFromMacro(i); });
    }

    // Close. Property edits are applied immediately while turning OPT.
    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(overlay_scope_)
        .then([this]() { activateFocusedRowOrClose(); });

    // Close without reverting live edits.
    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(overlay_scope_)
        .then([this]() { backFromStepEdit(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .press()
        .scope(overlay_scope_)
        .when([this]() {
            return core::state::sequencer::isRootContentView(sequencer_) &&
                   !chordEditorActive() &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get();
        })
        .then([this]() { step_retarget_active_ = true; });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(overlay_scope_)
        // LEFT_CENTER owns only root-Step retargeting. In child, Chord and
        // modal contexts it is deliberately a no-op; LEFT_TOP is the single
        // Back gesture and therefore the only path that may open a safe-exit
        // prompt.
        .then([this]() { step_retarget_active_ = false; });

    buttons_.button(Config::ButtonID::BOTTOM_CENTER)
        .release()
        .scope(overlay_scope_)
        .when([this]() {
            return !sequencer_.stepContentDraft.active.get() &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get();
        })
        .then([this]() { openStepPresetPicker(); });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .scope(overlay_scope_)
        .when([this]() {
            return !sequencer_.stepContentDraft.exitPromptVisible.get() &&
                   focusedRowSupportsLocalVariation();
        })
        .then([this]() {
            sequencer_.stepEdit.localVariationEditActive.set(true);
            configureOptForFocusedRow();
        });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .release()
        .scope(overlay_scope_)
        .when([this]() {
            return !sequencer_.stepContentDraft.exitPromptVisible.get() &&
                   sequencer_.stepEdit.localVariationEditActive.get();
        })
        .then([this]() {
            sequencer_.stepEdit.localVariationEditActive.set(false);
            configureOptForFocusedRow();
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(overlay_scope_)
        .when([this]() {
            return !sequencer_.stepContentDraft.exitPromptVisible.get() &&
                   focusedRowIsValueRow();
        })
        .then([this]() { resetFocusedValueRowToDefault(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .press()
        .scope(overlay_scope_)
        .when([this]() {
            return !sequencer_.stepContentDraft.active.get() &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get() &&
                   focusedRowIsContextRow();
        })
        .then([this]() {
            if (focusedContextHasChild()) {
                sequencer_.stepEdit.contextHold.begin(
                    core::state::StructureHoldAction::REMOVE,
                    oc::time::millis()
                );
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(overlay_scope_)
        .when([this]() {
            return !sequencer_.stepContentDraft.active.get() &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get() &&
                   focusedRowIsContextRow();
        })
        .then([this]() {
            sequencer_.stepEdit.contextHold.clear();
            if (context_release_latch_.consume(Config::ButtonID::BOTTOM_LEFT)) {
                return;
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(overlay_scope_)
        .when([this]() {
            return !sequencer_.stepContentDraft.active.get() &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get() &&
                   focusedRowIsContextRow() && focusedContextHasChild();
        })
        .then([this]() {
            sequencer_.stepEdit.contextHold.clear();
            context_release_latch_.arm(Config::ButtonID::BOTTOM_LEFT);
            clearFocusedContextChild();
        });

    // Any unpublished Step-content draft exposes one positive action: Apply.
    // This also covers the Step Editor opened inside a new Micro/Cycle draft.
    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(overlay_scope_)
        .when([this]() {
            return sequencer_.stepContentDraft.active.get() &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get();
        })
        .then([this]() { applyStepContentDraft(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(overlay_scope_)
        .when([this]() {
            return !sequencer_.stepContentDraft.active.get() &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get() &&
                   focusedRowIsContextRow();
        })
        .then([this]() {
            if (canPasteFocusedStepContent()) {
                sequencer_.stepEdit.contextHold.begin(
                    core::state::StructureHoldAction::PASTE,
                    oc::time::millis()
                );
            }
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(overlay_scope_)
        .when([this]() {
            return !sequencer_.stepContentDraft.active.get() &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get() &&
                   focusedRowIsContextRow();
        })
        .then([this]() {
            sequencer_.stepEdit.contextHold.clear();
            if (context_release_latch_.consume(Config::ButtonID::BOTTOM_RIGHT)) {
                return;
            }
            copyFocusedStepContent();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(overlay_scope_)
        .when([this]() {
            return !sequencer_.stepContentDraft.active.get() &&
                   !sequencer_.stepContentDraft.exitPromptVisible.get() &&
                   focusedRowIsContextRow() && canPasteFocusedStepContent();
        })
        .then([this]() {
            sequencer_.stepEdit.contextHold.clear();
            context_release_latch_.arm(Config::ButtonID::BOTTOM_RIGHT);
            pasteFocusedStepContent();
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(step_preset_overlay_scope_)
        .then([this](float delta) { moveStepPresetItem(delta); });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(step_preset_overlay_scope_)
        .then([this](float delta) { moveStepPresetPreviewState(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { toggleStepPresetDetail(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { closeStepPresetPicker(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { closeStepPresetPicker(); });

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { closeStepPresetPicker(); });

    buttons_.button(Config::ButtonID::BOTTOM_CENTER)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { toggleStepPresetMode(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(step_preset_overlay_scope_)
        .then([this]() { beginStepPresetActionGuard(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(step_preset_overlay_scope_)
        .then([this]() { releaseStepPresetAction(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(step_preset_overlay_scope_)
        .then([this]() { commitStepPresetActionGuard(); });
}

}  // namespace core::handler

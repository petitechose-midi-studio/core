#include "SequencerStepEditHandler.hpp"

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

#include "config/Timing.hpp"
#include "SequencerInteractionPolicyAdapter.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerPresetLibraryEntryPolicy.hpp"

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

    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(overlay_scope_)
        .when([this]() {
            return core::state::sequencer::preset_library_entry_policy::
                canOpenStepPresets(sequencer_);
        })
        .then([this]() {
            preset_open_release_latch_.arm(Config::ButtonID::NAV);
            openStepPresetLibrary();
        });

    // Chord Presets belong to the active Chord draft. Formula and Source keep
    // their own NAV grammar; only the six-field Chord surface opens the
    // library.
    buttons_.button(Config::ButtonID::NAV)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(overlay_scope_)
        .when([this]() {
            return core::state::sequencer::preset_library_entry_policy::
                canOpenChordPresets(sequencer_);
        })
        .then([this]() {
            if (chord_presets_.captureTarget().valid) {
                preset_open_release_latch_.arm(Config::ButtonID::NAV);
                openChordPresetLibrary();
            }
        });

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
        // In Chord detail LEFT_CENTER owns the non-destructive Source sheet.
        // Elsewhere it only releases root-Step retargeting. BOTTOM_CENTER
        // remains globally reserved for Transport.
        .then([this]() {
            if (chordEditorActive()) {
                toggleChordSourceSelector();
                return;
            }
            step_retarget_active_ = false;
        });

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
        .scope(preset_library_overlay_scope_)
        .then([this](float delta) {
            movePresetLibraryItem(delta);
        });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(preset_library_overlay_scope_)
        .then([this](float delta) {
            // The active adapter decides whether the focused detail row is
            // adjustable. Chord details deliberately consume OPT without
            // leaking into the invoking editor.
            adjustPresetLibraryDetail(delta);
        });

    buttons_.button(Config::ButtonID::NAV)
        .press()
        .scope(preset_library_overlay_scope_)
        .then([this]() {
            // Some input backends quarantine the opener's physical release
            // during the scope transition. In that case the next deliberate
            // press proves the old gesture ended and clears the stale latch
            // before its matching release is handled.
            (void)preset_open_release_latch_.consume(Config::ButtonID::NAV);
        });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(preset_library_overlay_scope_)
        .then([this]() {
            if (preset_open_release_latch_.consume(Config::ButtonID::NAV)) {
                return;
            }
            enterPresetLibraryDetail();
        });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(preset_library_overlay_scope_)
        .then([this]() { backFromPresetLibrary(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(preset_library_overlay_scope_)
        .then([]() {});

    buttons_.button(Config::ButtonID::BOTTOM_LEFT)
        .release()
        .scope(preset_library_overlay_scope_)
        .then([this]() { togglePresetLibraryMode(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(preset_library_overlay_scope_)
        .then([this]() {
            beginPresetLibraryActionGuard();
        });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(preset_library_overlay_scope_)
        .then([this]() { releasePresetLibraryAction(); });

    buttons_.button(Config::ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(preset_library_overlay_scope_)
        .then([this]() {
            commitPresetLibraryActionGuard();
        });
}

}  // namespace core::handler

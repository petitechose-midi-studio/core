#pragma once

/**
 * @file SequencerStepEditHandler.hpp
 * @brief Input bindings for the sequencer STEP EDIT overlay
 */

#include <cstdint>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "handler/common/ButtonReleaseLatch.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/StructureSelectionState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::state::sequencer {
enum class StepContentChildKind : uint8_t;
}

namespace core::handler {

class SequencerStepEditHandler {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
        core::state::StructureClipboardState& structureClipboard;
        core::state::TrackNavigationState& trackNavigation;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        SequencerHistoryDomainServices history;
    };

    SequencerStepEditHandler(
        StateRefs state,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID sequencerViewScope,
        oc::type::ScopeID overlayScope
    );

    // Non-copyable, non-movable
    SequencerStepEditHandler(const SequencerStepEditHandler&) = delete;
    SequencerStepEditHandler& operator=(const SequencerStepEditHandler&) = delete;
    SequencerStepEditHandler(SequencerStepEditHandler&&) = delete;
    SequencerStepEditHandler& operator=(SequencerStepEditHandler&&) = delete;

private:
    void setupBindings();

    void openForMacroInPage(uint8_t indexInPage);
    void closeStepEdit();

    void moveFocus(float delta);
    void activateFocusedRowOrClose();
    void setFocusedValue(float normalized);
    void configureOptForFocusedRow();
    void openChordEditor();
    void closeChordEditor();
    void moveChordEditorFocus(float delta);
    void setFocusedChordFieldValue(float normalized);
    void configureOptForFocusedChordField();
    void resetFocusedChordFieldToDefault();
    bool chordEditorActive() const;
    bool editedStepInRange(uint8_t& step) const;
    void activateFocusedContextRow(
        core::state::sequencer::StepContentChildKind childKind,
        uint8_t defaultLength
    );
    void maybeCloseFromMacro(uint8_t indexInPage);
    bool focusedRowIsValueRow() const;
    bool focusedRowIsContextRow() const;
    bool focusedRowSupportsLocalVariation() const;
    bool focusedContextHasChild() const;
    bool canPasteFocusedStepContent() const;
    void resetFocusedValueRowToDefault();
    void clearFocusedContextChild();
    void copyFocusedStepContent();
    void pasteFocusedStepContent();
    void recordContextMutation(
        core::state::sequencer::SequencerHistoryPatternSnapshot before,
        bool beforeCaptured
    );

    // Long-press opens while still pressed; ignore the release that follows.
    ButtonReleaseLatch<8> open_release_latch_;
    ButtonReleaseLatch<2> context_release_latch_;
    core::state::sequencer::SequencerHistoryPatternSnapshot history_snapshot_{};
    bool history_snapshot_valid_ = false;

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlay_state_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    core::state::StructureClipboardState& structure_clipboard_;
    core::state::TrackNavigationState& track_ui_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    SequencerHistoryDomainServices history_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID sequencer_view_scope_ = 0;
    oc::type::ScopeID overlay_scope_ = 0;
};

}  // namespace core::handler

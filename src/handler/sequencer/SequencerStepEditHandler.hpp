#pragma once

/**
 * @file SequencerStepEditHandler.hpp
 * @brief Input bindings for the sequencer STEP EDIT overlay
 */

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/StructureSelectionState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "app/OverlayTypes.hpp"

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
    void maybeCloseFromMacro(uint8_t indexInPage);
    bool focusedRowIsContextRow() const;
    bool focusedRowSupportsLocalVariation() const;
    bool focusedContextHasChild() const;
    bool canPasteFocusedStepContent() const;
    void clearFocusedContextChild();
    void copyFocusedStepContent();
    void pasteFocusedStepContent();
    void recordContextMutation(
        core::state::sequencer::SequencerHistoryPatternSnapshot before,
        bool beforeCaptured
    );

    // Long-press opens while still pressed; ignore the release that follows.
    bool ignore_open_release_ = false;
    uint8_t ignore_open_macro_index_in_page_ = 0;
    bool ignore_next_context_left_release_ = false;
    bool ignore_next_context_right_release_ = false;
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

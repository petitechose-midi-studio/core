#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

class SequencerRangeActionHandler {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
    };

    SequencerRangeActionHandler(StateRefs state,
                                oc::api::EncoderAPI& encoders,
                                oc::api::ButtonAPI& buttons,
                                oc::type::ScopeID scopeId);

    SequencerRangeActionHandler(const SequencerRangeActionHandler&) = delete;
    SequencerRangeActionHandler& operator=(const SequencerRangeActionHandler&) = delete;
    SequencerRangeActionHandler(SequencerRangeActionHandler&&) = delete;
    SequencerRangeActionHandler& operator=(SequencerRangeActionHandler&&) = delete;

private:
    using RangeSelectionKind = core::state::sequencer::RangeSelectionKind;
    using RangeSelectionPhase = core::state::sequencer::RangeSelectionPhase;

    void setupBindings();
    void clearCurrentPage();
    void openClearRange();
    void openCopyRange();
    void cancel();
    void moveCursor(float delta);
    void moveRange(float normalized);
    void commitCursor();
    void applyPaste();
    void beginRangeSelection(RangeSelectionKind kind);
    void configureOptForRangeEdit();
    void setSelectedRange(uint8_t start, uint8_t span);
    uint8_t currentRangeSpan() const;
    uint8_t maxRangeSpan() const;
    void ignoreNextBottomLeftRelease();
    void ignoreNextBottomRightRelease();
    void setCursorStep(uint8_t step);
    uint8_t currentPageStart() const;
    uint8_t currentPageEnd() const;
    uint8_t initialCursorStep() const;
    uint8_t maxCursorStep() const;
    void snapshotCurrentFocus();
    void restoreSnapshotFocus();

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    bool ignore_next_bottom_left_release_ = false;
    bool ignore_next_bottom_right_release_ = false;
};

}  // namespace core::handler

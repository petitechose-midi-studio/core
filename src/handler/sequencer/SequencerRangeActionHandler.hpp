#pragma once

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "state/CoreState.hpp"

namespace core::handler {

class SequencerRangeActionHandler {
public:
    SequencerRangeActionHandler(core::state::CoreState& state,
                                oc::api::EncoderAPI& encoders,
                                oc::api::ButtonAPI& buttons,
                                lv_obj_t* sequencerViewScope);

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

    core::state::CoreState& state_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* scope_element_ = nullptr;
    bool ignore_next_bottom_left_release_ = false;
    bool ignore_next_bottom_right_release_ = false;
};

}  // namespace core::handler

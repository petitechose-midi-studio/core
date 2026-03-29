#pragma once

#include <lvgl.h>

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

#include "state/CoreState.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::handler {

class SequencerPatternQuickControlsHandler {
public:
    SequencerPatternQuickControlsHandler(
        core::state::CoreState& state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        lv_obj_t* sequencerViewScope
    );

    SequencerPatternQuickControlsHandler(const SequencerPatternQuickControlsHandler&) = delete;
    SequencerPatternQuickControlsHandler& operator=(const SequencerPatternQuickControlsHandler&) = delete;

private:
    void setupBindings();
    void open();
    void closeApply();
    void closeCancel();
    void navigate(float delta);
    void setFocusedValue(float normalized);
    void configureOptForFocusedItem();
    void clampFocusToLength();
    int focusedItemOrderIndex() const;
    void setFocusedItemByOrderIndex(int index);
    int currentOffsetMax() const;
    float offsetToNormalized(int offsetSteps) const;
    int normalizedToOffset(float normalized) const;
    void applyOffsetFromSnapshot(int offsetSteps);

    core::state::CoreState& state_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    lv_obj_t* sequencer_view_scope_ = nullptr;
    core::state::sequencer::SequencerPatternSnapshot cancel_snapshot_{};
    core::state::sequencer::SequencerPatternSnapshot offset_snapshot_{};
};

}  // namespace core::handler

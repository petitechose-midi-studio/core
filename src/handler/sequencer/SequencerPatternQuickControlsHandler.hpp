#pragma once

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>

#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "app/OverlayTypes.hpp"

namespace core::handler {

class SequencerPatternQuickControlsHandler {
public:
    struct StateRefs {
        oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& tracks;
    };

    SequencerPatternQuickControlsHandler(
        StateRefs state,
        oc::api::EncoderAPI& encoders,
        oc::api::ButtonAPI& buttons,
        oc::type::ScopeID scopeId
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

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays_;
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& tracks_;
    oc::api::EncoderAPI& encoders_;
    oc::api::ButtonAPI& buttons_;
    oc::type::ScopeID scope_id_ = 0;
    core::state::sequencer::SequencerPatternSnapshot cancel_snapshot_{};
    core::state::sequencer::SequencerPatternSnapshot offset_snapshot_{};
};

}  // namespace core::handler

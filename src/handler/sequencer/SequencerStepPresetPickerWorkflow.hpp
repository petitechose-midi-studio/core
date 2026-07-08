#pragma once

#include <cstdint>

#include <oc/context/OverlayManager.hpp>

#include "app/OverlayTypes.hpp"
#include "handler/sequencer/SequencerStepPresetDomainServices.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

enum class SequencerStepPresetPickerOutcome : uint8_t {
    NONE = 0,
    SAVED,
    LOAD_EMPTY,
    LOAD_FAILED,
    LOADED,
};

class SequencerStepPresetPickerWorkflow {
public:
    SequencerStepPresetPickerWorkflow(
        core::state::sequencer::SequencerState& sequencer,
        SequencerStepPresetDomainServices& stepPresets,
        oc::context::OverlayManager<core::ui::OverlayType>& overlays
    );

    void open();
    void close();
    void move(float delta);
    void toggleMode();
    bool shouldCommitBeforeLoad() const;
    SequencerStepPresetPickerOutcome execute();

private:
    void refreshList();
    const char* selectedPresetId() const;
    void setFeedback(const SequencerStepPresetActionResult& result);

    core::state::sequencer::SequencerState& sequencer_;
    SequencerStepPresetDomainServices& step_presets_;
    oc::context::OverlayManager<core::ui::OverlayType>& overlays_;
};

}  // namespace core::handler

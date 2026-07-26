#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"

#include <cstdio>

#include "config/PlatformCompat.hpp"
#include "ui/font/StandaloneIcons.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {

using StripProps = core::ui::ContextActionStripProps;
using Visual = core::ui::ContextActionStripVisualState;
using Tone = core::ui::ContextActionStripTone;

FLASHMEM StepPresetPickerRenderData buildStepPresetPickerRenderData(
    const Source& source
) {
    return core::ui::sequencer::buildSequencerStepPresetPickerPresentation(
        source.sequencer
    );
}

FLASHMEM core::ui::ContextActionStripProps buildStepPresetActionStripProps(
    const Source& source
) {
    StripProps props{};
    const auto& picker = source.sequencer.stepPresetPicker;
    if (!picker.visible.get()) {
        props.visible = false;
        return props;
    }

    const auto action = core::ui::sequencer::
        buildSequencerStepPresetActionPresentation(picker);
    using ActionVisual = core::ui::sequencer::SequencerStepPresetActionVisual;
    using ActionTone = core::ui::sequencer::SequencerStepPresetActionTone;
    const auto visual = [&]() {
        switch (action.visual) {
            case ActionVisual::ACTIVE: return Visual::ACTIVE;
            case ActionVisual::PRESSED: return Visual::PRESSED;
            case ActionVisual::ARMED: return Visual::ARMED;
            case ActionVisual::CANCELLED: return Visual::CANCELLED;
            case ActionVisual::APPLIED: return Visual::APPLIED;
            case ActionVisual::DISABLED:
            default: return Visual::DISABLED;
        }
    }();
    const auto tone = [&]() {
        switch (action.tone) {
            case ActionTone::CONSTRUCTIVE: return Tone::CONSTRUCTIVE;
            case ActionTone::DESTRUCTIVE: return Tone::DESTRUCTIVE;
            case ActionTone::POSITIVE: return Tone::POSITIVE;
            case ActionTone::WARNING: return Tone::WARNING;
            case ActionTone::NEUTRAL:
            default: return Tone::NEUTRAL;
        }
    }();

    props.visible = true;
    props.slots[0] = core::ui::makeStandaloneIconStripSlot(
        ::standalone::icons::ACTION_CANCEL,
        Visual::ACTIVE,
        Tone::NEUTRAL
    );
    props.slots[1] = core::ui::makeStandaloneIconStripSlot(
        action.saveIcon
            ? ::standalone::icons::ACTION_APPLY
            : ::standalone::icons::STORAGE,
        Visual::ACTIVE,
        action.saveIcon ? Tone::CONSTRUCTIVE : Tone::POSITIVE
    );
    props.slots[1].showLabel = true;
    std::snprintf(
        props.slots[1].labelText.data(),
        props.slots[1].labelText.size(),
        "%s",
        action.saveIcon ? "Load" : "Save"
    );
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        action.statusIcon != nullptr
            ? action.statusIcon
            : (action.overwriteIcon
                ? ::standalone::icons::ACTION_OVERWRITE
                : (action.saveIcon
                    ? ::standalone::icons::STORAGE
                    : ::standalone::icons::ACTION_APPLY)),
        visual,
        tone
    );
    props.slots[2].holdActive = action.holdActive;
    props.slots[2].holdStartedAtMs = action.holdStartedAtMs;
    props.slots[2].holdDurationMs = action.holdDurationMs;
    if (action.showLabel) {
        props.slots[2].showLabel = true;
        props.slots[2].labelText = action.label;
    }
    return props;
}

}  // namespace core::context::standalone::sequencer_overlay_presenter

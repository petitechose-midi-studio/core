#include "context/standalone/SequencerOverlayPresenterFormatters.hpp"

#include <cstdio>

#include "config/PlatformCompat.hpp"
#include "ui/font/StandaloneIcons.hpp"

namespace core::context::standalone::sequencer_overlay_presenter {

using StripProps = core::ui::ContextActionStripProps;
using Visual = core::ui::ContextActionStripVisualState;
using Tone = core::ui::ContextActionStripTone;

FLASHMEM PresetLibraryRenderData buildPresetLibraryRenderData(
    const Source& source
) {
    return core::ui::sequencer::buildSequencerPresetLibraryPresentation(
        source.sequencer
    );
}

FLASHMEM core::ui::ContextActionStripProps buildPresetLibraryActionStripProps(
    const Source& source
) {
    StripProps props{};
    const auto& picker = source.sequencer.presetLibrary;
    if (!picker.visible.get()) {
        props.visible = false;
        return props;
    }

    const auto action = core::ui::sequencer::
        buildSequencerPresetLibraryActionPresentation(picker);

    props.visible = true;
    props.slots[0] = core::ui::makeStandaloneIconStripSlot(
        action.saveMode
            ? ::standalone::icons::ACTION_LOAD
            : ::standalone::icons::ACTION_SAVE,
        Visual::ACTIVE,
        action.saveMode ? Tone::CONSTRUCTIVE : Tone::POSITIVE
    );
    props.slots[1].visualState = Visual::HIDDEN;
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        action.statusIcon != nullptr
            ? action.statusIcon
            : (action.overwriteIcon
                ? ::standalone::icons::ACTION_OVERWRITE
                : (action.saveMode
                    ? ::standalone::icons::ACTION_SAVE
                    : ::standalone::icons::ACTION_LOAD)),
        action.visual,
        action.tone
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

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
        action.saveIcon
            ? ::standalone::icons::ACTION_APPLY
            : ::standalone::icons::STORAGE,
        Visual::ACTIVE,
        action.saveIcon ? Tone::CONSTRUCTIVE : Tone::POSITIVE
    );
    props.slots[0].showLabel = true;
    std::snprintf(
        props.slots[0].labelText.data(),
        props.slots[0].labelText.size(),
        "%s",
        action.saveIcon ? "Load" : "Save"
    );
    props.slots[1].visualState = Visual::HIDDEN;
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        action.statusIcon != nullptr
            ? action.statusIcon
            : (action.overwriteIcon
                ? ::standalone::icons::ACTION_OVERWRITE
                : (action.saveIcon
                    ? ::standalone::icons::STORAGE
                    : ::standalone::icons::ACTION_APPLY)),
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

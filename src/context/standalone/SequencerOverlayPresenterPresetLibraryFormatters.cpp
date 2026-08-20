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
    const bool patternLibrary = picker.libraryKind.get() ==
        core::state::sequencer::SequencerPresetLibraryKind::PATTERN;
    const bool managementPanel = patternLibrary &&
        picker.pattern().panel != core::state::sequencer::
            SequencerPatternPresetLibraryPanel::BROWSE;
    const bool userDetail = patternLibrary &&
        picker.detailVisible.get() && picker.pattern().descriptor.valid &&
        picker.pattern().descriptor.source == core::state::sequencer::
            SequencerPatternPresetSource::USER;
    const bool factoryDetail = patternLibrary &&
        picker.detailVisible.get() && picker.pattern().descriptor.valid &&
        picker.pattern().descriptor.source == core::state::sequencer::
            SequencerPatternPresetSource::FACTORY;
    const bool factoryCopyPending = patternLibrary &&
        picker.pattern().factoryCopyPending;
    const bool focusedFolder = patternLibrary &&
        picker.pattern().sourceFilter == core::state::sequencer::
            SequencerPatternPresetSourceFilter::USER &&
        picker.selectedItemIsExistingAsset() &&
        picker.entryKind(picker.existingEntryIndexForSelectedItem()) ==
            core::state::sequencer::SequencerPresetLibraryEntryKind::FOLDER;

    props.visible = true;
    props.slots[0] = core::ui::makeStandaloneIconStripSlot(
        managementPanel
            ? ::standalone::icons::ACTION_BACKWARD
            : factoryCopyPending
                ? ::standalone::icons::ACTION_BACKWARD
            : factoryDetail
                ? ::standalone::icons::ACTION_COPY
            : (userDetail || focusedFolder)
                ? ::standalone::icons::SETTINGS_GEAR
                : action.saveMode
            ? ::standalone::icons::ACTION_LOAD
            : ::standalone::icons::ACTION_SAVE,
        Visual::ACTIVE,
        managementPanel || factoryCopyPending || userDetail || focusedFolder
            ? Tone::NEUTRAL
            : factoryDetail
                ? Tone::CONSTRUCTIVE
            : (action.saveMode ? Tone::CONSTRUCTIVE : Tone::POSITIVE)
    );
    props.slots[1].visualState = Visual::HIDDEN;
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        action.statusIcon != nullptr
            ? action.statusIcon
            : (action.primaryIcon != nullptr
                ? action.primaryIcon
                : action.overwriteIcon
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
    return props;
}

}  // namespace core::context::standalone::sequencer_overlay_presenter

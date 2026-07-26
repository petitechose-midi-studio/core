#include "state/macro/MacroInteractionContextBuilder.hpp"

#include <config/PlatformCompat.hpp>

#include "state/shared/StructureSlotOps.hpp"

namespace core::state::macro {

namespace structure_slots = core::state::shared;

FLASHMEM core::state::StructureNavigationFocus effectiveMacroNavigationFocus(
    core::state::StructureNavigationFocus requestedFocus
) {
    // Track, Page and Macro are first-class hot-surface contexts selected by
    // the shared press/hold/turn/release gesture.
    return requestedFocus;
}

namespace {

FLASHMEM core::state::StructureNavigationFocus effectiveNavigationFocus(
    const MacroInteractionContextSource& source
) {
    return effectiveMacroNavigationFocus(
        source.navigationFocus
    );
}

}  // namespace

FLASHMEM bool macroInteractionPreviewingAddSlot(
    const MacroInteractionContextSource& source
) {
    switch (effectiveNavigationFocus(source)) {
        case core::state::StructureNavigationFocus::TRACK:
            return source.trackNavigation.previewAddSlot.get();
        case core::state::StructureNavigationFocus::STEP:
            return source.pages.isMacroAddSlot(source.macroUi.focusedMacroSlot.get());
        case core::state::StructureNavigationFocus::PAGE:
        default:
            return source.macroUi.previewAddPageSlot.get();
    }
}

FLASHMEM bool macroInteractionCanPasteStructure(
    const MacroInteractionContextSource& source
) {
    switch (effectiveNavigationFocus(source)) {
        case core::state::StructureNavigationFocus::TRACK:
            return source.structureClipboard.hasMacroTrack();
        case core::state::StructureNavigationFocus::STEP:
            return source.macroUi.focusedMacroSlot.get() < MACRO_COUNT &&
                   source.structureClipboard.hasMacroSlot();
        case core::state::StructureNavigationFocus::PAGE:
        default:
            return source.structureClipboard.hasMacroPage();
    }
}

FLASHMEM bool macroInteractionCanRemoveStructure(
    const MacroInteractionContextSource& source
) {
    switch (effectiveNavigationFocus(source)) {
        case core::state::StructureNavigationFocus::TRACK:
            return !source.trackNavigation.previewAddSlot.get() &&
                   structure_slots::countEnabled(source.enabledTrackMask, TRACK_COUNT) > 1U;
        case core::state::StructureNavigationFocus::STEP:
            return !source.pages.isMacroAddSlot(source.macroUi.focusedMacroSlot.get()) &&
                   source.pages.isMacroSlotActive(
                       source.macroUi.focusedMacroSlot.get()
                   );
        case core::state::StructureNavigationFocus::PAGE:
        default:
            return !source.macroUi.previewAddPageSlot.get() &&
                   structure_slots::countEnabled(
                       source.pages.currentEnabledPageMask(),
                       PAGE_COUNT
                   ) > 1U;
    }
}

FLASHMEM MacroInteractionContext buildMacroInteractionContext(
    const MacroInteractionContextSource& source
) {
    return MacroInteractionContext{
        .navigationFocus = effectiveNavigationFocus(source),
        .blockingOverlay = source.blockingOverlay,
        .slotPropertySelecting = source.slotPropertySelecting,
        .previewingAddSlot = macroInteractionPreviewingAddSlot(source),
        .compatibleClipboardAvailable = macroInteractionCanPasteStructure(source),
        .canRemoveStructure = macroInteractionCanRemoveStructure(source),
    };
}

}  // namespace core::state::macro

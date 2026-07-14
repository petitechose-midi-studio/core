#include "state/macro/MacroInteractionContextBuilder.hpp"

#include <config/PlatformCompat.hpp>

#include "state/macro/MacroSelectionDeleteAction.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::state::macro {

namespace structure_slots = core::state::shared;

FLASHMEM bool macroInteractionSelectionActive(
    const MacroInteractionContextSource& source
) {
    return !source.blockingOverlay &&
           (source.trackNavigation.selection.active.get() ||
            source.macroUi.pageSelection.active.get());
}

FLASHMEM bool macroInteractionPreviewingAddSlot(
    const MacroInteractionContextSource& source
) {
    switch (source.navigationFocus) {
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
    switch (source.navigationFocus) {
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
    switch (source.navigationFocus) {
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
    const bool trackSelection = source.trackNavigation.selection.active.get();
    const auto& selection = trackSelection
        ? source.trackNavigation.selection
        : source.macroUi.pageSelection;
    const auto selectionScope = trackSelection
        ? core::state::StructureSelectionScope::TRACK
        : core::state::StructureSelectionScope::PAGE;
    const uint16_t selectionEnabledMask = trackSelection
        ? source.enabledTrackMask
        : source.pages.currentEnabledPageMask();
    const auto selectionDeleteAction = buildMacroSelectionDeleteActionSpec({
        .active = macroInteractionSelectionActive(source),
        .scope = selectionScope,
        .selectedMask = selection.selectedMask.get(),
        .enabledMask = selectionEnabledMask,
        .currentIndex = selection.cursorIndex.get(),
        .activeTrack = source.pages.currentActiveTrack(),
        .activePage = source.pages.currentActivePage(),
    });

    return MacroInteractionContext{
        .navigationFocus = source.navigationFocus,
        .blockingOverlay = source.blockingOverlay,
        .slotPropertySelecting = source.slotPropertySelecting,
        .selectionActive = macroInteractionSelectionActive(source),
        .previewingAddSlot = macroInteractionPreviewingAddSlot(source),
        .compatibleClipboardAvailable = macroInteractionCanPasteStructure(source),
        .canRemoveStructure = macroInteractionCanRemoveStructure(source),
        .selectionDeleteAction = selectionDeleteAction,
    };
}

}  // namespace core::state::macro

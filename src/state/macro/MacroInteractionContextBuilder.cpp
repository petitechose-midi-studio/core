#include "state/macro/MacroInteractionContextBuilder.hpp"

#include <config/PlatformCompat.hpp>

#include "state/shared/StructureSlotOps.hpp"

namespace core::state::macro {

namespace structure_slots = core::state::shared;

namespace {

FLASHMEM bool macroSlotAutomationActive(const MacroPagesState& pages, uint8_t macroIndex) {
    if (macroIndex >= MACRO_COUNT) return false;
    const auto* slot = macroAutomationFindSlot(
        pages.automation,
        MacroAutomationSlotAddress{
            .track = pages.currentActiveTrack(),
            .page = pages.currentActivePage(),
            .macro = macroIndex,
        }
    );
    return slot != nullptr && slot->automation.active;
}

}  // namespace

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
            return !source.pages.isMacroAddSlot(source.macroUi.focusedMacroSlot.get()) &&
                   source.structureClipboard.hasMacroAutomation();
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
                   macroSlotAutomationActive(
                       source.pages,
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
        .navigationFocus = source.navigationFocus,
        .blockingOverlay = source.blockingOverlay,
        .slotPropertySelecting = source.slotPropertySelecting,
        .selectionActive = macroInteractionSelectionActive(source),
        .previewingAddSlot = macroInteractionPreviewingAddSlot(source),
        .compatibleClipboardAvailable = macroInteractionCanPasteStructure(source),
        .canRemoveStructure = macroInteractionCanRemoveStructure(source),
    };
}

}  // namespace core::state::macro

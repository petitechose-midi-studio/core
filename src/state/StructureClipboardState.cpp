#include "state/StructureClipboardState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM void SequencerPageClipboard::reset() {
    valid = false;
    sourcePage = core::state::sequencer::SequencerPatternState::PAGE_COUNT;
    count = 0;
    enabledMask = 0;
}

FLASHMEM void StructureClipboardState::clear() {
    kind.set(StructureClipboardKind::NONE);
    sequencerPage.reset();
    revision.set(revision.get() + 1);
}

FLASHMEM void StructureClipboardState::storeMacroPage(
    const core::state::macro::MacroPageData& page
) {
    macroPage = page;
    kind.set(StructureClipboardKind::MACRO_PAGE);
    revision.set(revision.get() + 1);
}

FLASHMEM void StructureClipboardState::storeMacroTrack(
    const core::state::macro::MacroTrackData& track
) {
    macroTrack = track;
    kind.set(StructureClipboardKind::MACRO_TRACK);
    revision.set(revision.get() + 1);
}

FLASHMEM void StructureClipboardState::storeSequencerPage(
    const core::state::SequencerPageClipboard& page
) {
    sequencerPage = page;
    kind.set(StructureClipboardKind::SEQUENCER_PAGE);
    revision.set(revision.get() + 1);
}

FLASHMEM void StructureClipboardState::storeSequencerTrack(
    const core::state::sequencer::SequencerPatternSnapshot& track
) {
    sequencerTrack = track;
    kind.set(StructureClipboardKind::SEQUENCER_TRACK);
    revision.set(revision.get() + 1);
}

}  // namespace core::state

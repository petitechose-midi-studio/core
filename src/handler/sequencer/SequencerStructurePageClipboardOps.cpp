#include "handler/sequencer/SequencerStructurePageClipboardOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

namespace core::handler {

FLASHMEM bool capturePageClipboard(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t page,
    core::state::SequencerPageClipboard& clipboard
) {
    clipboard.reset();
    if (page >= core::state::sequencer::SequencerState::PAGE_COUNT) return false;

    const uint8_t start = static_cast<uint8_t>(
        page * core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );
    const uint8_t len = sequencer.pattern.length.get();
    const uint8_t count = (start >= len)
        ? 0
        : static_cast<uint8_t>(std::min<uint16_t>(
              core::state::sequencer::SequencerState::STEPS_PER_PAGE,
              static_cast<uint16_t>(len - start)
          ));
    if (count == 0) return false;

    clipboard.valid = true;
    clipboard.sourcePage = page;
    clipboard.count = count;
    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t step = static_cast<uint8_t>(start + i);
        clipboard.note[i] = sequencer.pattern.note[step];
        clipboard.velocity[i] = sequencer.pattern.velocity[step];
        clipboard.gate[i] = sequencer.pattern.gate[step];
        clipboard.nudge[i] = sequencer.pattern.nudge[step];
        clipboard.probability[i] = sequencer.pattern.probability[step];
        if (sequencer.pattern.isEnabled(step)) {
            clipboard.enabledMask |= static_cast<uint8_t>(1U << i);
        }
    }
    return true;
}

}  // namespace core::handler

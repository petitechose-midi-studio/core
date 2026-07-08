#pragma once

#include <algorithm>
#include <cstdint>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

inline bool capturePageClipboard(
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

inline void copyPageStepContentFromGraph(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerPageClipboard& clipboard,
    const oc::note::sequencer::StepSequencerGraph* sourceGraph,
    uint8_t targetPage
) {
    if (sourceGraph == nullptr || !sourceGraph->enabled) return;

    const uint8_t sourceStart = static_cast<uint8_t>(
        clipboard.sourcePage *
        core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );
    const uint8_t targetStart = static_cast<uint8_t>(
        targetPage * core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );

    for (uint8_t i = 0; i < clipboard.count; ++i) {
        const uint8_t sourceStep = static_cast<uint8_t>(sourceStart + i);
        const uint8_t targetStep = static_cast<uint8_t>(targetStart + i);
        if (sourceStep >= core::state::sequencer::SequencerState::MAX_STEPS ||
            targetStep >= core::state::sequencer::SequencerState::MAX_STEPS) {
            break;
        }

        const auto sourceNode = core::state::sequencer::rootStepNodeId(sourceStep);
        const auto targetNode = core::state::sequencer::rootStepNodeId(targetStep);
        core::state::sequencer::copyStepNodePayloadFromGraph(
            sequencer.pattern,
            targetNode,
            *sourceGraph,
            sourceNode
        );
    }
}

inline void pastePageClipboard(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerPageClipboard& clipboard,
    const oc::note::sequencer::StepSequencerGraph* sourceGraph,
    uint8_t targetPage
) {
    const uint8_t targetStart =
        static_cast<uint8_t>(targetPage * core::state::sequencer::SequencerState::STEPS_PER_PAGE);
    const uint8_t targetEnd = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS - 1,
        static_cast<uint16_t>(
            targetStart + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1
        )
    ));
    core::state::sequencer::clearStepRange(sequencer, targetStart, targetEnd);

    const uint8_t requiredLength = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS,
        static_cast<uint16_t>(targetStart + std::max<uint8_t>(clipboard.count, 1))
    ));
    if (sequencer.pattern.length.get() < requiredLength) {
        sequencer.pattern.length.set(requiredLength);
    }

    for (uint8_t i = 0; i < clipboard.count; ++i) {
        const uint8_t step = static_cast<uint8_t>(targetStart + i);
        sequencer.pattern.note[step] = clipboard.note[i];
        sequencer.pattern.velocity[step] = clipboard.velocity[i];
        sequencer.pattern.gate[step] = clipboard.gate[i];
        sequencer.pattern.nudge[step] = clipboard.nudge[i];
        sequencer.pattern.probability[step] = clipboard.probability[i];
        sequencer.pattern.setEnabled(step, clipboard.isEnabled(i));
    }

    copyPageStepContentFromGraph(sequencer, clipboard, sourceGraph, targetPage);
}

}  // namespace core::handler

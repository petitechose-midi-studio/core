#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/macro/MacroPagesState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::state {

/**
 * Cross-domain clipboard for page and track structure operations.
 *
 * The clipboard stores detached value snapshots plus a revision signal so views
 * can react without owning macro or sequencer domain mutation.
 */
enum class StructureClipboardKind : uint8_t {
    NONE = 0,
    MACRO_PAGE = 1,
    MACRO_TRACK = 2,
    SEQUENCER_PAGE = 3,
    SEQUENCER_TRACK = 4,
};

struct SequencerPageClipboard {
    static constexpr uint8_t STEP_COUNT = core::state::sequencer::SequencerState::STEPS_PER_PAGE;

    bool valid = false;
    uint8_t sourcePage = core::state::sequencer::SequencerState::PAGE_COUNT;
    uint8_t count = 0;
    uint8_t enabledMask = 0;
    std::array<uint8_t, STEP_COUNT> note{};
    std::array<uint8_t, STEP_COUNT> velocity{};
    std::array<uint16_t, STEP_COUNT> gate{};
    std::array<int8_t, STEP_COUNT> nudge{};
    std::array<uint8_t, STEP_COUNT> probability{};

    void reset() {
        valid = false;
        sourcePage = core::state::sequencer::SequencerState::PAGE_COUNT;
        count = 0;
        enabledMask = 0;
    }

    bool isEnabled(uint8_t index) const {
        if (index >= count) return false;
        return (enabledMask & static_cast<uint8_t>(1U << index)) != 0;
    }
};

struct StructureClipboardState {
    oc::state::Signal<StructureClipboardKind, 4> kind{StructureClipboardKind::NONE};
    oc::state::Signal<uint32_t> revision{0};

    core::state::macro::MacroPageData macroPage{};
    core::state::macro::MacroTrackData macroTrack{};
    core::state::SequencerPageClipboard sequencerPage{};
    core::state::sequencer::SequencerPatternSnapshot sequencerTrack{};

    void clear() {
        kind.set(StructureClipboardKind::NONE);
        sequencerPage.reset();
        revision.set(revision.get() + 1);
    }

    void storeMacroPage(const core::state::macro::MacroPageData& page) {
        macroPage = page;
        kind.set(StructureClipboardKind::MACRO_PAGE);
        revision.set(revision.get() + 1);
    }

    void storeMacroTrack(const core::state::macro::MacroTrackData& track) {
        macroTrack = track;
        kind.set(StructureClipboardKind::MACRO_TRACK);
        revision.set(revision.get() + 1);
    }

    void storeSequencerPage(const core::state::SequencerPageClipboard& page) {
        sequencerPage = page;
        kind.set(StructureClipboardKind::SEQUENCER_PAGE);
        revision.set(revision.get() + 1);
    }

    void storeSequencerTrack(const core::state::sequencer::SequencerPatternSnapshot& track) {
        sequencerTrack = track;
        kind.set(StructureClipboardKind::SEQUENCER_TRACK);
        revision.set(revision.get() + 1);
    }

    bool hasMacroPage() const { return kind.get() == StructureClipboardKind::MACRO_PAGE; }
    bool hasMacroTrack() const { return kind.get() == StructureClipboardKind::MACRO_TRACK; }
    bool hasSequencerPage() const {
        return kind.get() == StructureClipboardKind::SEQUENCER_PAGE && sequencerPage.valid;
    }
    bool hasSequencerTrack() const { return kind.get() == StructureClipboardKind::SEQUENCER_TRACK; }
};

}  // namespace core::state

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "state/sequencer/SequencerState.hpp"

namespace core::ui::sequencer {

struct SequencerStepPresetPickerPresentation {
    static constexpr size_t ITEM_CAPACITY =
        core::state::sequencer::SequencerStepPresetPickerState::ENTRY_CAPACITY + 1U;

    std::array<std::array<char, 64>, ITEM_CAPACITY> itemBuffers{};
    std::array<const char*, ITEM_CAPACITY> items{};
    std::array<char, 40> title{};
    std::array<char, 56> meta{};
    uint32_t dataRevision = 0;
    int selectedIndex = 0;
    int itemCount = 0;
    bool visible = false;

    SequencerStepPresetPickerPresentation() = default;

    SequencerStepPresetPickerPresentation(
        const SequencerStepPresetPickerPresentation& other
    ) {
        copyFrom(other);
    }

    SequencerStepPresetPickerPresentation& operator=(
        const SequencerStepPresetPickerPresentation& other
    ) {
        if (this != &other) copyFrom(other);
        return *this;
    }

    SequencerStepPresetPickerPresentation(
        SequencerStepPresetPickerPresentation&& other
    ) noexcept {
        copyFrom(other);
    }

    SequencerStepPresetPickerPresentation& operator=(
        SequencerStepPresetPickerPresentation&& other
    ) noexcept {
        if (this != &other) copyFrom(other);
        return *this;
    }

private:
    void copyFrom(const SequencerStepPresetPickerPresentation& other) {
        itemBuffers = other.itemBuffers;
        title = other.title;
        meta = other.meta;
        dataRevision = other.dataRevision;
        selectedIndex = other.selectedIndex;
        itemCount = other.itemCount;
        visible = other.visible;
        items.fill(nullptr);
        const size_t reboundCount = itemCount > 0
            ? static_cast<size_t>(itemCount)
            : 0U;
        for (size_t i = 0; i < reboundCount && i < items.size(); ++i) {
            items[i] = itemBuffers[i].data();
        }
    }
};

enum class SequencerStepPresetActionVisual : uint8_t {
    DISABLED = 0,
    ACTIVE,
    PRESSED,
    ARMED,
    CANCELLED,
    APPLIED,
};

enum class SequencerStepPresetActionTone : uint8_t {
    NEUTRAL = 0,
    CONSTRUCTIVE,
    DESTRUCTIVE,
    POSITIVE,
    WARNING,
};

struct SequencerStepPresetActionPresentation {
    SequencerStepPresetActionVisual visual =
        SequencerStepPresetActionVisual::DISABLED;
    SequencerStepPresetActionTone tone =
        SequencerStepPresetActionTone::NEUTRAL;
    bool saveIcon = false;
    bool overwriteIcon = false;
    const char* statusIcon = nullptr;
    bool showLabel = false;
    std::array<char, 16> label{};
    bool holdActive = false;
    uint32_t holdStartedAtMs = 0;
    uint16_t holdDurationMs = 0;
};

SequencerStepPresetPickerPresentation buildSequencerStepPresetPickerPresentation(
    const core::state::sequencer::SequencerState& sequencer
);

SequencerStepPresetActionPresentation buildSequencerStepPresetActionPresentation(
    const core::state::sequencer::SequencerStepPresetPickerState& picker
);

}  // namespace core::ui::sequencer

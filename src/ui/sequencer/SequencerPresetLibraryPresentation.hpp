#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "state/sequencer/SequencerState.hpp"
#include "ui/sequencer/SequencerChordVoiceRail.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::ui::sequencer {

struct SequencerPresetLibraryPresentation {
    static constexpr size_t ITEM_CAPACITY =
        core::state::sequencer::SequencerPresetLibrarySessionState::
            ENTRY_CAPACITY + 2U;

    std::array<std::array<char, 64>, ITEM_CAPACITY> itemBuffers{};
    std::array<std::array<char, 20>, ITEM_CAPACITY> itemValueBuffers{};
    std::array<const char*, ITEM_CAPACITY> items{};
    std::array<const char*, ITEM_CAPACITY> itemValues{};
    std::array<const char*, ITEM_CAPACITY> itemIcons{};
    std::array<uint32_t, ITEM_CAPACITY> itemIconColors{};
    std::array<std::array<char, 4>, 8> voiceRailLabels{};
    std::array<std::array<char, 8>, 8> voiceRailValues{};
    std::array<char, 40> title{};
    std::array<char, 56> meta{};
    std::array<char, 56> breadcrumb{};
    const char* headerIcon = nullptr;
    uint32_t headerIconColor = 0U;
    core::ui::SequencerChordVoiceRailProps chordVoiceRail{};
    uint32_t dataRevision = 0;
    int selectedIndex = 0;
    int itemCount = 0;
    bool showIndexColumn = false;
    bool patternPreviewVisible = false;
    bool visible = false;

    SequencerPresetLibraryPresentation() = default;

    SequencerPresetLibraryPresentation(
        const SequencerPresetLibraryPresentation& other
    ) {
        copyFrom(other);
    }

    SequencerPresetLibraryPresentation& operator=(
        const SequencerPresetLibraryPresentation& other
    ) {
        if (this != &other) copyFrom(other);
        return *this;
    }

    SequencerPresetLibraryPresentation(
        SequencerPresetLibraryPresentation&& other
    ) noexcept {
        copyFrom(other);
    }

    SequencerPresetLibraryPresentation& operator=(
        SequencerPresetLibraryPresentation&& other
    ) noexcept {
        if (this != &other) copyFrom(other);
        return *this;
    }

private:
    void copyFrom(const SequencerPresetLibraryPresentation& other) {
        itemBuffers = other.itemBuffers;
        itemValueBuffers = other.itemValueBuffers;
        title = other.title;
        meta = other.meta;
        breadcrumb = other.breadcrumb;
        headerIcon = other.headerIcon;
        headerIconColor = other.headerIconColor;
        itemIcons = other.itemIcons;
        itemIconColors = other.itemIconColors;
        voiceRailLabels = other.voiceRailLabels;
        voiceRailValues = other.voiceRailValues;
        chordVoiceRail = other.chordVoiceRail;
        dataRevision = other.dataRevision;
        selectedIndex = other.selectedIndex;
        itemCount = other.itemCount;
        showIndexColumn = other.showIndexColumn;
        patternPreviewVisible = other.patternPreviewVisible;
        visible = other.visible;
        items.fill(nullptr);
        itemValues.fill(nullptr);
        const size_t reboundCount = itemCount > 0
            ? static_cast<size_t>(itemCount)
            : 0U;
        for (size_t i = 0; i < reboundCount && i < items.size(); ++i) {
            items[i] = itemBuffers[i].data();
            itemValues[i] = itemValueBuffers[i].data();
        }
        for (size_t i = 0;
             i < chordVoiceRail.itemCount &&
             i < chordVoiceRail.items.size();
             ++i) {
            chordVoiceRail.items[i].label = voiceRailLabels[i].data();
            chordVoiceRail.items[i].value = voiceRailValues[i].data();
        }
    }
};

struct SequencerPresetLibraryActionPresentation {
    core::ui::ContextActionStripVisualState visual =
        core::ui::ContextActionStripVisualState::DISABLED;
    core::ui::ContextActionStripTone tone =
        core::ui::ContextActionStripTone::NEUTRAL;
    bool saveMode = false;
    bool overwriteIcon = false;
    const char* primaryIcon = nullptr;
    const char* statusIcon = nullptr;
    bool holdActive = false;
    uint32_t holdStartedAtMs = 0;
    uint16_t holdDurationMs = 0;
};

SequencerPresetLibraryPresentation buildSequencerPresetLibraryPresentation(
    const core::state::sequencer::SequencerState& sequencer
);

SequencerPresetLibraryActionPresentation buildSequencerPresetLibraryActionPresentation(
    const core::state::sequencer::SequencerPresetLibrarySessionState& picker
);

}  // namespace core::ui::sequencer

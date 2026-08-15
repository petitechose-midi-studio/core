#pragma once

#include <array>
#include <cstdint>

#include "app/ViewTypes.hpp"

namespace core::state {

enum class ViewSelectorItem : uint8_t {
    MACROS = 0,
    SEQUENCER,
    MODULATORS,
    PROJECT_SETTINGS,
    DEVICE_SETTINGS,
    COUNT
};

inline constexpr int VIEW_SELECTOR_ITEM_COUNT =
    static_cast<int>(ViewSelectorItem::COUNT);

inline constexpr std::array<const char*, VIEW_SELECTOR_ITEM_COUNT> VIEW_SELECTOR_ITEM_LABELS = {
    "Macros",
    "Sequencer",
    "Modulators",
    "Project",
    "Device",
};

inline ViewSelectorItem viewSelectorItemAt(int index) {
    if (index < 0 || index >= VIEW_SELECTOR_ITEM_COUNT) {
        return ViewSelectorItem::MACROS;
    }
    return static_cast<ViewSelectorItem>(index);
}

inline const char* viewSelectorItemLabel(ViewSelectorItem item) {
    return VIEW_SELECTOR_ITEM_LABELS[static_cast<int>(item)];
}

inline ViewSelectorItem viewSelectorItemForView(core::ui::ViewType view) {
    switch (view) {
        case core::ui::ViewType::SEQUENCER:
            return ViewSelectorItem::SEQUENCER;
        case core::ui::ViewType::MODULATORS:
            return ViewSelectorItem::MODULATORS;
        case core::ui::ViewType::PROJECT:
            return ViewSelectorItem::PROJECT_SETTINGS;
        case core::ui::ViewType::DEVICE_SETTINGS:
            return ViewSelectorItem::DEVICE_SETTINGS;
        case core::ui::ViewType::MACRO:
        default:
            return ViewSelectorItem::MACROS;
    }
}

inline bool viewSelectorItemHasView(ViewSelectorItem item) {
    return item == ViewSelectorItem::MACROS ||
           item == ViewSelectorItem::SEQUENCER ||
           item == ViewSelectorItem::MODULATORS ||
           item == ViewSelectorItem::PROJECT_SETTINGS ||
           item == ViewSelectorItem::DEVICE_SETTINGS;
}

inline core::ui::ViewType viewForSelectorItem(ViewSelectorItem item) {
    switch (item) {
        case ViewSelectorItem::SEQUENCER:
            return core::ui::ViewType::SEQUENCER;
        case ViewSelectorItem::MODULATORS:
            return core::ui::ViewType::MODULATORS;
        case ViewSelectorItem::PROJECT_SETTINGS:
            return core::ui::ViewType::PROJECT;
        case ViewSelectorItem::DEVICE_SETTINGS:
            return core::ui::ViewType::DEVICE_SETTINGS;
        case ViewSelectorItem::MACROS:
        default:
            return core::ui::ViewType::MACRO;
    }
}

}  // namespace core::state

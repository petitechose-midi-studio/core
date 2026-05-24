#pragma once

#include <array>
#include <cstdint>

#include "app/ViewTypes.hpp"

namespace core::state {

enum class ViewSelectorItem : uint8_t {
    MACROS = 0,
    SEQUENCER,
    GLOBAL_SETTINGS,
    COUNT
};

inline constexpr int VIEW_SELECTOR_ITEM_COUNT =
    static_cast<int>(ViewSelectorItem::COUNT);

inline constexpr std::array<const char*, VIEW_SELECTOR_ITEM_COUNT> VIEW_SELECTOR_ITEM_LABELS = {
    "Macros",
    "Sequencer",
    "Global Settings",
};

inline constexpr const char* SETTINGS_SECTION_LABEL = "Settings";

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
        case core::ui::ViewType::MACRO:
        default:
            return ViewSelectorItem::MACROS;
    }
}

inline bool viewSelectorItemHasView(ViewSelectorItem item) {
    return item == ViewSelectorItem::MACROS || item == ViewSelectorItem::SEQUENCER;
}

inline core::ui::ViewType viewForSelectorItem(ViewSelectorItem item) {
    return item == ViewSelectorItem::SEQUENCER ? core::ui::ViewType::SEQUENCER
                                               : core::ui::ViewType::MACRO;
}

inline bool viewSelectorItemHasSettingsAction(ViewSelectorItem item) {
    return item == ViewSelectorItem::SEQUENCER;
}

}  // namespace core::state

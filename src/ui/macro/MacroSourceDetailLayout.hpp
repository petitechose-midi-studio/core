#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace core::ui::macro {

/** Facts shared by the Macro detail handler and its presenter. */
struct MacroSourceDetailContext {
    bool automationStored = false;
    bool modulationStored = false;
    bool automationPlayback = false;
    bool modulationPlayback = false;
    bool manualOverride = false;
};

enum class AutomationDetailItem : uint8_t {
    PLAYBACK = 0,
    RESUME,
    CONVERT_TO_MODULATION,
    LENGTH,
    OFFSET,
    CURVE,
};

enum class ModulationDetailItem : uint8_t {
    PLAYBACK = 0,
    DEPTH,
    CURVE,
    ORIGIN,
};

template <typename Item, size_t Capacity>
struct MacroSourceDetailLayout {
    std::array<Item, Capacity> items{};
    uint8_t count = 0;

    void append(Item item) {
        if (count < items.size()) items[count++] = item;
    }

    [[nodiscard]] Item at(uint8_t visibleIndex) const {
        return items[visibleIndex < count ? visibleIndex : 0U];
    }
};

using AutomationDetailLayout =
    MacroSourceDetailLayout<AutomationDetailItem, 6>;
using ModulationDetailLayout =
    MacroSourceDetailLayout<ModulationDetailItem, 4>;

inline bool needsAutomationResume(const MacroSourceDetailContext& context) {
    return context.manualOverride;
}

inline AutomationDetailLayout buildAutomationDetailLayout(
    const MacroSourceDetailContext& context
) {
    AutomationDetailLayout out;
    out.append(AutomationDetailItem::PLAYBACK);
    if (needsAutomationResume(context)) out.append(AutomationDetailItem::RESUME);
    if (context.automationStored) {
        // Conversion is a primary semantic action, so it stays above the
        // lower-frequency window-edit controls.
        out.append(AutomationDetailItem::CONVERT_TO_MODULATION);
        out.append(AutomationDetailItem::LENGTH);
        out.append(AutomationDetailItem::OFFSET);
        out.append(AutomationDetailItem::CURVE);
    }
    return out;
}

inline ModulationDetailLayout buildModulationDetailLayout(
    const MacroSourceDetailContext& context
) {
    ModulationDetailLayout out;
    out.append(ModulationDetailItem::PLAYBACK);
    if (context.modulationStored) {
        out.append(ModulationDetailItem::DEPTH);
        out.append(ModulationDetailItem::CURVE);
        out.append(ModulationDetailItem::ORIGIN);
    }
    return out;
}

}  // namespace core::ui::macro

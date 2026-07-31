#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace core::state::macro {

/** Domain facts that determine the semantic Macro source-detail actions. */
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
    INVALID = 0xFFU,
};

enum class ModulationDetailItem : uint8_t {
    PLAYBACK = 0,
    DEPTH,
    SHAPE,
    ORIGIN,
    INVALID = 0xFFU,
};

template <typename Item, std::size_t Capacity>
struct MacroSourceDetailPolicy {
    std::array<Item, Capacity> items{};
    uint8_t count = 0;

    void append(Item item) {
        if (count < items.size()) items[count++] = item;
    }

    [[nodiscard]] Item at(uint8_t visibleIndex) const {
        return visibleIndex < count ? items[visibleIndex] : Item::INVALID;
    }
};

using AutomationDetailPolicy =
    MacroSourceDetailPolicy<AutomationDetailItem, 5>;
using ModulationDetailPolicy =
    MacroSourceDetailPolicy<ModulationDetailItem, 4>;

inline bool needsAutomationResume(
    const MacroSourceDetailContext& context
) {
    return context.manualOverride;
}

inline AutomationDetailPolicy buildAutomationDetailPolicy(
    const MacroSourceDetailContext& context
) {
    AutomationDetailPolicy out;
    out.append(AutomationDetailItem::PLAYBACK);
    if (needsAutomationResume(context)) {
        out.append(AutomationDetailItem::RESUME);
    }
    if (context.automationStored) {
        // Conversion is a primary semantic action, so it stays above the
        // lower-frequency window-edit controls.
        out.append(AutomationDetailItem::CONVERT_TO_MODULATION);
        out.append(AutomationDetailItem::LENGTH);
        out.append(AutomationDetailItem::OFFSET);
    }
    return out;
}

inline ModulationDetailPolicy buildModulationDetailPolicy(
    const MacroSourceDetailContext& context
) {
    ModulationDetailPolicy out;
    out.append(ModulationDetailItem::PLAYBACK);
    if (context.modulationStored) {
        out.append(ModulationDetailItem::DEPTH);
        out.append(ModulationDetailItem::SHAPE);
        out.append(ModulationDetailItem::ORIGIN);
    }
    return out;
}

}  // namespace core::state::macro

#include "context/standalone/MacroOverlayPresenterFormatters.hpp"

#include <algorithm>
#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

namespace core::context::standalone::macro_overlay_presenter {

FLASHMEM void initializeStaticItems(StaticItems& items) {
    for (int i = 0; i < 16; ++i) {
        oc::type::text::formatUnsigned(items.channelLabels[i].data(), items.channelLabels[i].size(), i + 1);
        items.channelItems[i] = items.channelLabels[i].data();
    }

    for (int i = 0; i < 128; ++i) {
        oc::type::text::formatUnsigned(items.ccLabels[i].data(), items.ccLabels[i].size(), i);
        items.ccItems[i] = items.ccLabels[i].data();
    }

    for (uint8_t i = 0; i < core::state::MACRO_COUNT; ++i) {
        size_t pos = oc::type::text::appendString(items.macroLabels[i].data(), items.macroLabels[i].size(), 0, "Macro ");
        pos = oc::type::text::appendUnsigned(
            items.macroLabels[i].data(),
            items.macroLabels[i].size(),
            pos,
            static_cast<unsigned>(i) + 1U
        );
        oc::type::text::terminate(items.macroLabels[i].data(), items.macroLabels[i].size(), pos);
        items.macroItems[i] = items.macroLabels[i].data();
    }
}

FLASHMEM EditRenderData buildEditRenderData(Source& source) {
    EditRenderData data{};

    const uint8_t macroIndex = source.macroEdit.editingIndex.get();
    const uint8_t channel0 = source.macroEdit.tempChannel.get();
    const uint8_t cc = source.macroEdit.tempCC.get();

    size_t titlePos = oc::type::text::appendString(data.title.data(), data.title.size(), 0, "MACRO ");
    titlePos = oc::type::text::appendUnsigned(
        data.title.data(),
        data.title.size(),
        titlePos,
        static_cast<unsigned>(macroIndex) + 1U
    );
    oc::type::text::terminate(data.title.data(), data.title.size(), titlePos);

    const unsigned page1 = static_cast<unsigned>(source.pages.currentActivePage()) + 1U;
    size_t metaPos = oc::type::text::appendString(data.meta.data(), data.meta.size(), 0, "PAGE ");
    metaPos = oc::type::text::appendUnsigned(data.meta.data(), data.meta.size(), metaPos, page1);
    oc::type::text::terminate(data.meta.data(), data.meta.size(), metaPos);

    oc::type::text::formatUnsigned(data.valueBuffers[0].data(), data.valueBuffers[0].size(), static_cast<unsigned>(channel0) + 1U);
    oc::type::text::formatUnsigned(data.valueBuffers[1].data(), data.valueBuffers[1].size(), static_cast<unsigned>(cc));
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = source.pages.currentActiveTrack(),
        .page = source.pages.currentActivePage(),
        .macro = macroIndex,
    };
    const auto* automation = core::state::macro::macroAutomationFindSlot(
        source.pages.automation,
        address
    );
    const bool automationActive = automation != nullptr && automation->automation.active;
    const bool manualOverride =
        (source.macroUi.automationManualOverrideMask.get() &
         static_cast<uint16_t>(1U << macroIndex)) != 0;
    std::snprintf(
        data.valueBuffers[2].data(),
        data.valueBuffers[2].size(),
        "%s",
        !automationActive ? "Off" : (manualOverride ? "Manual" : "Auto")
    );

    data.rows = {{
        {.key = "Channel", .value = data.valueBuffers[0].data()},
        {.key = "CC", .value = data.valueBuffers[1].data()},
        {.key = "Automation", .value = data.valueBuffers[2].data()},
    }};
    data.selectedIndex = source.macroEdit.focusedRow.get();
    data.dataRevision =
        (static_cast<uint32_t>(macroIndex & 0x07U) << 28) |
        (automationActive ? (1UL << 27) : 0U) |
        (manualOverride ? (1UL << 26) : 0U) |
        (static_cast<uint32_t>(channel0 & 0x0FU) << 20) |
        (static_cast<uint32_t>(cc & 0x7FU) << 12) |
        (static_cast<uint32_t>(source.pages.currentActivePage() & 0x0FU) << 8) |
        (static_cast<uint32_t>(source.macroEdit.focusedRow.get() & 0x0FU) << 4) |
        static_cast<uint32_t>(source.macroUi.automationRecordingRevision.get() & 0x0FU);

    return data;
}

FLASHMEM AutomationRenderData buildAutomationRenderData(const Source& source) {
    AutomationRenderData data{};
    if (!source.macroEdit.automationVisible.get()) {
        return data;
    }

    const uint8_t macroIndex = source.macroEdit.editingIndex.get();
    size_t titlePos = oc::type::text::appendString(data.title.data(), data.title.size(), 0, "MACRO ");
    titlePos = oc::type::text::appendUnsigned(
        data.title.data(),
        data.title.size(),
        titlePos,
        static_cast<unsigned>(macroIndex) + 1U
    );
    oc::type::text::terminate(data.title.data(), data.title.size(), titlePos);
    std::snprintf(data.meta.data(), data.meta.size(), "%s", "AUTOMATION");

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = source.pages.currentActiveTrack(),
        .page = source.pages.currentActivePage(),
        .macro = macroIndex,
    };
    const auto* slot = core::state::macro::macroAutomationFindSlot(
        source.pages.automation,
        address
    );
    const bool active = slot != nullptr && slot->automation.active;
    const bool manualOverride =
        (source.macroUi.automationManualOverrideMask.get() &
         static_cast<uint16_t>(1U << macroIndex)) != 0;

    std::snprintf(
        data.valueBuffers[0].data(),
        data.valueBuffers[0].size(),
        "%s",
        !active ? "Off" : (manualOverride ? "Manual" : "Auto")
    );
    std::snprintf(
        data.valueBuffers[1].data(),
        data.valueBuffers[1].size(),
        "%u pts",
        active ? static_cast<unsigned>(slot->automation.pointCount) : 0U
    );
    if (active) {
        std::snprintf(
            data.valueBuffers[2].data(),
            data.valueBuffers[2].size(),
            "%.1f beats",
            static_cast<double>(
                core::state::macro::macroAutomationBeatsFromTicks(
                    slot->automation.durationTicks
                )
            )
        );
    } else {
        std::snprintf(data.valueBuffers[2].data(), data.valueBuffers[2].size(), "%s", "-");
    }
    std::snprintf(data.valueBuffers[3].data(), data.valueBuffers[3].size(), "%s", "Absolute");

    data.rows = {{
        {.key = "State", .value = data.valueBuffers[0].data()},
        {.key = "Points", .value = data.valueBuffers[1].data()},
        {.key = "Length", .value = data.valueBuffers[2].data()},
        {.key = "Mode", .value = data.valueBuffers[3].data()},
    }};
    data.selectedIndex = source.macroEdit.automationFocusedRow.get();
    data.dataRevision =
        (static_cast<uint32_t>(macroIndex & 0x07U) << 28) |
        (active ? (1UL << 27) : 0U) |
        (manualOverride ? (1UL << 26) : 0U) |
        (active ? static_cast<uint32_t>(slot->automation.pointCount & 0x3FFU) << 14 : 0U) |
        (static_cast<uint32_t>(source.macroEdit.automationFocusedRow.get() & 0x0FU) << 8) |
        static_cast<uint32_t>(source.macroUi.automationRecordingRevision.get() & 0xFFU);
    data.visible = true;
    return data;
}

FLASHMEM SelectorRenderData buildEditSelectorRenderData(const Source& source, const StaticItems& items) {
    SelectorRenderData data{};
    const auto& selector = source.macroEdit.selector;
    if (source.macroEdit.flowPhase.get() != core::state::MacroEditFlowPhase::VALUE_SELECTOR ||
        !selector.visible.get()) {
        return data;
    }

    const uint8_t row = selector.editingRow.get();
    const bool isChannel = row == 0;
    data.title = "VALUE";
    data.meta = isChannel ? "CHANNEL" : "CC";
    data.items = isChannel ? items.channelItems.data() : items.ccItems.data();
    data.itemCount = isChannel ? 16 : 128;
    data.selectedIndex = std::clamp(selector.selectedIndex.get(), 0, data.itemCount - 1);
    data.dataRevision = static_cast<uint32_t>(core::state::MacroEditFlowPhase::VALUE_SELECTOR) << 8 |
                        static_cast<uint32_t>(row + 1U);
    data.visible = true;
    return data;
}

FLASHMEM SelectorRenderData buildPageSelectorRenderData(const Source& source) {
    SelectorRenderData data{};
    if (source.macroEdit.flowPhase.get() != core::state::MacroEditFlowPhase::PAGE_SELECTOR ||
        !source.pages.selector.visible.get()) {
        return data;
    }

    static std::array<const char*, core::state::macro::PAGE_COUNT> pageItems{};
    for (uint8_t i = 0; i < core::state::macro::PAGE_COUNT; ++i) {
        pageItems[i] = source.pages.pageName(i);
    }

    data.title = "PAGE";
    data.meta = "MACRO";
    data.items = pageItems.data();
    data.itemCount = core::state::macro::PAGE_COUNT;
    data.selectedIndex = std::clamp(
        static_cast<int>(source.pages.selector.selectedIndex.get()),
        0,
        static_cast<int>(core::state::macro::PAGE_COUNT) - 1
    );
    data.dataRevision =
        (static_cast<uint32_t>(core::state::MacroEditFlowPhase::PAGE_SELECTOR) << 8) |
        static_cast<uint32_t>(data.selectedIndex + 1);
    data.visible = true;
    return data;
}

FLASHMEM SelectorRenderData buildTargetSelectorRenderData(const Source& source, const StaticItems& items) {
    SelectorRenderData data{};
    if (source.macroEdit.flowPhase.get() != core::state::MacroEditFlowPhase::TARGET_SELECTOR ||
        !source.macroEdit.macroSelector.visible.get()) {
        return data;
    }

    data.title = "MACRO";
    data.meta = "TARGET";
    data.items = items.macroItems.data();
    data.itemCount = core::state::MACRO_COUNT;
    data.selectedIndex = std::clamp(
        source.macroEdit.macroSelector.selectedIndex.get(),
        0,
        static_cast<int>(core::state::MACRO_COUNT) - 1
    );
    data.dataRevision =
        (static_cast<uint32_t>(core::state::MacroEditFlowPhase::TARGET_SELECTOR) << 24) |
        static_cast<uint32_t>(data.selectedIndex + 1);
    data.visible = true;
    return data;
}

}  // namespace core::context::standalone::macro_overlay_presenter

#include "context/standalone/MacroOverlayPresenterFormatters.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

namespace core::context::standalone::macro_overlay_presenter {

namespace {

struct CurveTickSummary {
    bool valid = false;
    uint16_t sourceDurationTicks = 0;
    uint16_t activeDurationTicks = 0;
    uint16_t windowOffsetTicks = 0;
    uint16_t firstTick = 0;
    uint16_t lastTick = 0;
    bool wraps = false;
    uint16_t pointCount = 0;
};

FLASHMEM void formatBeatDuration(char* out,
                                 size_t outSize,
                                 uint16_t durationTicks,
                                 const char* suffix) {
    const uint32_t ticks = durationTicks;
    const uint32_t hundredths =
        (ticks * 100U + core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT / 2U) /
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
    const uint32_t whole = hundredths / 100U;
    const uint32_t fraction = hundredths % 100U;
    if (fraction == 0U) {
        std::snprintf(out, outSize, "%u.0%s", static_cast<unsigned>(whole), suffix ? suffix : "");
    } else if ((fraction % 10U) == 0U) {
        std::snprintf(
            out,
            outSize,
            "%u.%u%s",
            static_cast<unsigned>(whole),
            static_cast<unsigned>(fraction / 10U),
            suffix ? suffix : ""
        );
    } else {
        std::snprintf(
            out,
            outSize,
            "%u.%02u%s",
            static_cast<unsigned>(whole),
            static_cast<unsigned>(fraction),
            suffix ? suffix : ""
        );
    }
}

FLASHMEM size_t appendBeatDurationCompact(char* out,
                                          size_t outSize,
                                          size_t pos,
                                          uint16_t durationTicks,
                                          const char* suffix) {
    if (out == nullptr || pos >= outSize) return pos;
    const uint32_t ticks = durationTicks;
    const uint32_t hundredths =
        (ticks * 100U + core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT / 2U) /
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT;
    const uint32_t whole = hundredths / 100U;
    const uint32_t fraction = hundredths % 100U;
    int written = 0;
    if (fraction == 0U) {
        written = std::snprintf(
            out + pos,
            outSize - pos,
            "%u%s",
            static_cast<unsigned>(whole),
            suffix ? suffix : ""
        );
    } else if ((fraction % 10U) == 0U) {
        written = std::snprintf(
            out + pos,
            outSize - pos,
            "%u.%u%s",
            static_cast<unsigned>(whole),
            static_cast<unsigned>(fraction / 10U),
            suffix ? suffix : ""
        );
    } else {
        written = std::snprintf(
            out + pos,
            outSize - pos,
            "%u.%02u%s",
            static_cast<unsigned>(whole),
            static_cast<unsigned>(fraction),
            suffix ? suffix : ""
        );
    }
    if (written <= 0) return pos;
    const size_t advanced = static_cast<size_t>(written);
    return std::min(outSize - 1U, pos + advanced);
}

FLASHMEM size_t appendText(char* out, size_t outSize, size_t pos, const char* text) {
    if (out == nullptr || text == nullptr || pos >= outSize) return pos;
    const int written = std::snprintf(out + pos, outSize - pos, "%s", text);
    if (written <= 0) return pos;
    return std::min(outSize - 1U, pos + static_cast<size_t>(written));
}

FLASHMEM uint32_t mixRevision(uint32_t seed, uint32_t value) {
    seed ^= value + 0x9E3779B9UL + (seed << 6U) + (seed >> 2U);
    return seed;
}

FLASHMEM CurveTickSummary summarizeCurveTicks(
    const core::state::macro::MacroAutomationCurveRef& curve,
    const core::state::macro::MacroAutomationPointPool& pool
) {
    CurveTickSummary summary{};
    if (!curve.active || curve.pointCount == 0 || curve.pointOffset >= pool.used) {
        return summary;
    }

    const uint16_t available = static_cast<uint16_t>(pool.used - curve.pointOffset);
    const uint16_t count = std::min<uint16_t>(curve.pointCount, available);
    if (count == 0) {
        return summary;
    }

    const uint16_t activeDurationTicks = curve.durationTicks == 0
        ? core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
        : curve.durationTicks;
    const uint16_t lastPointTick = pool.points[
        static_cast<uint16_t>(curve.pointOffset + count - 1U)
    ].tick;
    const uint16_t sourceDurationTicks = std::max<uint16_t>({
        curve.sourceDurationTicks,
        lastPointTick,
        1U,
    });
    const uint16_t windowOffsetTicks = sourceDurationTicks == 0
        ? 0
        : static_cast<uint16_t>(curve.windowOffsetTicks % sourceDurationTicks);
    const uint32_t windowEndTick =
        static_cast<uint32_t>(windowOffsetTicks) + static_cast<uint32_t>(activeDurationTicks);

    summary.valid = true;
    summary.sourceDurationTicks = sourceDurationTicks;
    summary.activeDurationTicks = activeDurationTicks;
    summary.windowOffsetTicks = windowOffsetTicks;
    summary.wraps = windowEndTick > sourceDurationTicks;
    summary.pointCount = count;
    summary.firstTick = std::min<uint16_t>(
        pool.points[curve.pointOffset].tick,
        sourceDurationTicks
    );
    summary.lastTick = std::min<uint16_t>(lastPointTick, sourceDurationTicks);
    return summary;
}

FLASHMEM void formatCurveSummary(char* out,
                                 size_t outSize,
                                 const CurveTickSummary& summary) {
    if (out == nullptr || outSize == 0) return;
    out[0] = '\0';
    if (!summary.valid) {
        std::snprintf(out, outSize, "%s", "-");
        return;
    }

    size_t pos = 0;
    if (summary.pointCount == 1) {
        pos = appendBeatDurationCompact(out, outSize, pos, summary.firstTick, "b");
    } else {
        if (summary.windowOffsetTicks > 0) {
            pos = appendText(out, outSize, pos, "+");
            pos = appendBeatDurationCompact(out, outSize, pos, summary.windowOffsetTicks, "");
            pos = appendText(out, outSize, pos, " ");
        }
        pos = appendBeatDurationCompact(out, outSize, pos, summary.firstTick, "");
        pos = appendText(out, outSize, pos, "-");
        pos = appendBeatDurationCompact(out, outSize, pos, summary.lastTick, "b");
    }
    if (summary.wraps) {
        appendText(out, outSize, pos, " Loop");
    }
}

FLASHMEM ms::ui::KeyValueSparkline buildCurveSparkline(
    const core::state::macro::MacroAutomationCurveRef& curve,
    const core::state::macro::MacroAutomationPointPool& pool
) {
    ms::ui::KeyValueSparkline sparkline{};
    if (!curve.active || curve.pointCount == 0 || curve.pointOffset >= pool.used) {
        return sparkline;
    }

    const uint16_t durationTicks = curve.durationTicks == 0
        ? core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
        : curve.durationTicks;
    constexpr uint8_t sampleCount =
        static_cast<uint8_t>(ms::ui::KEY_VALUE_SPARKLINE_SAMPLE_COUNT);
    sparkline.enabled = true;
    sparkline.sampleCount = sampleCount;

    const uint16_t lastSampleTick = durationTicks > 0U
        ? static_cast<uint16_t>(durationTicks - 1U)
        : 0U;
    for (uint8_t i = 0; i < sampleCount; ++i) {
        const uint32_t tick = sampleCount > 1U
            ? (static_cast<uint32_t>(i) * lastSampleTick) / (sampleCount - 1U)
            : 0U;
        const float beat =
            static_cast<float>(tick) /
            static_cast<float>(core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT);
        const float value = core::state::macro::macroAutomationEvaluate(
            curve,
            pool,
            beat,
            0.0f
        );
        const float clamped = core::state::macro::macroAutomationClamp01(value);
        sparkline.samples[i] = static_cast<uint8_t>(clamped * 255.0f + 0.5f);
    }
    return sparkline;
}

}  // namespace

FLASHMEM void initializeStaticItems(StaticItems& items) {
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

    oc::type::text::formatUnsigned(data.valueBuffers[0].data(), data.valueBuffers[0].size(), static_cast<unsigned>(cc));
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
    if (automationActive) {
        std::snprintf(
            data.valueBuffers[1].data(),
            data.valueBuffers[1].size(),
            "%s ",
            manualOverride ? "Manual" : "Auto"
        );
        const size_t prefixLength = std::strlen(data.valueBuffers[1].data());
        formatBeatDuration(
            data.valueBuffers[1].data() + prefixLength,
            data.valueBuffers[1].size() - prefixLength,
            automation->automation.durationTicks,
            "b"
        );
    } else {
        std::snprintf(data.valueBuffers[1].data(), data.valueBuffers[1].size(), "%s", "Off");
    }

    data.rows = {{
        {.key = "CC", .value = data.valueBuffers[0].data()},
        {.key = "Automation", .value = data.valueBuffers[1].data()},
    }};
    data.selectedIndex = source.macroEdit.focusedRow.get();
    uint32_t revision =
        (static_cast<uint32_t>(macroIndex & 0x07U) << 29) |
        (automationActive ? (1UL << 28) : 0U) |
        (manualOverride ? (1UL << 27) : 0U) |
        (static_cast<uint32_t>(cc & 0x7FU) << 20) |
        (static_cast<uint32_t>(source.pages.currentActivePage() & 0x0FU) << 16) |
        (static_cast<uint32_t>(source.macroEdit.focusedRow.get() & 0x03U) << 14);
    if (automationActive) {
        revision = mixRevision(revision, automation->automation.pointCount);
        revision = mixRevision(revision, automation->automation.durationTicks);
        revision = mixRevision(revision, automation->automation.sourceDurationTicks);
        revision = mixRevision(revision, automation->automation.windowOffsetTicks);
    }
    data.dataRevision = mixRevision(
        revision,
        source.macroUi.automationRecordingRevision.get()
    );

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
    ms::ui::KeyValueSparkline curveSparkline{};
    if (active) {
        const auto curveSummary = summarizeCurveTicks(
            slot->automation,
            source.pages.automation.pointPool
        );
        curveSparkline = buildCurveSparkline(
            slot->automation,
            source.pages.automation.pointPool
        );
        formatBeatDuration(
            data.valueBuffers[1].data(),
            data.valueBuffers[1].size(),
            slot->automation.durationTicks,
            " beats"
        );
        formatBeatDuration(
            data.valueBuffers[2].data(),
            data.valueBuffers[2].size(),
            slot->automation.windowOffsetTicks,
            " beats"
        );
        formatCurveSummary(
            data.valueBuffers[3].data(),
            data.valueBuffers[3].size(),
            curveSummary
        );
        data.meta[0] = '\0';
        size_t metaPos = appendBeatDurationCompact(
            data.meta.data(),
            data.meta.size(),
            0,
            slot->automation.durationTicks,
            "b"
        );
        if (curveSummary.sourceDurationTicks != slot->automation.durationTicks) {
            metaPos = appendText(data.meta.data(), data.meta.size(), metaPos, "/");
            metaPos = appendBeatDurationCompact(
                data.meta.data(),
                data.meta.size(),
                metaPos,
                curveSummary.sourceDurationTicks,
                "b"
            );
        }
        if (curveSummary.windowOffsetTicks > 0) {
            metaPos = appendText(data.meta.data(), data.meta.size(), metaPos, " +");
            appendBeatDurationCompact(
                data.meta.data(),
                data.meta.size(),
                metaPos,
                curveSummary.windowOffsetTicks,
                "b"
            );
            metaPos = std::strlen(data.meta.data());
        } else if (!curveSummary.wraps) {
            std::snprintf(
                data.meta.data() + metaPos,
                data.meta.size() - metaPos,
                " %uP",
                static_cast<unsigned>(slot->automation.pointCount)
            );
            metaPos = std::strlen(data.meta.data());
        }
        if (curveSummary.wraps) {
            appendText(data.meta.data(), data.meta.size(), metaPos, " Loop");
        }
    } else {
        std::snprintf(data.valueBuffers[1].data(), data.valueBuffers[1].size(), "%s", "-");
        std::snprintf(data.valueBuffers[2].data(), data.valueBuffers[2].size(), "%s", "-");
        std::snprintf(data.valueBuffers[3].data(), data.valueBuffers[3].size(), "%s", "-");
    }

    data.rows = {{
        {.key = "State", .value = data.valueBuffers[0].data()},
        {.key = "Length", .value = data.valueBuffers[1].data()},
        {.key = "Offset", .value = data.valueBuffers[2].data()},
        {
            .key = "Curve",
            .value = data.valueBuffers[3].data(),
            .sparkline = curveSparkline,
        },
    }};
    data.selectedIndex = source.macroEdit.automationFocusedRow.get();
    uint32_t revision =
        (static_cast<uint32_t>(macroIndex & 0x07U) << 29) |
        (active ? (1UL << 28) : 0U) |
        (manualOverride ? (1UL << 27) : 0U) |
        (static_cast<uint32_t>(source.macroEdit.automationFocusedRow.get() & 0x03U) << 16);
    if (active) {
        revision = mixRevision(revision, slot->automation.pointCount);
        revision = mixRevision(revision, slot->automation.durationTicks);
        revision = mixRevision(revision, slot->automation.sourceDurationTicks);
        revision = mixRevision(revision, slot->automation.windowOffsetTicks);
    }
    data.dataRevision = mixRevision(
        revision,
        source.macroUi.automationRecordingRevision.get()
    );
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
    data.title = "VALUE";
    data.meta = "CC";
    data.items = items.ccItems.data();
    data.itemCount = 128;
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

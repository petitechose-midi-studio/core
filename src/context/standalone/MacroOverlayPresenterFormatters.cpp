#include "context/standalone/MacroOverlayPresenterFormatters.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/macro/MacroSourceDetailLayout.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone::macro_overlay_presenter {

namespace {

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

FLASHMEM void formatCurveSummary(char* out,
                                 size_t outSize,
                                 const core::state::macro::MacroAutomationCurveWindowSummary& summary
) {
    if (out == nullptr || outSize == 0) return;
    out[0] = '\0';
    if (!summary.active) {
        std::snprintf(out, outSize, "%s", "-");
        return;
    }

    size_t pos = 0;
    if (summary.pointCount == 1) {
        pos = appendBeatDurationCompact(out, outSize, pos, summary.firstPointTick, "b");
    } else {
        if (summary.windowOffsetTicks > 0) {
            pos = appendText(out, outSize, pos, "+");
            pos = appendBeatDurationCompact(out, outSize, pos, summary.windowOffsetTicks, "");
            pos = appendText(out, outSize, pos, " ");
        }
        pos = appendBeatDurationCompact(out, outSize, pos, summary.firstPointTick, "");
        pos = appendText(out, outSize, pos, "-");
        pos = appendBeatDurationCompact(out, outSize, pos, summary.lastPointTick, "b");
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

FLASHMEM ms::ui::KeyValueSparkline buildModulationSparkline(
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
    sparkline.centerLine = true;
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
            static_cast<float>(
                core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
            );
        const float value = core::state::macro::macroModulationEvaluate(
            curve,
            pool,
            beat
        );
        const float normalized = std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f);
        sparkline.samples[i] =
            static_cast<uint8_t>(normalized * 255.0f + 0.5f);
    }
    return sparkline;
}

FLASHMEM void formatAutomationState(
    char* out,
    size_t outSize,
    const core::state::macro::MacroAutomationSlotState* slot,
    bool manual
) {
    if (out == nullptr || outSize == 0) return;
    if (slot == nullptr ||
        !core::state::macro::macroCurveStored(slot->automation)) {
        std::snprintf(out, outSize, "%s", "Off");
        return;
    }
    if (!core::state::macro::macroCurvePlaybackActive(slot->automation)) {
        std::snprintf(out, outSize, "%s", "Stored · Off");
        return;
    }
    std::snprintf(out, outSize, "%s", manual ? "Manual · Auto on" : "On");
}

FLASHMEM void formatModulationState(
    char* out,
    size_t outSize,
    const core::state::macro::MacroAutomationSlotState* slot
) {
    if (out == nullptr || outSize == 0) return;
    if (slot == nullptr ||
        !core::state::macro::macroCurveStored(slot->modulation)) {
        std::snprintf(out, outSize, "%s", "Off");
        return;
    }
    if (!core::state::macro::macroCurvePlaybackActive(slot->modulation)) {
        std::snprintf(out, outSize, "%s", "Stored · Off");
        return;
    }
    if (slot->modulationDepth <= 0.0f) {
        std::snprintf(out, outSize, "%s", "Paused · 0%%");
        return;
    }
    std::snprintf(
        out,
        outSize,
        "On · %u%%",
        static_cast<unsigned>(slot->modulationDepth * 100.0f + 0.5f)
    );
}

FLASHMEM core::ui::macro::MacroSourceDetailContext sourceDetailContext(
    const core::state::macro::MacroAutomationSlotState* slot,
    bool manual
) {
    if (slot == nullptr) return {};
    return {
        .automationStored =
            core::state::macro::macroCurveStored(slot->automation),
        .modulationStored =
            core::state::macro::macroCurveStored(slot->modulation),
        .automationPlayback =
            core::state::macro::macroCurvePlaybackActive(slot->automation),
        .modulationPlayback =
            core::state::macro::macroCurvePlaybackActive(slot->modulation),
        .manualOverride = manual,
    };
}

FLASHMEM const char* modulationOriginLabel(
    core::state::macro::MacroModulationOrigin origin
) {
    switch (origin) {
        case core::state::macro::MacroModulationOrigin::CONVERTED_MEAN:
            return "From mean";
        case core::state::macro::MacroModulationOrigin::CONVERTED_FIRST:
            return "From first";
        case core::state::macro::MacroModulationOrigin::CONVERTED_MIN:
            return "From min";
        case core::state::macro::MacroModulationOrigin::NATIVE:
        default:
            return "Native";
    }
}

FLASHMEM const char* conversionPolicyLabel(
    core::state::macro::MacroAutomationConversionPolicy policy
) {
    switch (policy) {
        case core::state::macro::MacroAutomationConversionPolicy::FIRST:
            return "First";
        case core::state::macro::MacroAutomationConversionPolicy::MIN:
            return "Min";
        case core::state::macro::MacroAutomationConversionPolicy::MEAN:
        default:
            return "Mean";
    }
}

FLASHMEM const char* winnerClassLabel(
    core::state::shared::MidiCcCandidateClass candidateClass
) {
    switch (candidateClass) {
        case core::state::shared::MidiCcCandidateClass::LIVE_MANUAL:
            return "Live";
        case core::state::shared::MidiCcCandidateClass::SEQUENCER_CC_LANE:
            return "CC lane";
        case core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED:
            return "Macro";
        case core::state::shared::MidiCcCandidateClass::MACRO_STATIC:
        default:
            return "Static";
    }
}

FLASHMEM bool formatConflict(
    char* out,
    size_t outSize,
    const Source& source,
    uint8_t macroIndex,
    bool computed,
    bool manual
) {
    if (out == nullptr || outSize == 0) return false;
    out[0] = '\0';
    if (source.midiCcCoordinator == nullptr) return false;
    auto telemetry = source.midiCcCoordinator->readTelemetry();
    if (!telemetry) return false;
    const uint16_t address = static_cast<uint16_t>(
        (static_cast<uint16_t>(source.pages.currentActiveTrack()) *
             core::state::macro::PAGE_COUNT +
         source.pages.currentActivePage()) *
            core::state::macro::MACRO_COUNT +
        macroIndex
    );
    const auto expectedClass = manual
        ? core::state::shared::MidiCcCandidateClass::LIVE_MANUAL
        : (computed
               ? core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED
               : core::state::shared::MidiCcCandidateClass::MACRO_STATIC);
    const size_t destinationCount = std::min<size_t>(
        telemetry->destinationCount,
        telemetry->destinations.size()
    );
    const size_t loserCount = std::min<size_t>(
        telemetry->loserCount,
        telemetry->losers.size()
    );
    for (size_t i = 0; i < destinationCount; ++i) {
        const auto& destination = telemetry->destinations[i];
        const bool localWinner =
            destination.winner.author.candidateClass == expectedClass &&
            destination.winner.author.stableAddress == address;
        bool localLoser = false;
        const size_t firstLoser = destination.firstLoser;
        const size_t localLoserCount = firstLoser < loserCount
            ? std::min<size_t>(destination.loserCount, loserCount - firstLoser)
            : 0;
        for (size_t loser = 0; loser < localLoserCount; ++loser) {
            const auto& candidate = telemetry->losers[firstLoser + loser];
            if (candidate.author.candidateClass == expectedClass &&
                candidate.author.stableAddress == address) {
                localLoser = true;
                break;
            }
        }
        if (!localWinner && !localLoser) continue;
        if (!destination.conflict) continue;
        if (localWinner) {
            std::snprintf(out, outSize, "%s", "Winner · Local");
        } else {
            std::snprintf(
                out,
                outSize,
                "Shadowed · %s",
                winnerClassLabel(destination.winner.author.candidateClass)
            );
        }
        return true;
    }
    return false;
}

FLASHMEM core::state::macro::MacroAutomationSlotAddress currentAddress(
    const Source& source
) {
    return {
        .track = source.pages.currentActiveTrack(),
        .page = source.pages.currentActivePage(),
        .macro = source.macroEdit.editingIndex.get(),
    };
}

FLASHMEM core::ui::ContextActionStripSlotProps scopeLabel(const char* label) {
    return {
        .visualState = core::ui::ContextActionStripVisualState::DIM,
        .tone = core::ui::ContextActionStripTone::NEUTRAL,
        .showIcon = false,
        .showLabel = true,
        .label = label,
    };
}

FLASHMEM void projectGuardedAction(
    core::ui::ContextActionStripSlotProps& slot,
    const Source& source,
    core::state::MacroContextButton button
) {
    using Feedback = core::state::contextual::OperationFeedbackStatus;
    using Visual = core::ui::ContextActionStripVisualState;
    if (source.macroEdit.contextButton.get() != button) return;
    const auto feedback = source.macroEdit.contextFeedback.get();
    if (!feedback.active) return;

    if (feedback.action == core::state::contextual::ContextActionId::PASTE) {
        slot.icon = ::standalone::icons::ACTION_PASTE;
        slot.tone = core::ui::ContextActionStripTone::CONSTRUCTIVE;
    } else if (feedback.action ==
               core::state::contextual::ContextActionId::OVERWRITE) {
        slot.icon = ::standalone::icons::ACTION_OVERWRITE;
        slot.tone = core::ui::ContextActionStripTone::WARNING;
    } else if (feedback.action == core::state::contextual::ContextActionId::REMOVE) {
        slot.icon = ::standalone::icons::ACTION_REMOVE;
        slot.tone = core::ui::ContextActionStripTone::DESTRUCTIVE;
    } else if (feedback.action == core::state::contextual::ContextActionId::CLEAR) {
        slot.icon = ::standalone::icons::ACTION_CLEAR;
        slot.tone = core::ui::ContextActionStripTone::DESTRUCTIVE;
    }

    switch (feedback.status) {
        case Feedback::PRESSED:
            slot.visualState = Visual::PRESSED;
            break;
        case Feedback::ARMED: {
            slot.visualState = Visual::ARMED;
            const auto guard = source.macroEdit.contextGuard.get();
            slot.holdActive = true;
            slot.holdStartedAtMs = guard.armedAtMs;
            slot.holdDurationMs = guard.guardDurationMs;
            break;
        }
        case Feedback::APPLIED:
            slot.visualState = Visual::APPLIED;
            slot.icon = ::standalone::icons::ACTION_VALIDATE;
            slot.tone = core::ui::ContextActionStripTone::POSITIVE;
            break;
        case Feedback::CANCELLED:
            slot.visualState = Visual::CANCELLED;
            slot.icon = ::standalone::icons::ACTION_CANCEL;
            break;
        case Feedback::FAILED:
        case Feedback::BLOCKED:
            slot.visualState = Visual::DISABLED;
            slot.icon = ::standalone::icons::STATUS_ERROR;
            break;
        default:
            break;
    }
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

FLASHMEM void buildEditRenderData(Source& source, EditRenderData& data) {
    const uint8_t macroIndex = source.macroEdit.editingIndex.get();
    const uint8_t cc = source.macroEdit.tempCC.get();
    size_t titlePos = oc::type::text::appendString(
        data.title.data(), data.title.size(), 0, "Macro "
    );
    titlePos = oc::type::text::appendUnsigned(
        data.title.data(),
        data.title.size(),
        titlePos,
        static_cast<unsigned>(macroIndex) + 1U
    );
    oc::type::text::terminate(data.title.data(), data.title.size(), titlePos);

    const unsigned page1 =
        static_cast<unsigned>(source.pages.currentActivePage()) + 1U;
    size_t metaPos = oc::type::text::appendString(
        data.meta.data(), data.meta.size(), 0, "Slot · Page "
    );
    metaPos = oc::type::text::appendUnsigned(data.meta.data(), data.meta.size(), metaPos, page1);
    oc::type::text::terminate(data.meta.data(), data.meta.size(), metaPos);

    std::snprintf(
        data.valueBuffers[0].data(),
        data.valueBuffers[0].size(),
        "Ch%u · CC%u",
        static_cast<unsigned>(source.pages.activeConfigs[macroIndex].channel) + 1U,
        static_cast<unsigned>(cc)
    );
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = source.pages.currentActiveTrack(),
        .page = source.pages.currentActivePage(),
        .macro = macroIndex,
    };
    const auto* slot = core::state::macro::macroAutomationFindSlot(
        source.pages.automation,
        address
    );
    const bool manualOverride =
        (source.macroUi.automationManualOverrideMask.get() &
         static_cast<uint16_t>(1U << macroIndex)) != 0;
    const bool automationStored = slot != nullptr &&
        core::state::macro::macroCurveStored(slot->automation);
    const bool modulationStored = slot != nullptr &&
        core::state::macro::macroCurveStored(slot->modulation);
    uint32_t baseBits = 0;
    std::memcpy(
        &baseBits,
        &source.pages.activePageData().values[macroIndex],
        sizeof(baseBits)
    );
    uint32_t previewRevision = mixRevision(
        source.configRevision.get(),
        static_cast<uint32_t>(macroIndex) |
            (static_cast<uint32_t>(source.pages.currentActivePage()) << 8U) |
            (static_cast<uint32_t>(source.pages.currentActiveTrack()) << 16U) |
            (manualOverride ? (1UL << 24U) : 0U)
    );
    previewRevision = mixRevision(previewRevision, baseBits);
    previewRevision = mixRevision(
        previewRevision,
        source.macroUi.automationRecordingRevision.get()
    );
    if (data.previewRevision != previewRevision) {
        core::ui::buildMacroEditorPreviewModel(
            source.pages.activePageData().values[macroIndex],
            slot,
            source.pages.automation.pointPool,
            manualOverride,
            data.preview
        );
        data.previewRevision = previewRevision;
    }
    formatAutomationState(
        data.valueBuffers[1].data(),
        data.valueBuffers[1].size(),
        slot,
        manualOverride
    );
    formatModulationState(
        data.valueBuffers[2].data(),
        data.valueBuffers[2].size(),
        slot
    );

    data.rows = {{
        {
            .key = "Destination",
            .value = data.valueBuffers[0].data(),
            .icon = ::standalone::icons::MIDI_CC,
            .iconFont = standalone_fonts.icons_14,
            .iconColor = ::standalone::theme::color::MACRO_CC_COLOR,
        },
        {
            .key = "Automation",
            .value = data.valueBuffers[1].data(),
            .icon = ::standalone::icons::MACRO_AUTOMATION,
            .iconFont = standalone_fonts.icons_14,
            .iconColor = ::standalone::theme::color::MACRO_AUTOMATION,
        },
        {
            .key = "Modulation",
            .value = data.valueBuffers[2].data(),
            .icon = ::standalone::icons::MACRO_MODULATION,
            .iconFont = standalone_fonts.icons_14,
            .iconColor = ::standalone::theme::color::MACRO_MODULATION,
        },
        {},
    }};
    data.selectedIndex = source.macroEdit.focusedRow.get();
    uint32_t revision =
        (static_cast<uint32_t>(macroIndex & 0x07U) << 29) |
        (automationStored ? (1UL << 28) : 0U) |
        (modulationStored ? (1UL << 26) : 0U) |
        (manualOverride ? (1UL << 27) : 0U) |
        (static_cast<uint32_t>(cc & 0x7FU) << 20) |
        (static_cast<uint32_t>(source.pages.currentActivePage() & 0x0FU) << 16) |
        (static_cast<uint32_t>(source.macroEdit.focusedRow.get() & 0x03U) << 14);
    if (slot != nullptr) {
        revision = mixRevision(revision, slot->automation.pointCount);
        revision = mixRevision(revision, slot->modulation.pointCount);
        revision = mixRevision(
            revision,
            static_cast<uint32_t>(slot->automation.playbackState)
        );
        revision = mixRevision(
            revision,
            static_cast<uint32_t>(slot->modulation.playbackState)
        );
        uint32_t depthBits = 0;
        std::memcpy(&depthBits, &slot->modulationDepth, sizeof(depthBits));
        revision = mixRevision(revision, depthBits);
    }
    revision = mixRevision(revision, baseBits);
    data.dataRevision = mixRevision(
        mixRevision(
            revision,
            source.macroUi.automationRecordingRevision.get()
        ),
        data.previewRevision
    );

}

FLASHMEM EditRenderData buildEditRenderData(Source& source) {
    EditRenderData data{};
    buildEditRenderData(source, data);
    return data;
}

FLASHMEM AutomationRenderData buildAutomationRenderData(const Source& source) {
    AutomationRenderData data{};
    if (!source.macroEdit.automationVisible.get()) {
        return data;
    }

    const uint8_t macroIndex = source.macroEdit.editingIndex.get();
    const auto phase = source.macroEdit.flowPhase.get();
    size_t titlePos = oc::type::text::appendString(
        data.title.data(), data.title.size(), 0,
        phase == core::state::MacroEditFlowPhase::CONVERT_PREVIEW
            ? "Convert · Macro "
            : "Macro "
    );
    titlePos = oc::type::text::appendUnsigned(
        data.title.data(),
        data.title.size(),
        titlePos,
        static_cast<unsigned>(macroIndex) + 1U
    );
    oc::type::text::terminate(data.title.data(), data.title.size(), titlePos);
    std::snprintf(
        data.meta.data(), data.meta.size(), "%s",
        phase == core::state::MacroEditFlowPhase::AUTOMATION
            ? "Automation"
            : (phase == core::state::MacroEditFlowPhase::MODULATION
                   ? "Modulation"
                   : "Silent preview")
    );

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = source.pages.currentActiveTrack(),
        .page = source.pages.currentActivePage(),
        .macro = macroIndex,
    };
    const auto* slot = core::state::macro::macroAutomationFindSlot(
        source.pages.automation,
        address
    );
    const bool automationStored = slot != nullptr &&
        core::state::macro::macroCurveStored(slot->automation);
    const bool modulationStored = slot != nullptr &&
        core::state::macro::macroCurveStored(slot->modulation);
    const bool manualOverride =
        (source.macroUi.automationManualOverrideMask.get() &
         static_cast<uint16_t>(1U << macroIndex)) != 0;

    if (phase == core::state::MacroEditFlowPhase::CONVERT_PREVIEW) {
        const auto& plan = source.macroEdit.conversionPreview.plan;
        std::snprintf(data.valueBuffers[0].data(), data.valueBuffers[0].size(), "%s", conversionPolicyLabel(plan.policy));
        std::snprintf(data.valueBuffers[1].data(), data.valueBuffers[1].size(), "%u%%", static_cast<unsigned>(plan.reference * 100.0f + 0.5f));
        std::snprintf(data.valueBuffers[2].data(), data.valueBuffers[2].size(), "%s", "Stored · Off");
        std::snprintf(data.valueBuffers[3].data(), data.valueBuffers[3].size(), "%s", "On · 100%");
        std::snprintf(
            data.valueBuffers[4].data(), data.valueBuffers[4].size(), "%s",
            plan.status == core::state::macro::MacroAutomationConversionStatus::READY
                ? "Tap apply"
                : (plan.status == core::state::macro::MacroAutomationConversionStatus::OVERWRITE_REQUIRED
                       ? "Hold overwrite"
                       : "Stale")
        );
        data.rows = {{
            {.key = "Policy", .value = data.valueBuffers[0].data(), .icon = ::standalone::icons::STATUS_PREVIEW, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
            {.key = "Reference", .value = data.valueBuffers[1].data(), .icon = ::standalone::icons::KNOB, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_CC_COLOR},
            {.key = "Automation", .value = data.valueBuffers[2].data(), .icon = ::standalone::icons::MACRO_AUTOMATION, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_AUTOMATION},
            {.key = "Modulation", .value = data.valueBuffers[3].data(), .icon = ::standalone::icons::MACRO_MODULATION, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
            {.key = "Impact", .value = data.valueBuffers[4].data(), .icon = plan.overwritesModulation ? ::standalone::icons::ACTION_OVERWRITE : ::standalone::icons::ACTION_APPLY, .iconFont = standalone_fonts.icons_14, .iconColor = plan.overwritesModulation ? ::standalone::theme::color::MACRO_CONFLICT : ::standalone::theme::color::MACRO_CC_COLOR},
            {},
            {},
        }};
        data.selectedIndex = 0;
        data.rowCount = 5;
        data.dataRevision = source.macroEdit.conversionPreview.revision.get();
        data.visible = true;
        return data;
    }

    const auto detailContext = sourceDetailContext(slot, manualOverride);
    if (phase == core::state::MacroEditFlowPhase::AUTOMATION) {
        formatAutomationState(
            data.valueBuffers[0].data(),
            data.valueBuffers[0].size(),
            slot,
            manualOverride
        );
        ms::ui::KeyValueSparkline curveSparkline{};
        if (automationStored) {
            const auto curveSummary =
                core::state::macro::macroAutomationCurveWindowSummary(
                    slot->automation,
                    source.pages.automation.pointPool
                );
            curveSparkline = buildCurveSparkline(
                slot->automation,
                source.pages.automation.pointPool
            );
            formatBeatDuration(
                data.valueBuffers[2].data(),
                data.valueBuffers[2].size(),
                slot->automation.durationTicks,
                " beats"
            );
            formatBeatDuration(
                data.valueBuffers[3].data(),
                data.valueBuffers[3].size(),
                slot->automation.windowOffsetTicks,
                " beats"
            );
            formatCurveSummary(
                data.valueBuffers[4].data(),
                data.valueBuffers[4].size(),
                curveSummary
            );
        }
        std::snprintf(
            data.valueBuffers[1].data(),
            data.valueBuffers[1].size(),
            "%s",
            "Preview impact"
        );
        std::snprintf(
            data.valueBuffers[5].data(),
            data.valueBuffers[5].size(),
            "%s",
            "Resume Auto"
        );

        const auto layout =
            core::ui::macro::buildAutomationDetailLayout(detailContext);
        for (uint8_t i = 0; i < layout.count; ++i) {
            ms::ui::KeyValueRow row{};
            switch (layout.items[i]) {
                case core::ui::macro::AutomationDetailItem::PLAYBACK:
                    row = {.key = "Playback", .value = data.valueBuffers[0].data(), .icon = ::standalone::icons::MACRO_AUTOMATION, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_AUTOMATION};
                    break;
                case core::ui::macro::AutomationDetailItem::RESUME:
                    row = {.key = "Automation", .value = data.valueBuffers[5].data(), .icon = ::standalone::icons::STATUS_RESUME, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_AUTOMATION};
                    break;
                case core::ui::macro::AutomationDetailItem::CONVERT_TO_MODULATION:
                    row = {.key = "Convert to Mod", .value = data.valueBuffers[1].data(), .icon = ::standalone::icons::STATUS_PREVIEW, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION};
                    break;
                case core::ui::macro::AutomationDetailItem::LENGTH:
                    row = {.key = "Length", .value = data.valueBuffers[2].data(), .icon = ::standalone::icons::LENGTH, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::STEP_LENGTH};
                    break;
                case core::ui::macro::AutomationDetailItem::OFFSET:
                    row = {.key = "Offset", .value = data.valueBuffers[3].data(), .icon = ::standalone::icons::OFFSET, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::STEP_OFFSET};
                    break;
                case core::ui::macro::AutomationDetailItem::CURVE:
                    row = {.key = "Curve", .value = data.valueBuffers[4].data(), .icon = ::standalone::icons::MACRO_AUTOMATION, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_AUTOMATION, .sparkline = curveSparkline};
                    break;
            }
            data.rows[i] = row;
        }
        data.rowCount = layout.count;
        data.selectedIndex = std::min<int>(
            source.macroEdit.automationFocusedRow.get(),
            std::max(0, data.rowCount - 1)
        );
    } else {
        formatModulationState(
            data.valueBuffers[0].data(),
            data.valueBuffers[0].size(),
            slot
        );
        const bool paused = detailContext.modulationPlayback &&
            slot != nullptr && slot->modulationDepth <= 0.0f;
        std::snprintf(
            data.valueBuffers[1].data(),
            data.valueBuffers[1].size(),
            modulationStored ? "%u%%" : "-",
            modulationStored
                ? static_cast<unsigned>(slot->modulationDepth * 100.0f + 0.5f)
                : 0U
        );
        std::snprintf(
            data.valueBuffers[2].data(),
            data.valueBuffers[2].size(),
            "%s",
            "Bipolar shape"
        );
        std::snprintf(
            data.valueBuffers[3].data(),
            data.valueBuffers[3].size(),
            "%s",
            modulationStored
                ? modulationOriginLabel(slot->modulation.modulationOrigin)
                : "None"
        );
        const auto modulationSparkline = modulationStored
            ? buildModulationSparkline(
                  slot->modulation,
                  source.pages.automation.pointPool
              )
            : ms::ui::KeyValueSparkline{};
        const auto layout =
            core::ui::macro::buildModulationDetailLayout(detailContext);
        for (uint8_t i = 0; i < layout.count; ++i) {
            ms::ui::KeyValueRow row{};
            switch (layout.items[i]) {
                case core::ui::macro::ModulationDetailItem::PLAYBACK:
                    row = {.key = "Playback", .value = data.valueBuffers[0].data(), .icon = paused ? ::standalone::icons::STATUS_PAUSED : ::standalone::icons::MACRO_MODULATION, .iconFont = standalone_fonts.icons_14, .iconColor = paused ? ::standalone::theme::color::MACRO_PAUSED : ::standalone::theme::color::MACRO_MODULATION};
                    break;
                case core::ui::macro::ModulationDetailItem::DEPTH:
                    row = {.key = "Depth", .value = data.valueBuffers[1].data(), .icon = paused ? ::standalone::icons::STATUS_PAUSED : ::standalone::icons::KNOB, .iconFont = standalone_fonts.icons_14, .iconColor = paused ? ::standalone::theme::color::MACRO_PAUSED : ::standalone::theme::color::MACRO_MODULATION};
                    break;
                case core::ui::macro::ModulationDetailItem::CURVE:
                    row = {.key = "Shape", .value = data.valueBuffers[2].data(), .icon = ::standalone::icons::MACRO_MODULATION, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION, .sparkline = modulationSparkline};
                    break;
                case core::ui::macro::ModulationDetailItem::ORIGIN:
                    row = {.key = "Origin", .value = data.valueBuffers[3].data(), .icon = ::standalone::icons::STATUS_PREVIEW, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::TEXT_SECONDARY};
                    break;
            }
            data.rows[i] = row;
        }
        data.rowCount = layout.count;
        data.selectedIndex = std::min<int>(
            source.macroEdit.modulationFocusedRow.get(),
            std::max(0, data.rowCount - 1)
        );

        if (formatConflict(
                data.valueBuffers[6].data(),
                data.valueBuffers[6].size(),
                source,
                macroIndex,
                automationStored || modulationStored,
                manualOverride
            )) {
            std::snprintf(
                data.meta.data(),
                data.meta.size(),
                "Mod · %.16s",
                data.valueBuffers[6].data()
            );
        }
    }
    uint32_t revision =
        (static_cast<uint32_t>(macroIndex & 0x07U) << 29) |
        (automationStored ? (1UL << 28) : 0U) |
        (modulationStored ? (1UL << 26) : 0U) |
        (manualOverride ? (1UL << 27) : 0U) |
        (static_cast<uint32_t>(data.selectedIndex & 0x07U) << 16) |
        (static_cast<uint32_t>(phase) << 8);
    if (automationStored) {
        revision = mixRevision(revision, slot->automation.pointCount);
        revision = mixRevision(revision, slot->automation.durationTicks);
        revision = mixRevision(revision, slot->automation.sourceDurationTicks);
        revision = mixRevision(revision, slot->automation.windowOffsetTicks);
    }
    if (modulationStored) {
        revision = mixRevision(revision, slot->modulation.pointCount);
        uint32_t depthBits = 0;
        std::memcpy(&depthBits, &slot->modulationDepth, sizeof(depthBits));
        revision = mixRevision(revision, depthBits);
    }
    data.dataRevision = mixRevision(
        revision,
        source.macroUi.automationRecordingRevision.get()
    );
    data.visible = true;
    return data;
}

FLASHMEM core::ui::ContextActionStripProps buildEditActionStripProps(
    const Source& source
) {
    using Tone = core::ui::ContextActionStripTone;
    using Visual = core::ui::ContextActionStripVisualState;
    core::ui::ContextActionStripProps props{};
    if (!source.macroEdit.visible.get() ||
        source.macroEdit.flowPhase.get() != core::state::MacroEditFlowPhase::EDIT) {
        return props;
    }

    props.visible = true;
    const uint8_t row = std::min<uint8_t>(source.macroEdit.focusedRow.get(), 2U);
    const auto address = currentAddress(source);
    const auto* slot = core::state::macro::macroAutomationFindSlot(
        source.pages.automation,
        address
    );
    const bool automationStored = slot != nullptr &&
        core::state::macro::macroCurveStored(slot->automation);
    const bool modulationStored = slot != nullptr &&
        core::state::macro::macroCurveStored(slot->modulation);
    const bool automationPlayback = automationStored &&
        core::state::macro::macroCurvePlaybackActive(slot->automation);
    const bool modulationPlayback = modulationStored &&
        core::state::macro::macroCurvePlaybackActive(slot->modulation);

    if (row == 0U) {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            ::standalone::icons::ACTION_REMOVE,
            Visual::ACTIVE,
            Tone::DESTRUCTIVE
        );
        props.slots[1] = scopeLabel("Destination");
    } else {
        const bool stored = row == 1U ? automationStored : modulationStored;
        const bool playback = row == 1U ? automationPlayback : modulationPlayback;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            playback
                ? (row == 1U ? ::standalone::icons::MACRO_AUTOMATION
                             : ::standalone::icons::MACRO_MODULATION)
                : ::standalone::icons::STATUS_PAUSED,
            stored ? Visual::ACTIVE : Visual::DISABLED,
            Tone::NEUTRAL
        );
        props.slots[1] = scopeLabel(row == 1U ? "Automation" : "Modulation");
    }
    const bool canCopy = row == 0U ||
        (row == 1U ? automationStored : modulationStored);
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        ::standalone::icons::ACTION_COPY,
        canCopy ? Visual::ACTIVE : Visual::DISABLED,
        Tone::NEUTRAL
    );
    projectGuardedAction(
        props.slots[0], source, core::state::MacroContextButton::BOTTOM_LEFT
    );
    projectGuardedAction(
        props.slots[2], source, core::state::MacroContextButton::BOTTOM_RIGHT
    );
    return props;
}

FLASHMEM core::ui::ContextActionStripProps buildDetailActionStripProps(
    const Source& source
) {
    using Status = core::state::macro::MacroAutomationConversionStatus;
    using Tone = core::ui::ContextActionStripTone;
    using Visual = core::ui::ContextActionStripVisualState;
    core::ui::ContextActionStripProps props{};
    if (!source.macroEdit.automationVisible.get()) return props;

    props.visible = true;
    const auto phase = source.macroEdit.flowPhase.get();
    if (phase == core::state::MacroEditFlowPhase::CONVERT_PREVIEW) {
        props.slots[0].visualState = Visual::HIDDEN;
        props.slots[1] = scopeLabel("Preview");
        const auto status = source.macroEdit.conversionPreview.plan.status;
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            status == Status::OVERWRITE_REQUIRED
                ? ::standalone::icons::ACTION_OVERWRITE
                : ::standalone::icons::ACTION_APPLY,
            status == Status::READY || status == Status::OVERWRITE_REQUIRED
                ? Visual::ACTIVE
                : Visual::DISABLED,
            status == Status::OVERWRITE_REQUIRED ? Tone::WARNING
                                                 : Tone::CONSTRUCTIVE
        );
        projectGuardedAction(
            props.slots[2], source, core::state::MacroContextButton::BOTTOM_RIGHT
        );
        return props;
    }

    const auto address = currentAddress(source);
    const auto* slot = core::state::macro::macroAutomationFindSlot(
        source.pages.automation,
        address
    );
    const bool modulation = phase == core::state::MacroEditFlowPhase::MODULATION;
    const bool automationStored = slot != nullptr &&
        core::state::macro::macroCurveStored(slot->automation);
    const bool modulationStored = slot != nullptr &&
        core::state::macro::macroCurveStored(slot->modulation);
    const bool stored = modulation ? modulationStored : automationStored;
    const bool playback = stored && core::state::macro::macroCurvePlaybackActive(
        modulation ? slot->modulation : slot->automation
    );
    props.slots[0] = core::ui::makeStandaloneIconStripSlot(
        playback
            ? (modulation ? ::standalone::icons::MACRO_MODULATION
                          : ::standalone::icons::MACRO_AUTOMATION)
            : ::standalone::icons::STATUS_PAUSED,
        stored ? Visual::ACTIVE : Visual::DISABLED,
        Tone::NEUTRAL
    );
    props.slots[1] = scopeLabel(modulation ? "Modulation" : "Automation");
    const bool canCopy = stored;
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        ::standalone::icons::ACTION_COPY,
        canCopy ? Visual::ACTIVE : Visual::DISABLED,
        Tone::NEUTRAL
    );
    projectGuardedAction(
        props.slots[0], source, core::state::MacroContextButton::BOTTOM_LEFT
    );
    projectGuardedAction(
        props.slots[2], source, core::state::MacroContextButton::BOTTOM_RIGHT
    );
    return props;
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

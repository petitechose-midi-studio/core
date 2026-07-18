#include "context/standalone/MacroOverlayPresenterFormatters.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/macro/MacroLfoAuditionModel.hpp"
#include "ui/macro/MacroSourceDetailLayout.hpp"
#include "ui/modulation/ModulatorAdsrUiModel.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone::macro_overlay_presenter {

namespace {

namespace adsr_ui = core::ui::modulation::adsr;

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

FLASHMEM void formatModulationAssignmentSummary(
    char* out,
    size_t outSize,
    const char* name,
    int depth,
    uint16_t position,
    uint16_t count
) {
    size_t pos = oc::type::text::appendString(out, outSize, 0U, name);
    pos = oc::type::text::appendString(out, outSize, pos, "  ");
    pos = oc::type::text::appendSigned(out, outSize, pos, depth, true);
    pos = oc::type::text::appendChar(out, outSize, pos, '%');
    if (count > 1U) {
        pos = oc::type::text::appendString(out, outSize, pos, "  ");
        pos = oc::type::text::appendUnsigned(out, outSize, pos, position);
        pos = oc::type::text::appendChar(out, outSize, pos, '/');
        pos = oc::type::text::appendUnsigned(out, outSize, pos, count);
    }
    oc::type::text::terminate(out, outSize, pos);
}

FLASHMEM uint32_t mixRevision(uint32_t seed, uint32_t value) {
    seed ^= value + 0x9E3779B9UL + (seed << 6U) + (seed >> 2U);
    return seed;
}

const core::state::modulation::ModulationBindingState* bindingAtDestination(
    const core::state::modulation::ProjectModulationState& graph,
    const core::state::modulation::ModulationDestination& destination,
    uint16_t ordinal
) {
    uint16_t found = 0U;
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        if (found++ == ordinal) return &binding;
    }
    return nullptr;
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
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ProjectCurveId curveId,
    const core::state::macro::MacroAutomationCurveRef& curve,
    float fallback = 0.0f
) {
    ms::ui::KeyValueSparkline sparkline{};
    if (!curve.active || curve.pointCount == 0 ||
        !core::state::modulation::valid(curveId)) {
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
        const float value =
            core::state::modulation::evaluateProjectControlCurve(
            control,
            curveId,
            beat,
            fallback
        );
        const float clamped = core::state::macro::macroAutomationClamp01(value);
        sparkline.samples[i] = static_cast<uint8_t>(clamped * 255.0f + 0.5f);
    }
    return sparkline;
}

FLASHMEM float liveSourceValue(
    const core::state::modulation::ProjectControlState& control,
    core::state::modulation::ModulatorId sourceId
) {
    for (uint16_t index = 0; index < control.plan.sourceCount; ++index) {
        if (control.plan.sources[index].id == sourceId) {
            return std::clamp(control.sourceScratch[index], -1.0f, 1.0f);
        }
    }
    return 0.0f;
}

FLASHMEM ms::ui::KeyValueSparkline buildLfoSparkline(
    core::state::modulation::ModulatorLfoShape shape,
    float liveValue
) {
    using Shape = core::state::modulation::ModulatorLfoShape;
    ms::ui::KeyValueSparkline sparkline{};
    sparkline.enabled = true;
    sparkline.liveMarker = true;
    sparkline.sampleCount = static_cast<uint8_t>(
        ms::ui::KEY_VALUE_SPARKLINE_SAMPLE_COUNT
    );
    sparkline.liveValue = static_cast<uint8_t>(std::lround(
        std::clamp(liveValue * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f
    ));
    switch (shape) {
        case Shape::TRIANGLE:
            sparkline.samples = {{128, 179, 230, 255, 204, 153,
                                  102, 51, 0, 26, 77, 128}};
            break;
        case Shape::SAW_UP:
            sparkline.samples = {{0, 23, 46, 70, 93, 116,
                                  139, 162, 185, 209, 232, 255}};
            break;
        case Shape::SAW_DOWN:
            sparkline.samples = {{255, 232, 209, 185, 162, 139,
                                  116, 93, 70, 46, 23, 0}};
            break;
        case Shape::SQUARE:
            sparkline.samples = {{255, 255, 255, 255, 255, 255,
                                  0, 0, 0, 0, 0, 0}};
            break;
        case Shape::SINE:
        default:
            sparkline.samples = {{128, 197, 244, 255, 221, 164,
                                  91, 34, 1, 11, 58, 128}};
            break;
    }
    return sparkline;
}

FLASHMEM ms::ui::KeyValueSparkline buildAdsrSparkline(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulatorSourceState& source
) {
    using namespace core::state::modulation;
    ms::ui::KeyValueSparkline sparkline{};
    sparkline.enabled = true;
    sparkline.centerLine = false;
    sparkline.sampleCount = static_cast<uint8_t>(
        ms::ui::KEY_VALUE_SPARKLINE_SAMPLE_COUNT
    );
    const auto boundaries = adsr_ui::previewBoundaries(source.parameters.adsr);
    for (uint8_t index = 0U; index < sparkline.sampleCount; ++index) {
        const uint16_t position = static_cast<uint16_t>(
            (static_cast<uint32_t>(index) * 65535U) /
            static_cast<uint32_t>(sparkline.sampleCount - 1U)
        );
        const float value = adsr_ui::previewValue(
            source.parameters.adsr,
            boundaries,
            position
        );
        sparkline.samples[index] = static_cast<uint8_t>(std::lround(
            std::clamp(value, 0.0f, 1.0f) * 255.0f
        ));
    }
    const auto* runtime = adsr_ui::runtimeState(control, source.id);
    sparkline.liveMarker =
        (source.flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U &&
        runtime != nullptr && runtime->stage != ProjectModulationAdsrStage::IDLE;
    sparkline.liveValue = static_cast<uint8_t>(std::lround(
        std::clamp(liveSourceValue(control, source.id), 0.0f, 1.0f) * 255.0f
    ));
    return sparkline;
}

FLASHMEM uint16_t sourceUsageCount(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId
) {
    uint16_t count = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        if (graph.outputBindings[index].sourceId == sourceId) ++count;
    }
    return count;
}

FLASHMEM bool sourceAssignedTo(
    const core::state::modulation::ProjectModulationState& graph,
    core::state::modulation::ModulatorId sourceId,
    const core::state::modulation::ModulationDestination& destination
) {
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.sourceId == sourceId &&
            binding.destination == destination) {
            return true;
        }
    }
    return false;
}

FLASHMEM const char* lfoRateCompact(uint8_t index) {
    return core::ui::macro::lfo_audition::rateCompactLabel(index);
}

FLASHMEM ms::ui::KeyValueSparkline buildSourceSparkline(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulatorSourceState& source
) {
    if (source.kind == core::state::modulation::ModulatorKind::LFO) {
        return buildLfoSparkline(
            source.parameters.lfo.shape,
            liveSourceValue(control, source.id)
        );
    }
    if (source.kind == core::state::modulation::ModulatorKind::ADSR) {
        return buildAdsrSparkline(control, source);
    }
    const auto curveId = source.parameters.recordedCurveId;
    const auto* record = core::state::modulation::findProjectCurve(
        control.authored.curves,
        curveId
    );
    if (record == nullptr) return {};
    core::state::macro::MacroAutomationCurveRef curve{};
    curve.active = true;
    curve.pointOffset = record->pointOffset;
    curve.pointCount = record->pointCount;
    curve.sourceDurationTicks = record->sourceDurationTicks;
    curve.durationTicks = record->durationTicks;
    curve.windowOffsetTicks = record->windowOffsetTicks;
    auto sparkline = buildCurveSparkline(control, curveId, curve);
    sparkline.centerLine = true;
    sparkline.liveMarker = true;
    sparkline.liveValue = static_cast<uint8_t>(std::lround(
        std::clamp(
            liveSourceValue(control, source.id) * 0.5f + 0.5f,
            0.0f,
            1.0f
        ) * 255.0f
    ));
    return sparkline;
}

FLASHMEM void provideModulatorPickerRow(
    void* context,
    int index,
    ms::ui::KeyValueRowBuffer& out
) {
    auto* source = static_cast<Source*>(context);
    if (source == nullptr || index < 0) return;
    const auto& graph = source->pages.control.authored.modulation;
    if (index >= static_cast<int>(graph.sourceCount)) return;
    const auto& modulator = graph.sources[static_cast<uint16_t>(index)];
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = source->pages.currentActiveTrack(),
        .page = source->pages.currentActivePage(),
        .macro = source->macroEdit.editingIndex.get(),
    };
    const auto destination =
        core::state::modulation::projectControlDestination(address);
    const bool assigned = sourceAssignedTo(graph, modulator.id, destination);
    const uint16_t usage = sourceUsageCount(graph, modulator.id);
    const char* primary = "Motion";
    if (modulator.kind == core::state::modulation::ModulatorKind::LFO) {
        primary = lfoRateCompact(
            core::ui::macro::lfo_audition::rateIndex(
                modulator.parameters.lfo.periodTicks
            )
        );
    } else if (modulator.kind ==
               core::state::modulation::ModulatorKind::ADSR) {
        primary = "ADSR";
    }
    out.sparkline = buildSourceSparkline(source->pages.control, modulator);
    std::snprintf(out.key.data(), out.key.size(), "%s", modulator.name.data());
    if (assigned) {
        std::snprintf(
            out.value.data(),
            out.value.size(),
            "Assigned · Used %u",
            static_cast<unsigned>(usage)
        );
    } else {
        std::snprintf(
            out.value.data(),
            out.value.size(),
            "%s · Used by %u",
            primary,
            static_cast<unsigned>(usage)
        );
    }
    std::snprintf(
        out.icon.data(),
        out.icon.size(),
        "%s",
        modulator.kind == core::state::modulation::ModulatorKind::LFO
            ? ::standalone::icons::MACRO_MODULATION
            : (modulator.kind == core::state::modulation::ModulatorKind::ADSR
                ? ::standalone::icons::NOTE_PROP_GATE
                : ::standalone::icons::MACRO_AUTOMATION)
    );
    out.iconFont = standalone_fonts.icons_14;
    const bool enabled =
        (modulator.flags &
         core::state::modulation::PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
    out.iconColor = enabled
        ? ::standalone::theme::color::MACRO_MODULATION
        : ::standalone::theme::color::TEXT_SECONDARY;
}

FLASHMEM void provideModulationAssignmentRow(
    void* context,
    int rowIndex,
    ms::ui::KeyValueRowBuffer& out
) {
    auto* source = static_cast<Source*>(context);
    if (source == nullptr || rowIndex < 0) return;
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = source->pages.currentActiveTrack(),
        .page = source->pages.currentActivePage(),
        .macro = source->macroEdit.editingIndex.get(),
    };
    const auto destination =
        core::state::modulation::projectControlDestination(address);
    const auto& graph = source->pages.control.authored.modulation;
    uint16_t count = 0;
    uint16_t enabledCount = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        ++count;
        if ((binding.flags & core::state::modulation::
                PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U) {
            ++enabledCount;
        }
    }
    if (count == 0U) return;
    const int firstAssignmentRow = 1;
    const int addSourceRow = firstAssignmentRow + static_cast<int>(count);
    if (rowIndex == 0) {
        const uint16_t scaleQ15 =
            core::state::modulation::projectModulationDestinationScaleQ15(
                graph,
                destination
            );
        const unsigned depthPercent = static_cast<unsigned>(std::lround(
            static_cast<float>(scaleQ15) * 100.0f /
            static_cast<float>(core::state::modulation::
                PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15)
        ));
        std::snprintf(out.key.data(), out.key.size(), "%s", "All");
        std::snprintf(
            out.value.data(),
            out.value.size(),
            "%u%%  %s",
            depthPercent,
            enabledCount == 0U ? "Off"
                : (enabledCount == count ? "On" : "Mixed")
        );
        std::snprintf(
            out.icon.data(),
            out.icon.size(),
            "%s",
            enabledCount > 0U ? ::standalone::icons::MACRO_MODULATION
                              : ::standalone::icons::STATUS_PAUSED
        );
        out.iconFont = standalone_fonts.icons_14;
        out.iconColor = enabledCount > 0U
            ? ::standalone::theme::color::MACRO_MODULATION
            : ::standalone::theme::color::TEXT_SECONDARY;
        return;
    }
    if (rowIndex == addSourceRow) {
        std::snprintf(out.key.data(), out.key.size(), "%s", "+ Source");
        std::snprintf(out.value.data(), out.value.size(), "%s", "Add");
        std::snprintf(
            out.icon.data(),
            out.icon.size(),
            "%s",
            ::standalone::icons::ACTION_PLACE_TARGET
        );
        out.iconFont = standalone_fonts.icons_14;
        out.iconColor = ::standalone::theme::color::MACRO_MODULATION;
        return;
    }

    const int targetOrdinal = rowIndex - firstAssignmentRow;
    if (targetOrdinal < 0) return;
    int ordinal = 0;
    const core::state::modulation::ModulationBindingState* selected = nullptr;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        if (ordinal++ == targetOrdinal) {
            selected = &binding;
            break;
        }
    }
    if (selected == nullptr) return;
    const auto* modulator = core::state::modulation::findProjectModulator(
        graph,
        selected->sourceId
    );
    if (modulator == nullptr) return;
    const bool edgeEnabled =
        (selected->flags & core::state::modulation::
            PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
    const bool sourceEnabled =
        (modulator->flags & core::state::modulation::
            PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
    const int depth = static_cast<int>(std::lround(
        std::clamp(
            static_cast<float>(selected->amountQ15) / 32767.0f,
            -1.0f,
            1.0f
        ) * 100.0f
    ));
    std::snprintf(out.key.data(), out.key.size(), "%s", modulator->name.data());
    std::snprintf(
        out.value.data(),
        out.value.size(),
        edgeEnabled ? "%+d%%" : "%+d%% Off",
        depth
    );
    std::snprintf(
        out.icon.data(),
        out.icon.size(),
        "%s",
        modulator->kind == core::state::modulation::ModulatorKind::LFO
            ? ::standalone::icons::MACRO_MODULATION
            : ::standalone::icons::MACRO_AUTOMATION
    );
    out.iconFont = standalone_fonts.icons_14;
    out.iconColor = edgeEnabled && sourceEnabled
        ? ::standalone::theme::color::MACRO_MODULATION
        : ::standalone::theme::color::TEXT_SECONDARY;
}

FLASHMEM ms::ui::KeyValueSparkline buildModulationSparkline(
    const core::ui::MacroEditorPreviewModel& preview
) {
    ms::ui::KeyValueSparkline sparkline{};
    if (!preview.modulationStored) {
        return sparkline;
    }

    constexpr uint8_t sampleCount =
        static_cast<uint8_t>(ms::ui::KEY_VALUE_SPARKLINE_SAMPLE_COUNT);
    sparkline.enabled = true;
    sparkline.centerLine = true;
    sparkline.sampleCount = sampleCount;
    for (uint8_t i = 0; i < sampleCount; ++i) {
        const uint16_t positionQ16 = sampleCount > 1U
            ? static_cast<uint16_t>(
                (static_cast<uint32_t>(i) * 65535U) /
                static_cast<uint32_t>(sampleCount - 1U)
            )
            : 0U;
        const uint16_t previousPositionQ16 = i > 0U
            ? static_cast<uint16_t>(
                (static_cast<uint32_t>(i - 1U) * 65535U) /
                static_cast<uint32_t>(sampleCount - 1U)
            )
            : 0U;
        core::ui::MacroEditorPreviewSample sample{};
        if (!core::ui::sampleMacroEditorPreview(
                preview,
                core::ui::MacroEditorPreviewFocus::ALL_MODULATION,
                positionQ16,
                previousPositionQ16,
                i > 0U,
                sample
            )) {
            return {};
        }
        const float value = std::clamp(
            static_cast<float>(sample.modulationQ15) / 32767.0f,
            -1.0f,
            1.0f
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
    const core::state::modulation::ProjectControlMacroSlotView* slot,
    bool manual
) {
    if (out == nullptr || outSize == 0) return;
    if (slot == nullptr || !slot->automationStored) {
        std::snprintf(out, outSize, "%s", "Off");
        return;
    }
    if (!slot->automationEnabled) {
        std::snprintf(out, outSize, "%s", "Stored · Off");
        return;
    }
    std::snprintf(out, outSize, "%s", manual ? "Manual · Auto on" : "On");
}

FLASHMEM void formatModulationState(
    char* out,
    size_t outSize,
    const core::state::modulation::ProjectControlMacroSlotView* slot
) {
    if (out == nullptr || outSize == 0) return;
    if (slot == nullptr || !slot->modulationStored) {
        std::snprintf(out, outSize, "%s", "Off");
        return;
    }
    if (slot->modulationCount > 1U) {
        std::snprintf(
            out,
            outSize,
            "%u sources · %s",
            static_cast<unsigned>(slot->modulationCount),
            slot->activeModulationCount > 0U ? "On" : "Off"
        );
        return;
    }
    if (!slot->modulationEnabled) {
        std::snprintf(out, outSize, "%s", "Stored · Off");
        return;
    }
    const int amount = static_cast<int>(std::lround(
        std::clamp(slot->compatibility.modulationDepth, -1.0f, 1.0f) * 100.0f
    ));
    if (amount == 0) {
        std::snprintf(out, outSize, "%s", "Paused · 0%%");
        return;
    }
    std::snprintf(
        out,
        outSize,
        "On · %d%%",
        amount
    );
}

FLASHMEM core::ui::macro::MacroSourceDetailContext sourceDetailContext(
    const core::state::modulation::ProjectControlMacroSlotView* slot,
    bool manual
) {
    if (slot == nullptr) return {};
    return {
        .automationStored = slot->automationStored,
        .modulationStored = slot->modulationStored,
        .automationPlayback = slot->automationEnabled,
        .modulationPlayback = slot->activeModulationCount > 0U,
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
    core::state::modulation::ProjectControlMacroSlotView slotView{};
    const auto* slot = core::state::modulation::readProjectControlMacroSlot(
        source.pages.control,
        address,
        slotView
    ) ? &slotView : nullptr;
    const bool manualOverride =
        (source.macroUi.automationManualOverrideMask.get() &
         static_cast<uint16_t>(1U << macroIndex)) != 0;
    const bool automationStored = slot != nullptr && slot->automationStored;
    const bool modulationStored = slot != nullptr && slot->modulationStored;
    core::state::modulation::ModulationBindingId focusedBindingId{};
    const core::state::modulation::ModulationBindingState* focusedBinding =
        nullptr;
    if (slot != nullptr && slot->modulationStored) {
        focusedBindingId =
            core::state::modulation::projectControlFocusedModulationBinding(
                source.pages.control,
                address
            );
        focusedBinding = core::state::modulation::findProjectModulationBinding(
            source.pages.control.authored.modulation,
            focusedBindingId
        );
    }
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
    previewRevision = mixRevision(
        previewRevision,
        source.pages.control.authoredRevision
    );
    previewRevision = mixRevision(previewRevision, focusedBindingId.value);
    if (data.previewRevision != previewRevision) {
        core::ui::buildMacroEditorPreviewModel(
            source.pages.activePageData().values[macroIndex],
            source.pages.control,
            address,
            manualOverride,
            focusedBindingId,
            {},
            data.preview
        );
        data.previewRevision = previewRevision;
    }
    data.preview.live = {};
    if (source.macroUi.runtimeProjectionValidFor(
            address.track,
            address.page,
            address.macro
        )) {
        const auto& projection = source.macroUi.runtimeProjections[macroIndex];
        data.preview.live = {
            .base = projection.base,
            .modulation = projection.modulation,
            .out = projection.resolved,
            .timestampMs = source.pages.control.runtime.lastEvaluationMs,
            .valid = projection.valid,
            .clippedLow = projection.clippedLow,
            .clippedHigh = projection.clippedHigh,
        };
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
    if (slot != nullptr && focusedBinding != nullptr) {
        const auto* modulator = core::state::modulation::findProjectModulator(
            source.pages.control.authored.modulation,
            focusedBinding->sourceId
        );
        if (modulator != nullptr) {
            const int depth = static_cast<int>(std::lround(
                std::clamp(
                    static_cast<float>(focusedBinding->amountQ15) / 32767.0f,
                    -1.0f,
                    1.0f
                ) * 100.0f
            ));
            uint16_t position = 1U;
            if (slot->modulationCount > 1U) {
                const auto& graph = source.pages.control.authored.modulation;
                for (uint16_t index = 0;
                     index < graph.outputBindingCount;
                     ++index) {
                    const auto& candidate = graph.outputBindings[index];
                    if (candidate.destination == focusedBinding->destination &&
                        candidate.id.value < focusedBinding->id.value) {
                        ++position;
                    }
                }
            }
            formatModulationAssignmentSummary(
                data.valueBuffers[2].data(),
                data.valueBuffers[2].size(),
                modulator->name.data(),
                depth,
                position,
                slot->modulationCount
            );
        }
    }
    if (source.macroEdit.focusedRow.get() == 0U) {
        std::snprintf(data.meta.data(), data.meta.size(), "Live · 2s");
    } else if (source.macroEdit.focusedRow.get() == 1U) {
        const unsigned beats = std::max<unsigned>(
            1U,
            static_cast<unsigned>(data.preview.automationDurationTicks) /
                core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
        );
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "Loop · %ub",
            beats
        );
    } else if (focusedBinding != nullptr) {
        const auto* focusedSource =
            core::state::modulation::findProjectModulator(
                source.pages.control.authored.modulation,
                focusedBinding->sourceId
            );
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "%s · %.10s",
            focusedSource != nullptr && focusedSource->kind ==
                    core::state::modulation::ModulatorKind::ADSR
                ? "Envelope"
                : "Cycle",
            focusedSource != nullptr ? focusedSource->name.data() : "Source"
        );
    } else {
        std::snprintf(data.meta.data(), data.meta.size(), "Cycle · None");
    }

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

    const bool macroCycle = source.macroEdit.macroCycleActive.get();
    const bool contextSelector = source.macroEdit.contextSelectorActive.get();
    data.interactionOverlayVisible = macroCycle || contextSelector;
    if (macroCycle) {
        data.interactionIcon = ::standalone::icons::KNOB;
        data.interactionColor = ::standalone::theme::color::getMacroColor(
            macroIndex
        );
        std::snprintf(
            data.interactionLabel.data(),
            data.interactionLabel.size(),
            "ACTIVE MACRO"
        );
        std::snprintf(
            data.interactionValue.data(),
            data.interactionValue.size(),
            "MACRO %u",
            static_cast<unsigned>(macroIndex) + 1U
        );
    } else if (contextSelector) {
        const uint8_t row = std::min<uint8_t>(
            source.macroEdit.focusedRow.get(), 2U
        );
        const uint8_t property = source.macroEdit.contextPropertyIndex.get();
        if (row == 0U) {
            const bool channel = property == 1U;
            data.interactionIcon = channel
                ? ::standalone::icons::MIDI_CHANNEL
                : ::standalone::icons::MIDI_CC;
            data.interactionColor = channel
                ? ::standalone::theme::color::MACRO_CH_COLOR
                : ::standalone::theme::color::MACRO_CC_COLOR;
            std::snprintf(
                data.interactionLabel.data(),
                data.interactionLabel.size(),
                "%s",
                channel ? "TRACK CHANNEL" : "CC NUMBER"
            );
            std::snprintf(
                data.interactionValue.data(),
                data.interactionValue.size(),
                channel ? "CHANNEL %u" : "CC %u",
                static_cast<unsigned>(
                    channel
                        ? source.pages.activeConfigs[macroIndex].channel + 1U
                        : source.macroEdit.tempCC.get()
                )
            );
        } else if (row == 1U) {
            const uint8_t bounded = std::min<uint8_t>(property, 3U);
            const char* label = bounded == 0U
                ? "PLAYBACK"
                : (bounded == 1U
                    ? "LENGTH"
                    : (bounded == 2U ? "OFFSET" : "CONVERT"));
            data.interactionIcon = ::standalone::icons::MACRO_AUTOMATION;
            data.interactionColor = ::standalone::theme::color::MACRO_AUTOMATION;
            std::snprintf(
                data.interactionLabel.data(),
                data.interactionLabel.size(),
                "%s",
                label
            );
            if (bounded == 0U) {
                const bool manual =
                    (source.macroUi.automationManualOverrideMask.get() &
                     static_cast<uint16_t>(1U << macroIndex)) != 0U;
                std::snprintf(
                    data.interactionValue.data(),
                    data.interactionValue.size(),
                    "%s",
                    manual
                        ? "TURN OPT FOR AUTO"
                        : (slot != nullptr && slot->automationEnabled
                            ? "ON"
                            : "OFF")
                );
            } else if (bounded == 1U || bounded == 2U) {
                const uint16_t ticks = slot == nullptr
                    ? 0U
                    : (bounded == 1U
                        ? slot->compatibility.automation.durationTicks
                        : slot->compatibility.automation.windowOffsetTicks);
                formatBeatDuration(
                    data.interactionValue.data(),
                    data.interactionValue.size(),
                    ticks,
                    " BEATS"
                );
            } else {
                std::snprintf(
                    data.interactionValue.data(),
                    data.interactionValue.size(),
                    "%s",
                    slot != nullptr && slot->automationStored
                        ? "TURN OPT TO PREVIEW"
                        : "NO AUTOMATION"
                );
            }
        } else {
            const auto destination =
                core::state::modulation::projectControlDestination(address);
            const auto& graph = source.pages.control.authored.modulation;
            const uint16_t assignmentCount = slot != nullptr
                ? slot->modulationCount
                : 0U;
            data.interactionIcon = ::standalone::icons::MACRO_MODULATION;
            data.interactionColor = ::standalone::theme::color::MACRO_MODULATION;
            if (property < assignmentCount) {
                const auto* binding = bindingAtDestination(
                    graph, destination, property
                );
                const auto* modulator = binding != nullptr
                    ? core::state::modulation::findProjectModulator(
                        graph, binding->sourceId
                    )
                    : nullptr;
                std::snprintf(
                    data.interactionLabel.data(),
                    data.interactionLabel.size(),
                    "%s",
                    modulator != nullptr ? modulator->name.data() : "SOURCE DEPTH"
                );
                const int depth = binding != nullptr
                    ? static_cast<int>(std::lround(
                        static_cast<float>(binding->amountQ15) /
                        32767.0f * 100.0f
                    ))
                    : 0;
                std::snprintf(
                    data.interactionValue.data(),
                    data.interactionValue.size(),
                    "%+d%% DEPTH",
                    depth
                );
            } else {
                const uint16_t scale =
                    core::state::modulation::projectModulationDestinationScaleQ15(
                        graph, destination
                    );
                std::snprintf(
                    data.interactionLabel.data(),
                    data.interactionLabel.size(),
                    "ALL DEPTH"
                );
                std::snprintf(
                    data.interactionValue.data(),
                    data.interactionValue.size(),
                    "%u%%",
                    static_cast<unsigned>(
                        (static_cast<uint32_t>(scale) * 200U + 32767U) /
                        65535U
                    )
                );
            }
        }
    }
    uint32_t revision =
        (static_cast<uint32_t>(macroIndex & 0x07U) << 29) |
        (automationStored ? (1UL << 28) : 0U) |
        (modulationStored ? (1UL << 26) : 0U) |
        (manualOverride ? (1UL << 27) : 0U) |
        (static_cast<uint32_t>(cc & 0x7FU) << 20) |
        (static_cast<uint32_t>(source.pages.currentActivePage() & 0x0FU) << 16) |
        (static_cast<uint32_t>(source.macroEdit.focusedRow.get() & 0x03U) << 14);
    if (slot != nullptr) {
        revision = mixRevision(
            revision,
            slot->compatibility.automation.pointCount
        );
        revision = mixRevision(
            revision,
            slot->compatibility.modulation.pointCount
        );
        revision = mixRevision(
            revision,
            static_cast<uint32_t>(slot->automationEnabled)
        );
        revision = mixRevision(
            revision,
            static_cast<uint32_t>(slot->activeModulationCount)
        );
        uint32_t depthBits = 0;
        std::memcpy(
            &depthBits,
            &slot->compatibility.modulationDepth,
            sizeof(depthBits)
        );
        revision = mixRevision(revision, depthBits);
    }
    revision = mixRevision(revision, focusedBindingId.value);
    revision = mixRevision(
        revision,
        (source.macroEdit.contextSelectorActive.get() ? 1U : 0U) |
            (source.macroEdit.macroCycleActive.get() ? 2U : 0U) |
            (static_cast<uint32_t>(
                 source.macroEdit.contextPropertyIndex.get()
             ) << 8U)
    );
    if (focusedBinding != nullptr) {
        revision = mixRevision(
            revision,
            static_cast<uint16_t>(focusedBinding->amountQ15)
        );
    }
    revision = mixRevision(revision, source.pages.control.authoredRevision);
    revision = mixRevision(revision, baseBits);
    data.dataRevision = mixRevision(
        mixRevision(
            revision,
            source.macroUi.automationRecordingRevision.get()
        ),
        mixRevision(
            data.previewRevision,
            source.macroUi.runtimeProjectionRevision.get()
        )
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
                   : (phase == core::state::MacroEditFlowPhase::NEW_MODULATOR_AUDITION
                          ? "Audition · Live"
                          : "Silent preview"))
    );

    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = source.pages.currentActiveTrack(),
        .page = source.pages.currentActivePage(),
        .macro = macroIndex,
    };
    core::state::modulation::ProjectControlMacroSlotView slotView{};
    const auto* slot = core::state::modulation::readProjectControlMacroSlot(
        source.pages.control,
        address,
        slotView
    ) ? &slotView : nullptr;
    const bool automationStored = slot != nullptr && slot->automationStored;
    const bool modulationStored = slot != nullptr && slot->modulationStored;
    const bool manualOverride =
        (source.macroUi.automationManualOverrideMask.get() &
         static_cast<uint16_t>(1U << macroIndex)) != 0;

    if (phase == core::state::MacroEditFlowPhase::MODULATOR_PICKER) {
        const auto& graph = source.pages.control.authored.modulation;
        std::snprintf(data.title.data(), data.title.size(), "%s", "Use Existing");
        std::snprintf(data.meta.data(), data.meta.size(), "%s", "Focus is silent");
        data.rowCount = static_cast<int>(graph.sourceCount);
        data.selectedIndex = data.rowCount > 0
            ? std::clamp(
                  source.macroEdit.macroSelector.selectedIndex.get(),
                  0,
                  data.rowCount - 1
              )
            : 0;
        data.rowProvider = &provideModulatorPickerRow;
        data.rowProviderContext = const_cast<Source*>(&source);
        uint32_t revision = mixRevision(
            source.pages.control.authoredRevision,
            static_cast<uint32_t>(data.selectedIndex)
        );
        const int first = std::max(0, data.selectedIndex - 2);
        const int last = std::min(data.rowCount, first + 5);
        for (int index = first; index < last; ++index) {
            const auto& modulator = graph.sources[static_cast<uint16_t>(index)];
            const uint32_t live = static_cast<uint32_t>(std::lround(
                std::clamp(
                    liveSourceValue(source.pages.control, modulator.id) * 0.5f +
                        0.5f,
                    0.0f,
                    1.0f
                ) * 63.0f
            ));
            revision = mixRevision(
                revision,
                modulator.id.value ^ (live << 24U)
            );
        }
        data.dataRevision = revision;
        data.visible = data.rowCount > 0;
        return data;
    }

    if (phase ==
        core::state::MacroEditFlowPhase::EXISTING_MODULATOR_AUDITION) {
        const auto& audition = source.pages.control.audition;
        const auto* modulator = audition.active && !audition.sourceCreated
            ? core::state::modulation::findProjectModulator(
                  source.pages.control.authored.modulation,
                  audition.sourceId
              )
            : nullptr;
        const auto* binding = audition.active
            ? core::state::modulation::findProjectModulationBinding(
                  source.pages.control.authored.modulation,
                  audition.bindingId
              )
            : nullptr;
        if (modulator == nullptr || binding == nullptr) return data;
        std::snprintf(
            data.title.data(),
            data.title.size(),
            "%s",
            modulator->name.data()
        );
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "%s",
            "Audition · Existing"
        );
        const int16_t depth =
            core::ui::macro::lfo_audition::depthQ15ToPercent(
                binding->amountQ15
            );
        std::snprintf(
            data.valueBuffers[1].data(),
            data.valueBuffers[1].size(),
            "%+d%%",
            static_cast<int>(depth)
        );
        data.rows = {{
            {.key = "Source", .value = "", .icon = ::standalone::icons::MACRO_MODULATION, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION, .sparkline = buildSourceSparkline(source.pages.control, *modulator)},
            {.key = "Depth", .value = data.valueBuffers[1].data(), .icon = ::standalone::icons::KNOB, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
            {},
            {},
            {},
            {},
            {},
        }};
        data.rowCount = 2;
        data.selectedIndex = std::min<int>(
            source.macroEdit.modulationFocusedRow.get(),
            1
        );
        const uint32_t live = static_cast<uint32_t>(std::lround(
            std::clamp(
                liveSourceValue(source.pages.control, modulator->id) * 0.5f +
                    0.5f,
                0.0f,
                1.0f
            ) * 255.0f
        ));
        data.dataRevision = mixRevision(
            mixRevision(source.pages.control.authoredRevision, audition.generation),
            live | (static_cast<uint32_t>(data.selectedIndex) << 8U)
        );
        data.visible = true;
        return data;
    }

    if (phase == core::state::MacroEditFlowPhase::NEW_MODULATOR_AUDITION) {
        const auto& audition = source.pages.control.audition;
        const auto* modulator = audition.active
            ? core::state::modulation::findProjectModulator(
                  source.pages.control.authored.modulation,
                  audition.sourceId
              )
            : nullptr;
        const core::state::modulation::ModulationBindingState* binding = nullptr;
        if (audition.active) {
            const auto& graph = source.pages.control.authored.modulation;
            for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
                if (graph.outputBindings[index].id == audition.bindingId) {
                    binding = &graph.outputBindings[index];
                    break;
                }
            }
        }
        if (modulator == nullptr || binding == nullptr) {
            return data;
        }
        if (modulator->kind == core::state::modulation::ModulatorKind::ADSR) {
            std::snprintf(
                data.title.data(),
                data.title.size(),
                "%s",
                modulator->name.data()
            );
            adsr_ui::formatDuration(
                data.valueBuffers[0].data(),
                data.valueBuffers[0].size(),
                modulator->parameters.adsr.attack,
                modulator->parameters.adsr.timing
            );
            std::snprintf(
                data.meta.data(),
                data.meta.size(),
                "A %.5s · Note",
                data.valueBuffers[0].data()
            );
            adsr_ui::formatDuration(
                data.valueBuffers[1].data(),
                data.valueBuffers[1].size(),
                modulator->parameters.adsr.decay,
                modulator->parameters.adsr.timing
            );
            std::snprintf(
                data.valueBuffers[2].data(),
                data.valueBuffers[2].size(),
                "%u%%",
                static_cast<unsigned>(adsr_ui::sustainQ15ToPercent(
                    modulator->parameters.adsr.sustainQ15
                ))
            );
            adsr_ui::formatDuration(
                data.valueBuffers[3].data(),
                data.valueBuffers[3].size(),
                modulator->parameters.adsr.release,
                modulator->parameters.adsr.timing
            );
            std::snprintf(
                data.valueBuffers[4].data(),
                data.valueBuffers[4].size(),
                "%+d%%",
                static_cast<int>(
                    core::ui::macro::lfo_audition::depthQ15ToPercent(
                        binding->amountQ15
                    )
                )
            );
            data.rows = {{
                {.key = "Attack", .value = data.valueBuffers[0].data(), .icon = ::standalone::icons::NOTE_PROP_GATE, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION, .sparkline = buildAdsrSparkline(source.pages.control, *modulator)},
                {.key = "Decay", .value = data.valueBuffers[1].data(), .icon = ::standalone::icons::LENGTH, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
                {.key = "Sustain", .value = data.valueBuffers[2].data(), .icon = ::standalone::icons::KNOB, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
                {.key = "Release", .value = data.valueBuffers[3].data(), .icon = ::standalone::icons::LENGTH, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
                {.key = "Depth", .value = data.valueBuffers[4].data(), .icon = ::standalone::icons::KNOB, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
                {},
                {},
            }};
            data.rowCount = adsr_ui::AUDITION_ITEM_COUNT;
            data.selectedIndex = std::min<int>(
                source.macroEdit.modulationFocusedRow.get(),
                adsr_ui::AUDITION_ITEM_COUNT - 1U
            );
            const float live = liveSourceValue(
                source.pages.control, modulator->id
            );
            const auto* runtime = adsr_ui::runtimeState(
                source.pages.control, modulator->id
            );
            const uint32_t stage = runtime != nullptr
                ? static_cast<uint32_t>(runtime->stage)
                : 0U;
            const uint8_t liveQuantized = static_cast<uint8_t>(std::lround(
                std::clamp(live, 0.0f, 1.0f) * 255.0f
            ));
            data.dataRevision = mixRevision(
                mixRevision(
                    source.pages.control.authoredRevision,
                    audition.generation
                ),
                static_cast<uint32_t>(liveQuantized) |
                    (static_cast<uint32_t>(data.selectedIndex) << 8U) |
                    (stage << 16U)
            );
            data.visible = true;
            return data;
        }
        if (modulator->kind != core::state::modulation::ModulatorKind::LFO) {
            return data;
        }
        std::snprintf(
            data.title.data(),
            data.title.size(),
            "%s",
            modulator->name.data()
        );
        std::snprintf(
            data.valueBuffers[0].data(),
            data.valueBuffers[0].size(),
            "%s",
            core::ui::macro::lfo_audition::shapeLabel(
                modulator->parameters.lfo.shape
            )
        );
        const uint8_t rateIndex = core::ui::macro::lfo_audition::rateIndex(
            modulator->parameters.lfo.periodTicks
        );
        std::snprintf(
            data.valueBuffers[1].data(),
            data.valueBuffers[1].size(),
            "%s",
            core::ui::macro::lfo_audition::rateLabel(rateIndex)
        );
        const int16_t depth =
            core::ui::macro::lfo_audition::depthQ15ToPercent(
                binding->amountQ15
            );
        std::snprintf(
            data.valueBuffers[2].data(),
            data.valueBuffers[2].size(),
            "%+d%%",
            static_cast<int>(depth)
        );
        const float live = liveSourceValue(
            source.pages.control,
            modulator->id
        );
        data.rows = {{
            {.key = "Shape", .value = data.valueBuffers[0].data(), .icon = ::standalone::icons::MACRO_MODULATION, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION, .sparkline = buildLfoSparkline(modulator->parameters.lfo.shape, live)},
            {.key = "Rate", .value = data.valueBuffers[1].data(), .icon = ::standalone::icons::DIVISION, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
            {.key = "Depth", .value = data.valueBuffers[2].data(), .icon = ::standalone::icons::KNOB, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
            {},
            {},
            {},
            {},
        }};
        data.rowCount = 3;
        data.selectedIndex = std::min<int>(
            source.macroEdit.modulationFocusedRow.get(),
            2
        );
        const uint8_t liveQuantized = static_cast<uint8_t>(std::lround(
            std::clamp(live * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f
        ));
        data.dataRevision = mixRevision(
            mixRevision(
                source.pages.control.authoredRevision,
                audition.generation
            ),
            static_cast<uint32_t>(liveQuantized) |
                (static_cast<uint32_t>(data.selectedIndex) << 8U)
        );
        data.visible = true;
        return data;
    }

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

    if (phase == core::state::MacroEditFlowPhase::MODULATION &&
        modulationStored) {
        const uint16_t count = slotView.modulationCount;
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            count == 1U ? "1 assignment" : "%u assignments",
            static_cast<unsigned>(count)
        );
        const auto navigationFeedback =
            source.macroEdit.modulatorNavigationFeedback.get();
        if (navigationFeedback == core::state::
                MacroModulatorNavigationFeedback::SOURCE_UNAVAILABLE) {
            std::snprintf(
                data.meta.data(), data.meta.size(), "%s", "Source removed"
            );
        } else if (navigationFeedback == core::state::
                       MacroModulatorNavigationFeedback::ASSIGNMENT_UNAVAILABLE) {
            std::snprintf(
                data.meta.data(), data.meta.size(), "%s", "Assignment removed"
            );
        } else if (navigationFeedback == core::state::
                       MacroModulatorNavigationFeedback::CONTEXT_CHANGED) {
            std::snprintf(
                data.meta.data(), data.meta.size(), "%s", "Context updated"
            );
        }
        data.rowCount = static_cast<int>(count) + 2;
        data.selectedIndex = std::clamp(
            static_cast<int>(source.macroEdit.modulationFocusedRow.get()),
            0,
            data.rowCount - 1
        );
        data.rowProvider = &provideModulationAssignmentRow;
        data.rowProviderContext = const_cast<Source*>(&source);
        data.dataRevision = mixRevision(
            mixRevision(
                source.pages.control.authoredRevision,
                static_cast<uint32_t>(data.selectedIndex)
            ),
            static_cast<uint32_t>(navigationFeedback)
        );
        data.visible = true;
        return data;
    }

    if (phase == core::state::MacroEditFlowPhase::MODULATOR_CREATE ||
        (phase == core::state::MacroEditFlowPhase::MODULATION &&
         !modulationStored)) {
        const auto navigationFeedback =
            source.macroEdit.modulatorNavigationFeedback.get();
        const bool reusable =
            source.pages.control.authored.modulation.sourceCount > 0U;
        if (phase == core::state::MacroEditFlowPhase::MODULATOR_CREATE) {
            std::snprintf(
                data.meta.data(), data.meta.size(), "%s", "Add source"
            );
        }
        if (phase == core::state::MacroEditFlowPhase::MODULATION) {
            if (navigationFeedback == core::state::
                    MacroModulatorNavigationFeedback::SOURCE_UNAVAILABLE) {
                std::snprintf(
                    data.meta.data(), data.meta.size(), "%s", "Source removed"
                );
            } else if (navigationFeedback == core::state::
                           MacroModulatorNavigationFeedback::ASSIGNMENT_UNAVAILABLE) {
                std::snprintf(
                    data.meta.data(),
                    data.meta.size(),
                    "%s",
                    "Assignment removed"
                );
            } else if (navigationFeedback == core::state::
                           MacroModulatorNavigationFeedback::CONTEXT_CHANGED) {
                std::snprintf(
                    data.meta.data(), data.meta.size(), "%s", "Context updated"
                );
            }
        }
        std::snprintf(
            data.valueBuffers[0].data(),
            data.valueBuffers[0].size(),
            "%s",
            "Create"
        );
        std::snprintf(
            data.valueBuffers[1].data(),
            data.valueBuffers[1].size(),
            "%s",
            "Create"
        );
        std::snprintf(
            data.valueBuffers[2].data(),
            data.valueBuffers[2].size(),
            "%s",
            reusable ? "Choose" : "None yet"
        );
        data.rows = {{
            {.key = "New LFO", .value = data.valueBuffers[0].data(), .icon = ::standalone::icons::MACRO_MODULATION, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
            {.key = "New ADSR", .value = data.valueBuffers[1].data(), .icon = ::standalone::icons::NOTE_PROP_GATE, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
            {.key = "Use Existing", .value = data.valueBuffers[2].data(), .icon = ::standalone::icons::ACTION_PLACE_TARGET, .iconFont = standalone_fonts.icons_14, .iconColor = reusable ? ::standalone::theme::color::MACRO_MODULATION : ::standalone::theme::color::TEXT_SECONDARY},
            {},
            {},
            {},
            {},
        }};
        data.rowCount = 3;
        data.selectedIndex = std::min<int>(
            source.macroEdit.modulationFocusedRow.get(),
            2
        );
        data.dataRevision = mixRevision(
            mixRevision(
                source.pages.control.authoredRevision,
                static_cast<uint32_t>(data.selectedIndex) |
                    (reusable ? (1UL << 8U) : 0U)
            ),
            static_cast<uint32_t>(navigationFeedback)
        );
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
                core::state::modulation::projectControlCurveWindowSummary(
                    source.pages.control,
                    slot->automationCurveId
                );
            curveSparkline = buildCurveSparkline(
                source.pages.control,
                slot->automationCurveId,
                slot->compatibility.automation
            );
            formatBeatDuration(
                data.valueBuffers[2].data(),
                data.valueBuffers[2].size(),
                slot->compatibility.automation.durationTicks,
                " beats"
            );
            formatBeatDuration(
                data.valueBuffers[3].data(),
                data.valueBuffers[3].size(),
                slot->compatibility.automation.windowOffsetTicks,
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
            slot != nullptr && slot->modulationCount == 1U &&
            std::abs(slot->compatibility.modulationDepth) < 0.0001f;
        if (!modulationStored) {
            std::snprintf(
                data.valueBuffers[1].data(),
                data.valueBuffers[1].size(),
                "%s",
                "-"
            );
        } else if (slot->modulationCount > 1U) {
            std::snprintf(
                data.valueBuffers[1].data(),
                data.valueBuffers[1].size(),
                "%u sources",
                static_cast<unsigned>(slot->modulationCount)
            );
        } else {
            std::snprintf(
                data.valueBuffers[1].data(),
                data.valueBuffers[1].size(),
                "%d%%",
                static_cast<int>(std::lround(
                    std::clamp(
                        slot->compatibility.modulationDepth,
                        -1.0f,
                        1.0f
                    ) * 100.0f
                ))
            );
        }
        const char* modulationDomainLabel = "-";
        if (modulationStored) {
            if (slot->modulationCount > 1U) {
                modulationDomainLabel = "Mixed sources";
            } else if (!slot->primaryRecordedShape) {
                modulationDomainLabel = "Centered LFO";
            } else {
                const auto* curve = core::state::modulation::findProjectCurve(
                    source.pages.control.authored.curves,
                    slot->modulationCurveId
                );
                modulationDomainLabel = curve != nullptr &&
                    curve->valueDomain == core::state::modulation::
                        ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR
                    ? "Positive shape"
                    : "Centered shape";
            }
        }
        std::snprintf(
            data.valueBuffers[2].data(),
            data.valueBuffers[2].size(),
            "%s",
            modulationDomainLabel
        );
        std::snprintf(
            data.valueBuffers[3].data(),
            data.valueBuffers[3].size(),
            "%s",
            !modulationStored
                ? "None"
                : (slot->modulationCount > 1U
                    ? "Mixed"
                    : (slot->primaryRecordedShape
                        ? modulationOriginLabel(
                            slot->compatibility.modulation.modulationOrigin
                        )
                        : "Generated"))
        );
        core::ui::MacroEditorPreviewModel modulationPreview{};
        if (modulationStored) {
            core::ui::buildMacroEditorPreviewModel(
                source.pages.activePageData().values[macroIndex],
                source.pages.control,
                address,
                manualOverride,
                modulationPreview
            );
        }
        const auto modulationSparkline = modulationStored
            ? buildModulationSparkline(modulationPreview)
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
        revision = mixRevision(
            revision,
            slot->compatibility.automation.pointCount
        );
        revision = mixRevision(
            revision,
            slot->compatibility.automation.durationTicks
        );
        revision = mixRevision(
            revision,
            slot->compatibility.automation.sourceDurationTicks
        );
        revision = mixRevision(
            revision,
            slot->compatibility.automation.windowOffsetTicks
        );
    }
    if (modulationStored) {
        revision = mixRevision(revision, slot->modulationCount);
        revision = mixRevision(revision, slot->activeModulationCount);
        uint32_t depthBits = 0;
        std::memcpy(
            &depthBits,
            &slot->compatibility.modulationDepth,
            sizeof(depthBits)
        );
        revision = mixRevision(revision, depthBits);
    }
    revision = mixRevision(revision, source.pages.control.authoredRevision);
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
    core::state::modulation::ProjectControlMacroSlotView slot{};
    const bool slotReadable = core::state::modulation::readProjectControlMacroSlot(
        source.pages.control,
        address,
        slot
    );
    const bool automationStored = slotReadable && slot.automationStored;
    const bool modulationStored = slotReadable && slot.modulationStored;
    const bool automationPlayback = automationStored && slot.automationEnabled;
    const bool modulationPlayback = modulationStored &&
        slot.activeModulationCount > 0U;

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
    const bool canPasteAssignment = row == 2U && !modulationStored &&
        source.clipboard != nullptr &&
        source.clipboard->hasMacroModulationAssignment();
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        canPasteAssignment ? ::standalone::icons::ACTION_PASTE
                           : ::standalone::icons::ACTION_COPY,
        (canCopy || canPasteAssignment) ? Visual::ACTIVE : Visual::DISABLED,
        canPasteAssignment ? Tone::CONSTRUCTIVE : Tone::NEUTRAL
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
    if (phase == core::state::MacroEditFlowPhase::MODULATOR_CREATE ||
        phase == core::state::MacroEditFlowPhase::MODULATOR_PICKER) {
        props.slots[0].visualState = Visual::HIDDEN;
        props.slots[1] = scopeLabel("Choose source");
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }
    if (phase == core::state::MacroEditFlowPhase::NEW_MODULATOR_AUDITION ||
        phase ==
            core::state::MacroEditFlowPhase::EXISTING_MODULATOR_AUDITION) {
        props.slots[0].visualState = Visual::HIDDEN;
        props.slots[1] = scopeLabel("Audition");
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            ::standalone::icons::ACTION_VALIDATE,
            source.pages.control.audition.active
                ? Visual::ACTIVE
                : Visual::DISABLED,
            Tone::CONSTRUCTIVE
        );
        projectGuardedAction(
            props.slots[2],
            source,
            core::state::MacroContextButton::BOTTOM_RIGHT
        );
        return props;
    }
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
    core::state::modulation::ProjectControlMacroSlotView slot{};
    const bool slotReadable = core::state::modulation::readProjectControlMacroSlot(
        source.pages.control,
        address,
        slot
    );
    const bool modulation = phase == core::state::MacroEditFlowPhase::MODULATION;
    const bool automationStored = slotReadable && slot.automationStored;
    const bool modulationStored = slotReadable && slot.modulationStored;
    if (modulation && modulationStored) {
        const uint16_t count = slot.modulationCount;
        const int firstAssignmentRow = 1;
        const int addSourceRow = firstAssignmentRow + static_cast<int>(count);
        const int row = std::clamp(
            static_cast<int>(source.macroEdit.modulationFocusedRow.get()),
            0,
            addSourceRow
        );
        if (row == addSourceRow) {
            props.slots[0].visualState = Visual::HIDDEN;
            props.slots[1] = scopeLabel("Add source");
            props.slots[2].visualState = Visual::HIDDEN;
            return props;
        }
        if (row == 0) {
            const bool anyEnabled = slot.activeModulationCount > 0U;
            props.slots[0] = core::ui::makeStandaloneIconStripSlot(
                anyEnabled ? ::standalone::icons::MACRO_MODULATION
                           : ::standalone::icons::STATUS_PAUSED,
                Visual::ACTIVE,
                Tone::NEUTRAL
            );
            props.slots[1] = scopeLabel("All");
            props.slots[2].visualState = Visual::DISABLED;
            projectGuardedAction(
                props.slots[0],
                source,
                core::state::MacroContextButton::BOTTOM_LEFT
            );
            return props;
        }

        const int targetOrdinal = row - firstAssignmentRow;
        const auto destination =
            core::state::modulation::projectControlDestination(address);
        const auto& graph = source.pages.control.authored.modulation;
        const core::state::modulation::ModulationBindingState* binding = nullptr;
        int ordinal = 0;
        for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
            const auto& candidate = graph.outputBindings[index];
            if (candidate.destination != destination) continue;
            if (ordinal++ == targetOrdinal) {
                binding = &candidate;
                break;
            }
        }
        const auto* modulator = binding != nullptr
            ? core::state::modulation::findProjectModulator(
                  graph,
                  binding->sourceId
              )
            : nullptr;
        if (binding == nullptr || modulator == nullptr) return props;
        const bool enabled =
            (binding->flags & core::state::modulation::
                PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            enabled ? ::standalone::icons::MACRO_MODULATION
                    : ::standalone::icons::STATUS_PAUSED,
            Visual::ACTIVE,
            Tone::NEUTRAL
        );
        props.slots[1] = scopeLabel(modulator->name.data());
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            ::standalone::icons::ACTION_COPY,
            Visual::ACTIVE,
            Tone::NEUTRAL
        );
        projectGuardedAction(
            props.slots[0],
            source,
            core::state::MacroContextButton::BOTTOM_LEFT
        );
        projectGuardedAction(
            props.slots[2],
            source,
            core::state::MacroContextButton::BOTTOM_RIGHT
        );
        return props;
    }
    const bool stored = modulation ? modulationStored : automationStored;
    const bool playback = stored && (modulation
        ? slot.activeModulationCount > 0U
        : slot.automationEnabled);
    props.slots[0] = core::ui::makeStandaloneIconStripSlot(
        playback
            ? (modulation ? ::standalone::icons::MACRO_MODULATION
                          : ::standalone::icons::MACRO_AUTOMATION)
            : ::standalone::icons::STATUS_PAUSED,
        stored ? Visual::ACTIVE : Visual::DISABLED,
        Tone::NEUTRAL
    );
    props.slots[1] = scopeLabel(modulation ? "Modulation" : "Automation");
    const bool canPasteAssignment = modulation && !stored &&
        source.clipboard != nullptr &&
        source.clipboard->hasMacroModulationAssignment();
    const bool canCopy = stored;
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        canPasteAssignment ? ::standalone::icons::ACTION_PASTE
                           : ::standalone::icons::ACTION_COPY,
        (canCopy || canPasteAssignment) ? Visual::ACTIVE : Visual::DISABLED,
        canPasteAssignment ? Tone::CONSTRUCTIVE : Tone::NEUTRAL
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

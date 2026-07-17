#include "ui/project/ProjectModulatorUiModel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/macro/MacroLfoAuditionModel.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::project::modulators {
namespace {

using namespace core::state::modulation;

const char LABEL_FREE[] PROGMEM = "Free";
const char LABEL_TEMPO_SYNC[] PROGMEM = "Tempo Sync";
const char LABEL_ON_PLAY[] PROGMEM = "On Play";
const char LABEL_TRIGGERED[] PROGMEM = "Triggered";
const char LABEL_FREE_RUN[] PROGMEM = "Free Run";
const char LABEL_RUN[] PROGMEM = "Run";
const char LABEL_AVAILABLE_IN[] PROGMEM = "Available in";
const char LABEL_DETAILS[] PROGMEM = "Details";
const char LABEL_MORE[] PROGMEM = "More >";
const char LABEL_RENAME[] PROGMEM = "Rename";
const char LABEL_DESTINATIONS[] PROGMEM = "Destinations";
const char DESTINATION_KEY_FORMAT[] PROGMEM = "T%u/P%u/M%u";
const char DESTINATION_VALUE_FORMAT[] PROGMEM = "CC%u · %+d%%";
const char DESTINATION_OFF_VALUE_FORMAT[] PROGMEM = "CC%u · Off %+d%%";
const char PICKER_ADD_KEY_FORMAT[] PROGMEM = "+ M%u · CC%u";
const char PICKER_KEY_FORMAT[] PROGMEM = "M%u · CC%u";
const char PICKER_PREVIEW_FORMAT[] PROGMEM = "Preview %+d%%";
const char PICKER_ASSIGNED[] PROGMEM = "Assigned";
const char PICKER_DEPTH_PREVIEW[] PROGMEM = "+25% · Preview";
const char PICKER_CREATE_ASSIGN[] PROGMEM = "Create + assign";
const char PICKER_PREREQUISITE_FORMAT[] PROGMEM = "Add M%u first";
const char PICKER_UNAVAILABLE[] PROGMEM = "Unavailable";

const char RATE_1_16[] PROGMEM = "1/16";
const char RATE_1_8[] PROGMEM = "1/8";
const char RATE_1_4[] PROGMEM = "1/4";
const char RATE_1_2[] PROGMEM = "1/2";
const char RATE_1_BAR[] PROGMEM = "1B";
const char RATE_2_BARS[] PROGMEM = "2B";
const char* const COMPACT_RATE_LABELS[] PROGMEM = {
    RATE_1_16,
    RATE_1_8,
    RATE_1_4,
    RATE_1_2,
    RATE_1_BAR,
    RATE_2_BARS,
};

FLASHMEM const char* shapeLabel(ModulatorLfoShape shape) {
    return core::ui::macro::lfo_audition::shapeLabel(shape);
}

FLASHMEM const char* timingLabel(ModulatorTimingMode timing) {
    return timing == ModulatorTimingMode::FREE
        ? LABEL_FREE
        : LABEL_TEMPO_SYNC;
}

FLASHMEM const char* retriggerLabel(ModulatorRetriggerPolicy retrigger) {
    switch (retrigger) {
        case ModulatorRetriggerPolicy::TRANSPORT: return LABEL_ON_PLAY;
        case ModulatorRetriggerPolicy::EXPLICIT_TRIGGER: return LABEL_TRIGGERED;
        case ModulatorRetriggerPolicy::FREE_RUNNING:
        default: return LABEL_FREE_RUN;
    }
}

FLASHMEM void formatFreePeriod(char* out, size_t size, uint32_t milliseconds) {
    if (milliseconds >= 1000U) {
        const uint32_t tenths = (milliseconds + 50U) / 100U;
        std::snprintf(
            out,
            size,
            "%u.%us",
            static_cast<unsigned>(tenths / 10U),
            static_cast<unsigned>(tenths % 10U)
        );
    } else {
        std::snprintf(out, size, "%ums", static_cast<unsigned>(milliseconds));
    }
}

FLASHMEM void formatRate(char* out,
                         size_t size,
                         const ModulatorLfoParameters& lfo,
                         bool compact) {
    if (lfo.timing == ModulatorTimingMode::FREE) {
        formatFreePeriod(out, size, lfo.freePeriodMs);
        return;
    }
    const char* label = core::ui::macro::lfo_audition::rateLabel(
        core::ui::macro::lfo_audition::rateIndex(lfo.periodTicks)
    );
    if (!compact) {
        std::snprintf(out, size, "%s", label);
        return;
    }
    const uint8_t index = core::ui::macro::lfo_audition::rateIndex(
        lfo.periodTicks
    );
    std::snprintf(out, size, "%s", COMPACT_RATE_LABELS[index]);
}

FLASHMEM void formatDuration(char* out, size_t size, uint16_t ticks) {
    constexpr uint32_t TICKS_PER_BEAT = PROJECT_CONTROL_TICKS_PER_BEAT;
    const uint32_t tenths =
        (static_cast<uint32_t>(ticks) * 10U + TICKS_PER_BEAT / 2U) /
        TICKS_PER_BEAT;
    if ((tenths % 10U) == 0U) {
        std::snprintf(out, size, "%ub", static_cast<unsigned>(tenths / 10U));
    } else {
        std::snprintf(
            out,
            size,
            "%u.%ub",
            static_cast<unsigned>(tenths / 10U),
            static_cast<unsigned>(tenths % 10U)
        );
    }
}

FLASHMEM void formatReach(char* out, size_t size, const ModulatorReach& reach) {
    switch (reach.kind) {
        case ModulatorReachKind::MACRO:
            std::snprintf(
                out,
                size,
                "T%uP%uM%u",
                static_cast<unsigned>(reach.track + 1U),
                static_cast<unsigned>(reach.page + 1U),
                static_cast<unsigned>(reach.macro + 1U)
            );
            return;
        case ModulatorReachKind::TRACK_SET: {
            uint8_t count = 0;
            uint8_t first = 0;
            uint8_t second = 0;
            for (uint8_t track = 0; track < PROJECT_MODULATION_TRACK_COUNT; ++track) {
                if ((reach.trackMask & (1U << track)) == 0U) continue;
                if (count == 0U) first = track;
                if (count == 1U) second = track;
                ++count;
            }
            if (count == 1U) {
                std::snprintf(out, size, "T%u", static_cast<unsigned>(first + 1U));
            } else if (count == 2U) {
                std::snprintf(
                    out,
                    size,
                    "T%u,%u",
                    static_cast<unsigned>(first + 1U),
                    static_cast<unsigned>(second + 1U)
                );
            } else {
                std::snprintf(out, size, "%u Tracks", static_cast<unsigned>(count));
            }
            return;
        }
        case ModulatorReachKind::PROJECT:
            std::snprintf(out, size, "%s", "Project");
            return;
        case ModulatorReachKind::DETACHED:
        default:
            std::snprintf(out, size, "%s", "Detached");
            return;
    }
}

FLASHMEM void formatRegistryReach(
    char* out,
    size_t size,
    const ModulatorReach& reach
) {
    if (reach.kind == ModulatorReachKind::MACRO) {
        std::snprintf(out, size, "%s", "Macro");
        return;
    }
    formatReach(out, size, reach);
}

FLASHMEM float liveSourceValue(const ProjectControlState& control, ModulatorId id) {
    for (uint16_t index = 0; index < control.plan.sourceCount; ++index) {
        if (control.plan.sources[index].id == id) {
            return std::clamp(control.sourceScratch[index], -1.0f, 1.0f);
        }
    }
    return 0.0f;
}

FLASHMEM ms::ui::KeyValueSparkline lfoSparkline(
    ModulatorLfoShape shape,
    float live,
    bool enabled
) {
    ms::ui::KeyValueSparkline out{};
    out.enabled = true;
    out.centerLine = true;
    out.liveMarker = enabled;
    out.sampleCount = static_cast<uint8_t>(ms::ui::KEY_VALUE_SPARKLINE_SAMPLE_COUNT);
    out.liveValue = static_cast<uint8_t>(std::lround(
        std::clamp(live * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f
    ));
    switch (shape) {
        case ModulatorLfoShape::TRIANGLE:
            out.samples = {{128, 179, 230, 255, 204, 153, 102, 51, 0, 26, 77, 128}};
            break;
        case ModulatorLfoShape::SAW_UP:
            out.samples = {{0, 23, 46, 70, 93, 116, 139, 162, 185, 209, 232, 255}};
            break;
        case ModulatorLfoShape::SAW_DOWN:
            out.samples = {{255, 232, 209, 185, 162, 139, 116, 93, 70, 46, 23, 0}};
            break;
        case ModulatorLfoShape::SQUARE:
            out.samples = {{255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0}};
            break;
        case ModulatorLfoShape::SINE:
        default:
            out.samples = {{128, 197, 244, 255, 221, 164, 91, 34, 1, 11, 58, 128}};
            break;
    }
    return out;
}

FLASHMEM ms::ui::KeyValueSparkline recordedSparkline(
    const ProjectControlState& control,
    const ModulatorSourceState& source,
    bool enabled
) {
    ms::ui::KeyValueSparkline out{};
    const auto* record = findProjectCurve(
        control.authored.curves,
        source.parameters.recordedCurveId
    );
    if (record == nullptr || record->pointCount == 0U) return out;
    out.enabled = true;
    out.centerLine = true;
    out.liveMarker = enabled;
    out.liveValue = static_cast<uint8_t>(std::lround(
        std::clamp(liveSourceValue(control, source.id) * 0.5f + 0.5f, 0.0f, 1.0f) *
        255.0f
    ));
    out.sampleCount = static_cast<uint8_t>(ms::ui::KEY_VALUE_SPARKLINE_SAMPLE_COUNT);
    const uint16_t duration = std::max<uint16_t>(record->durationTicks, 1U);
    for (uint8_t index = 0; index < out.sampleCount; ++index) {
        const uint32_t tick =
            (static_cast<uint32_t>(index) * (duration - 1U)) /
            (out.sampleCount - 1U);
        const float beat = static_cast<float>(tick) /
            static_cast<float>(PROJECT_CONTROL_TICKS_PER_BEAT);
        const float value = evaluateProjectControlCurve(
            control,
            source.parameters.recordedCurveId,
            beat,
            0.0f
        );
        out.samples[index] = static_cast<uint8_t>(std::lround(
            std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f
        ));
    }
    return out;
}

FLASHMEM ms::ui::KeyValueSparkline sourceSparkline(
    const ProjectControlState& control,
    const ModulatorSourceState& source
) {
    const bool enabled =
        (source.flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
    return source.kind == ModulatorKind::LFO
        ? lfoSparkline(
              source.parameters.lfo.shape,
              liveSourceValue(control, source.id),
              enabled
          )
        : recordedSparkline(control, source, enabled);
}

FLASHMEM void setText(std::array<char, ms::ui::KEY_VALUE_ROW_TEXT_CAPACITY>& out,
                      const char* text) {
    std::snprintf(out.data(), out.size(), "%s", text ? text : "");
}

}  // namespace

FLASHMEM const ModulatorSourceState* sourceAtRegistryIndex(
    const ProjectControlState& control,
    uint16_t index
) {
    return index < control.authored.modulation.sourceCount
        ? &control.authored.modulation.sources[index]
        : nullptr;
}

FLASHMEM void populateRegistryRow(const ProjectControlState& control,
                                  int index,
                                  ms::ui::KeyValueRowBuffer& out) {
    const auto& graph = control.authored.modulation;
    if (index < 0) return;
    if (index >= static_cast<int>(graph.sourceCount)) {
        setText(out.key, "+ Source");
        setText(out.value, "Create");
        out.iconFont = standalone_fonts.icons_14;
        setText(out.icon, standalone::icons::MACRO_MODULATION);
        out.iconColor = standalone::theme::color::MACRO_MODULATION;
        return;
    }

    const auto& source = graph.sources[static_cast<uint16_t>(index)];
    const bool enabled =
        (source.flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U;
    const uint16_t destinations = sourceDestinationCount(graph, source.id);
    std::snprintf(
        out.key.data(),
        out.key.size(),
        enabled ? "%s" : "%s · Off",
        source.name.data()
    );

    char primary[16]{};
    if (source.kind == ModulatorKind::LFO) {
        formatRate(primary, sizeof(primary), source.parameters.lfo, true);
    } else {
        const auto* curve = findProjectCurve(
            control.authored.curves,
            source.parameters.recordedCurveId
        );
        formatDuration(primary, sizeof(primary), curve ? curve->durationTicks : 0U);
    }
    char reach[16]{};
    formatRegistryReach(reach, sizeof(reach), source.reach);
    if (destinations == 0U) {
        std::snprintf(
            out.detail.data(),
            out.detail.size(),
            "%s · Unassigned",
            primary
        );
    } else {
        std::snprintf(
            out.detail.data(),
            out.detail.size(),
            "%s · %s · x%u",
            primary,
            reach,
            static_cast<unsigned>(destinations)
        );
    }
    out.iconFont = standalone_fonts.icons_14;
    setText(
        out.icon,
        source.kind == ModulatorKind::LFO
            ? standalone::icons::MACRO_MODULATION
            : standalone::icons::MACRO_AUTOMATION
    );
    out.iconColor = enabled
        ? standalone::theme::color::MACRO_MODULATION
        : standalone::theme::color::INACTIVE;
    out.sparkline = sourceSparkline(control, source);
}

FLASHMEM void populateSourceDetailRow(
    const ProjectControlState& control,
    const ModulatorSourceState& source,
    int index,
    ms::ui::KeyValueRowBuffer& out
) {
    const auto layout = sourceDetailLayout(source.kind);
    if (index < 0 || index >= layout.count) return;
    const auto item = layout.at(static_cast<uint8_t>(index));
    out.iconFont = standalone_fonts.icons_14;
    out.iconColor = standalone::theme::color::MACRO_MODULATION;
    char value[32]{};
    switch (item) {
        case SourceDetailItem::PREVIEW:
            setText(out.key, "Source");
            setText(
                out.icon,
                source.kind == ModulatorKind::LFO
                    ? standalone::icons::MACRO_MODULATION
                    : standalone::icons::MACRO_AUTOMATION
            );
            out.sparkline = sourceSparkline(control, source);
            break;
        case SourceDetailItem::ENABLED:
            setText(out.key, "Enabled");
            setText(
                out.value,
                (source.flags & PROJECT_MODULATOR_FLAG_ENABLED) != 0U
                    ? "On" : "Off"
            );
            setText(out.icon, standalone::icons::STATUS_RESUME);
            break;
        case SourceDetailItem::SHAPE:
            setText(out.key, "Shape");
            setText(out.value, shapeLabel(source.parameters.lfo.shape));
            setText(out.icon, standalone::icons::MACRO_MODULATION);
            break;
        case SourceDetailItem::RATE:
            setText(out.key, "Rate");
            formatRate(value, sizeof(value), source.parameters.lfo, false);
            setText(out.value, value);
            setText(out.icon, standalone::icons::DIVISION);
            break;
        case SourceDetailItem::TIMING:
            setText(out.key, "Timing");
            setText(out.value, timingLabel(source.parameters.lfo.timing));
            setText(out.icon, standalone::icons::TEMPO);
            break;
        case SourceDetailItem::PHASE: {
            setText(out.key, "Phase");
            const int32_t percent =
                (static_cast<int32_t>(source.parameters.lfo.phaseQ15) + 32767) *
                100 / 65534;
            std::snprintf(value, sizeof(value), "%ld%%", static_cast<long>(percent));
            setText(out.value, value);
            setText(out.icon, standalone::icons::OFFSET);
            break;
        }
        case SourceDetailItem::RETRIGGER:
            setText(out.key, LABEL_RUN);
            setText(out.value, retriggerLabel(source.parameters.lfo.retrigger));
            setText(out.icon, standalone::icons::CYCLE_STATE);
            break;
        case SourceDetailItem::LENGTH: {
            setText(out.key, "Length");
            const auto* curve = findProjectCurve(
                control.authored.curves,
                source.parameters.recordedCurveId
            );
            formatDuration(value, sizeof(value), curve ? curve->durationTicks : 0U);
            setText(out.value, value);
            setText(out.icon, standalone::icons::LENGTH);
            break;
        }
        case SourceDetailItem::SOURCE_DOMAIN: {
            setText(out.key, "Domain");
            const auto* curve = findProjectCurve(
                control.authored.curves,
                source.parameters.recordedCurveId
            );
            setText(
                out.value,
                curve != nullptr &&
                    curve->valueDomain ==
                        ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR
                    ? "Positive"
                    : "Centered"
            );
            setText(out.icon, standalone::icons::KNOB);
            break;
        }
        case SourceDetailItem::REACH:
            setText(out.key, LABEL_AVAILABLE_IN);
            formatReach(value, sizeof(value), source.reach);
            setText(out.value, value);
            setText(out.icon, standalone::icons::ROUTE_PIN);
            break;
        case SourceDetailItem::OPTIONS:
            setText(out.key, LABEL_DETAILS);
            setText(out.value, LABEL_MORE);
            setText(out.icon, standalone::icons::SETTINGS_GEAR);
            break;
        case SourceDetailItem::RENAME:
            setText(out.key, LABEL_RENAME);
            setText(out.value, source.name.data());
            setText(out.icon, standalone::icons::ACTION_PLACE_TARGET);
            break;
        case SourceDetailItem::DESTINATIONS:
            setText(out.key, LABEL_DESTINATIONS);
            std::snprintf(
                value,
                sizeof(value),
                "%u >",
                static_cast<unsigned>(sourceDestinationCount(
                    control.authored.modulation,
                    source.id
                ))
            );
            setText(out.value, value);
            setText(out.icon, standalone::icons::ROUTING);
            break;
        default:
            break;
    }
}

FLASHMEM void populateSourceOptionsRow(
    const ProjectControlState& control,
    const ModulatorSourceState& source,
    int index,
    ms::ui::KeyValueRowBuffer& out
) {
    const auto layout = sourceOptionsLayout(source.kind);
    if (index < 0 || index >= layout.count) return;
    const auto item = layout.at(static_cast<uint8_t>(index));
    out.iconFont = standalone_fonts.icons_14;
    out.iconColor = standalone::theme::color::MACRO_MODULATION;
    char value[32]{};
    switch (item) {
        case SourceDetailItem::PHASE: {
            setText(out.key, "Phase");
            const int32_t percent =
                (static_cast<int32_t>(source.parameters.lfo.phaseQ15) + 32767) *
                100 / 65534;
            std::snprintf(value, sizeof(value), "%ld%%", static_cast<long>(percent));
            setText(out.value, value);
            setText(out.icon, standalone::icons::OFFSET);
            break;
        }
        case SourceDetailItem::RETRIGGER:
            setText(out.key, LABEL_RUN);
            setText(out.value, retriggerLabel(source.parameters.lfo.retrigger));
            setText(out.icon, standalone::icons::CYCLE_STATE);
            break;
        case SourceDetailItem::REACH:
            setText(out.key, LABEL_AVAILABLE_IN);
            formatReach(value, sizeof(value), source.reach);
            setText(out.value, value);
            setText(out.icon, standalone::icons::ROUTE_PIN);
            break;
        case SourceDetailItem::RENAME:
            setText(out.key, LABEL_RENAME);
            setText(out.value, source.name.data());
            setText(out.icon, standalone::icons::ACTION_PLACE_TARGET);
            break;
        case SourceDetailItem::DESTINATIONS:
            setText(out.key, LABEL_DESTINATIONS);
            std::snprintf(
                value,
                sizeof(value),
                "%u >",
                static_cast<unsigned>(sourceDestinationCount(
                    control.authored.modulation,
                    source.id
                ))
            );
            setText(out.value, value);
            setText(out.icon, standalone::icons::ROUTING);
            break;
        default:
            break;
    }
}

FLASHMEM void populateDestinationRow(
    const core::state::macro::MacroPagesState& pages,
    ModulatorId sourceId,
    int index,
    ms::ui::KeyValueRowBuffer& out
) {
    if (index < 0) return;
    const auto& graph = pages.control.authored.modulation;
    const auto* binding = core::state::project::modulators::sourceBindingAtOrdinal(
        graph,
        sourceId,
        static_cast<uint16_t>(index)
    );
    if (binding == nullptr) {
        setText(out.key, "+ Destination");
        setText(out.value, "Assign");
        setText(out.icon, standalone::icons::ROUTING);
        out.iconFont = standalone_fonts.icons_14;
        out.iconColor = standalone::theme::color::MACRO_MODULATION;
        return;
    }

    const auto& destination = binding->destination;
    const uint8_t cc = pages.tracks[destination.track]
        .pages[destination.page]
        .cc[destination.macro];
    std::snprintf(
        out.key.data(),
        out.key.size(),
        DESTINATION_KEY_FORMAT,
        static_cast<unsigned>(destination.track + 1U),
        static_cast<unsigned>(destination.page + 1U),
        static_cast<unsigned>(destination.macro + 1U)
    );
    const int16_t percent =
        core::ui::macro::lfo_audition::depthQ15ToPercent(binding->amountQ15);
    const bool enabled =
        (binding->flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U;
    std::snprintf(
        out.value.data(),
        out.value.size(),
        enabled ? DESTINATION_VALUE_FORMAT : DESTINATION_OFF_VALUE_FORMAT,
        static_cast<unsigned>(cc),
        static_cast<int>(percent)
    );
    setText(out.icon, standalone::icons::ROUTING);
    out.iconFont = standalone_fonts.icons_14;
    out.iconColor = enabled
        ? standalone::theme::color::MACRO_MODULATION
        : standalone::theme::color::INACTIVE;
}

FLASHMEM void populateReachRow(
    const ProjectControlState& control,
    ModulatorId sourceId,
    int index,
    ms::ui::KeyValueRowBuffer& out
) {
    if (index < 0) return;
    const auto& graph = control.authored.modulation;
    const auto* source = findProjectModulator(graph, sourceId);
    if (!source) return;
    const auto layout =
        core::state::project::modulators::sourceReachChoiceLayout(
            graph,
            sourceId
        );
    if (index >= layout.count) return;
    const auto choice = layout.at(static_cast<uint8_t>(index));
    using core::state::project::modulators::ReachChoiceKind;
    out.iconFont = standalone_fonts.icons_14;
    out.iconColor = standalone::theme::color::MACRO_MODULATION;

    if (choice.kind == ReachChoiceKind::TIGHTEST) {
        setText(out.key, "Tightest");
        char value[20]{};
        const auto reach =
            core::state::project::modulators::tightestSourceReach(
                graph,
                sourceId
            );
        formatReach(value, sizeof(value), reach);
        setText(out.value, value);
        setText(out.icon, standalone::icons::ROUTE_PIN);
        return;
    }
    if (choice.kind == ReachChoiceKind::PROJECT) {
        setText(out.key, "Project");
        setText(
            out.value,
            source->reach.kind == ModulatorReachKind::PROJECT
                ? "Current" : "Allow all"
        );
        setText(out.icon, standalone::icons::ROUTING);
        return;
    }

    std::snprintf(
        out.key.data(),
        out.key.size(),
        "Split T%u",
        static_cast<unsigned>(choice.track + 1U)
    );
    std::snprintf(
        out.value.data(),
        out.value.size(),
        "%u dest.",
        static_cast<unsigned>(choice.destinationCount)
    );
    setText(out.icon, standalone::icons::ACTION_COPY);
}

FLASHMEM void populateDestinationPickerRow(
    const core::state::macro::MacroPagesState& pages,
    ModulatorId sourceId,
    uint8_t track,
    uint8_t page,
    bool creatingSource,
    int index,
    ms::ui::KeyValueRowBuffer& out
) {
    if (index < 0 || track >= core::state::macro::TRACK_COUNT ||
        page >= core::state::macro::PAGE_COUNT) {
        return;
    }
    if (creatingSource && index == core::state::macro::MACRO_COUNT) {
        setText(out.key, "Keep Unassigned");
        setText(out.value, "Explicit");
        setText(out.icon, standalone::icons::ROUTE_PIN);
        out.iconFont = standalone_fonts.icons_14;
        out.iconColor = standalone::theme::color::INACTIVE;
        return;
    }
    if (index >= core::state::macro::MACRO_COUNT) return;

    const uint8_t macro = static_cast<uint8_t>(index);
    const auto& pageData = pages.pageData(track, page);
    const bool active = pageData.isMacroActive(macro);
    const uint8_t nextAdd = pageData.nextAddMacroIndex();
    const bool addSlot = !active && nextAdd == macro;
    const ModulationDestination destination{
        ModulationDestinationKind::MACRO_SLOT,
        track,
        page,
        macro,
    };
    const bool auditioned = pages.control.audition.active &&
        pages.control.audition.destination == destination;
    const ModulatorId effectiveSource = valid(sourceId)
        ? sourceId
        : (auditioned ? pages.control.audition.sourceId : ModulatorId{});
    bool alreadyAssigned = false;
    if (valid(effectiveSource)) {
        const auto& graph = pages.control.authored.modulation;
        for (uint16_t bindingIndex = 0;
             bindingIndex < graph.outputBindingCount;
             ++bindingIndex) {
            const auto& binding = graph.outputBindings[bindingIndex];
            if (binding.sourceId == effectiveSource &&
                binding.destination == destination) {
                alreadyAssigned = true;
                break;
            }
        }
    }
    uint8_t displayCc = pageData.cc[macro];
    if (addSlot) {
        const auto plan =
            core::state::macro::MacroWorkflow::planMacroSlotActivation(
                pages,
                {track, page, macro}
            );
        if (plan.valid) displayCc = plan.cc;
    }
    std::snprintf(
        out.key.data(),
        out.key.size(),
        addSlot ? PICKER_ADD_KEY_FORMAT : PICKER_KEY_FORMAT,
        static_cast<unsigned>(macro + 1U),
        static_cast<unsigned>(displayCc)
    );
    if (auditioned) {
        const auto* binding = findProjectModulationBinding(
            pages.control.authored.modulation,
            pages.control.audition.bindingId
        );
        const int16_t percent = binding
            ? core::ui::macro::lfo_audition::depthQ15ToPercent(
                  binding->amountQ15
              )
            : 0;
        std::snprintf(
            out.value.data(),
            out.value.size(),
            PICKER_PREVIEW_FORMAT,
            static_cast<int>(percent)
        );
    } else if (active) {
        setText(
            out.value,
            alreadyAssigned ? PICKER_ASSIGNED : PICKER_DEPTH_PREVIEW
        );
    } else if (addSlot) {
        setText(out.value, PICKER_CREATE_ASSIGN);
    } else if (nextAdd < core::state::macro::MACRO_COUNT) {
        std::snprintf(
            out.value.data(),
            out.value.size(),
            PICKER_PREREQUISITE_FORMAT,
            static_cast<unsigned>(nextAdd + 1U)
        );
    } else {
        setText(out.value, PICKER_UNAVAILABLE);
    }
    setText(
        out.icon,
        auditioned ? standalone::icons::ACTION_APPLY : standalone::icons::KNOB
    );
    out.iconFont = standalone_fonts.icons_14;
    out.iconColor = auditioned || ((active || addSlot) && !alreadyAssigned)
        ? standalone::theme::color::MACRO_MODULATION
        : standalone::theme::color::INACTIVE;
}

FLASHMEM uint32_t registryRevision(const ProjectControlState& control,
                                   uint8_t telemetryRevision,
                                   uint8_t focusedRow) {
    uint32_t revision = control.authoredRevision * 16777619U;
    revision ^= static_cast<uint32_t>(telemetryRevision) * 2246822519U;
    revision ^= static_cast<uint32_t>(focusedRow) << 24U;
    return revision == 0U ? 1U : revision;
}

}  // namespace core::ui::project::modulators

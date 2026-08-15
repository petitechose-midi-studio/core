#include "context/standalone/MacroOverlayPresenterFormatters.hpp"
#include "context/standalone/MacroOverlayPresenterFormatterInternals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

#include "sequencer/MidiCcGlobalFrameCoordinator.hpp"
#include "state/macro/MacroEditMenuModel.hpp"
#include "state/macro/MacroSourceDetailPolicy.hpp"
#include "state/modulation/ModulationDepthParameterMapping.hpp"
#include "state/modulation/ModulatorLfoParameterMapping.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/modulation/ModulatorLfoUiModel.hpp"
#include "ui/modulation/ModulatorSparklineModel.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone::macro_overlay_presenter {

namespace internal {

namespace mod_sparkline = core::ui::modulation::sparkline;
namespace menu = core::state::macro;
namespace depth_parameter = core::state::modulation::depth;
namespace lfo_parameter = core::state::modulation::lfo;

FLASHMEM const char* recordedShapeCaptureLabel(
    const core::state::modulation::ProjectRecordedShapeCaptureState& capture
) {
    using Status = core::state::modulation::
        ProjectRecordedShapeCaptureStatus;
    switch (capture.status) {
        case Status::ARMED:
            return "ARMED";
        case Status::RECORDING:
        case Status::REDUCED:
            return capture.take != nullptr && capture.take->touched
                ? "RECORDING"
                : "ARMED";
        case Status::COMMITTED:
            return "COMMITTED";
        case Status::NO_CHANGE:
            return "NO CHANGE";
        case Status::CANCELLED:
            return "CANCELLED";
        case Status::INVALIDATED:
        case Status::SCRATCH_UNAVAILABLE:
        case Status::COMMIT_FAILED:
            return "INVALIDATED";
        case Status::IDLE:
        default:
            return "HOLD + TURN";
    }
}

FLASHMEM int bindingDepthPercent(
    const core::state::modulation::ProjectControlState& control,
    const core::state::modulation::ModulationBindingState& binding
) {
    return depth_parameter::amountQ15ToPercent(
        binding.amountQ15,
        depth_parameter::scaleFor(
            control.authored.modulation,
            control.authored.curves,
            binding
        )
    );
}

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
    return core::ui::modulation::lfo::rateCompactLabel(index);
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
            lfo_parameter::rateIndex(
                modulator.parameters.lfo.periodTicks
            )
        );
    } else if (modulator.kind ==
               core::state::modulation::ModulatorKind::ADSR) {
        primary = "DAHDSR";
    }
    out.sparkline = mod_sparkline::buildSource(
        source->pages.control,
        modulator
    );
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
    const auto rows = menu::buildMacroModulationRows(graph, destination);
    uint16_t enabledCount = 0;
    for (uint16_t index = 0; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        if ((binding.flags & core::state::modulation::
                PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U) {
            ++enabledCount;
        }
    }
    if (rows.assignmentCount == 0U) return;
    const auto descriptor = menu::macroModulationRowAt(
        graph,
        rows,
        rowIndex
    );
    if (descriptor.kind == menu::MacroModulationRowKind::ALL) {
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
                : (enabledCount == rows.assignmentCount ? "On" : "Mixed")
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
    if (descriptor.kind == menu::MacroModulationRowKind::ADD_SOURCE) {
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

    const auto* selected = menu::macroModulationBinding(graph, descriptor);
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
    const int depth = bindingDepthPercent(source->pages.control, *selected);
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

FLASHMEM bool sampleMacroModulationSparkline(
    const ms::ui::KeyValueSparkline& descriptor,
    uint16_t positionQ16,
    uint16_t previousPositionQ16,
    bool hasPrevious,
    ms::ui::KeyValueSparklineSample& out
) {
    out = {};
    const auto* preview = static_cast<
        const core::ui::MacroEditorPreviewModel*>(descriptor.context);
    if (preview == nullptr || !preview->modulationStored) return false;

    core::ui::MacroEditorPreviewSample sample{};
    if (!core::ui::sampleMacroEditorPreview(
            *preview,
            core::ui::MacroEditorPreviewFocus::ALL_MODULATION,
            positionQ16,
            previousPositionQ16,
            hasPrevious,
            sample
        )) {
        return false;
    }
    out = {
        .valueQ16 = static_cast<uint16_t>(std::clamp<int32_t>(
            32768 + static_cast<int32_t>(sample.modulationQ15),
            0,
            65535
        )),
        .discontinuityBefore = sample.discontinuityBefore,
    };
    return true;
}

FLASHMEM uint32_t macroModulationGeometryRevision(
    const core::ui::MacroEditorPreviewModel& preview
) {
    if (preview.control == nullptr || !preview.modulationStored) return 0U;
    const auto& control = *preview.control;
    const auto destination = core::state::modulation::
        projectControlDestination(preview.address);
    const auto& graph = control.authored.modulation;
    uint32_t revision = mixRevision(
        0x4D4F4455UL,
        static_cast<uint32_t>(
            core::state::modulation::projectModulationDestinationScaleQ15(
                graph,
                destination
            )
        )
    );
    for (uint16_t index = 0U; index < graph.outputBindingCount; ++index) {
        const auto& binding = graph.outputBindings[index];
        if (binding.destination != destination) continue;
        revision = mixRevision(revision, binding.id.value);
        revision = mixRevision(revision, binding.sourceId.value);
        revision = mixRevision(
            revision,
            static_cast<uint16_t>(binding.amountQ15)
        );
        revision = mixRevision(
            revision,
            static_cast<uint8_t>(binding.application)
        );
        const auto* source = core::state::modulation::findProjectModulator(
            graph,
            binding.sourceId
        );
        if (source != nullptr) {
            revision = mixRevision(
                revision,
                mod_sparkline::sourceGeometryRevision(control, *source)
            );
        }
    }
    return revision == 0U ? 1U : revision;
}

FLASHMEM ms::ui::KeyValueSparkline buildModulationSparkline(
    const core::ui::MacroEditorPreviewModel& preview
) {
    if (preview.control == nullptr || !preview.modulationStored) return {};
    const auto& address = preview.address;
    return {
        .context = &preview,
        .identity =
            (static_cast<uint32_t>(address.track) << 16U) |
            (static_cast<uint32_t>(address.page) << 8U) |
            static_cast<uint32_t>(address.macro),
        .geometryRevision = macroModulationGeometryRevision(preview),
        .enabled = true,
        .centerLine = true,
        .curveColorRole = static_cast<uint8_t>(
            ms::ui::KeyValueSparklineColorRole::DATA
        ),
        .markerColorRole = static_cast<uint8_t>(
            ms::ui::KeyValueSparklineColorRole::LIVE
        ),
        .sampleProvider = &sampleMacroModulationSparkline,
        .markerProvider = nullptr,
    };
}

FLASHMEM void formatAutomationState(
    char* out,
    size_t outSize,
    const core::state::modulation::ProjectControlMacroDestinationView* slot,
    bool manual
) {
    if (out == nullptr || outSize == 0) return;
    if (slot == nullptr || !slot->automation.stored()) {
        std::snprintf(out, outSize, "%s", "Off");
        return;
    }
    if (!slot->automation.enabled) {
        std::snprintf(out, outSize, "%s", "Stored · Off");
        return;
    }
    std::snprintf(out, outSize, "%s", manual ? "Manual · Auto on" : "On");
}

FLASHMEM void formatModulationState(
    char* out,
    size_t outSize,
    const core::state::modulation::ProjectControlMacroDestinationView* slot
) {
    if (out == nullptr || outSize == 0) return;
    if (slot == nullptr || slot->modulationCount == 0U) {
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
    if (!slot->primaryModulation.enabled) {
        std::snprintf(out, outSize, "%s", "Stored · Off");
        return;
    }
    const int amount = static_cast<int>(std::lround(
        std::clamp(slot->primaryModulation.amount, -1.0f, 1.0f) * 100.0f
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

FLASHMEM core::state::macro::MacroSourceDetailContext sourceDetailContext(
    const core::state::modulation::ProjectControlMacroDestinationView* slot,
    bool manual
) {
    if (slot == nullptr) return {};
    return {
        .automationStored = slot->automation.stored(),
        .modulationStored = slot->modulationCount > 0U,
        .automationPlayback =
            slot->automation.stored() && slot->automation.enabled,
        .modulationPlayback = slot->activeModulationCount > 0U,
        .manualOverride = manual,
    };
}

FLASHMEM const char* modulationOriginLabel(
    core::state::modulation::ProjectCurveOrigin origin
) {
    switch (origin) {
        case core::state::modulation::ProjectCurveOrigin::CONVERTED_MEAN:
            return "From mean";
        case core::state::modulation::ProjectCurveOrigin::CONVERTED_FIRST:
            return "From first";
        case core::state::modulation::ProjectCurveOrigin::CONVERTED_MIN:
            return "From min";
        case core::state::modulation::ProjectCurveOrigin::NATIVE:
        default:
            return "Native";
    }
}

FLASHMEM const char* conversionPolicyLabel(
    core::state::modulation::ProjectAutomationConversionPolicy policy
) {
    switch (policy) {
        case core::state::modulation::ProjectAutomationConversionPolicy::FIRST:
            return "First";
        case core::state::modulation::ProjectAutomationConversionPolicy::MIN:
            return "Min";
        case core::state::modulation::ProjectAutomationConversionPolicy::MEAN:
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

}  // namespace internal

using namespace internal;

FLASHMEM void initializeStaticItems(StaticItems& items) {
    for (int i = 0; i < 128; ++i) {
        oc::type::text::formatUnsigned(items.ccLabels[i].data(), items.ccLabels[i].size(), i);
        items.ccItems[i] = items.ccLabels[i].data();
    }

}

core::ui::MacroEditorLiveValue buildEditLiveValue(
    const Source& source
) {
    const uint8_t macroIndex = source.macroEdit.editingIndex.get();
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = source.pages.currentActiveTrack(),
        .page = source.pages.currentActivePage(),
        .macro = macroIndex,
    };
    if (!source.macroUi.runtimeProjectionValidFor(
            address.track,
            address.page,
            address.macro
        )) {
        return {};
    }
    const auto& projection = source.macroUi.runtimeProjections[macroIndex];
    return {
        .base = projection.base,
        .modulation = projection.modulation,
        .out = projection.resolved,
        .timestampMs = source.pages.control.runtime.lastEvaluationMs,
        .valid = projection.valid,
        .clippedLow = projection.clippedLow,
        .clippedHigh = projection.clippedHigh,
    };
}
}  // namespace core::context::standalone::macro_overlay_presenter

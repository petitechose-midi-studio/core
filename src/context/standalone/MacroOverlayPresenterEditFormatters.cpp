#include "context/standalone/MacroOverlayPresenterFormatters.hpp"
#include "context/standalone/MacroOverlayPresenterFormatterInternals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "state/macro/MacroEditMenuModel.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/modulation/ModulationDepthUiModel.hpp"
#include "ui/modulation/ModulatorLfoUiModel.hpp"
#include "ui/macro/MacroSourceDetailLayout.hpp"
#include "ui/modulation/ModulatorSparklineModel.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone::macro_overlay_presenter {

using namespace internal;
namespace menu = core::state::macro;

FLASHMEM void buildEditRenderData(Source& source, EditRenderData& data) {
    if (data.previewSessionOpenedAtMs != source.macroEdit.openedAtMs) {
        const float tempo = source.statusBar != nullptr
            ? source.statusBar->tempo.get()
            : 120.0f;
        data.frozenTimelineTempoBpm = std::isfinite(tempo)
            ? std::clamp(tempo, 1.0f, 999.0f)
            : 120.0f;
        data.previewSessionOpenedAtMs = source.macroEdit.openedAtMs;
        data.previewRevision = UINT32_MAX;
    }
    const uint8_t macroIndex = source.macroEdit.editingIndex.get();
    const uint8_t cc = source.macroEdit.tempCC.get();
    const uint8_t trackChannel =
        core::state::project::projectTrackMidiChannel(
            source.projectTracks,
            source.pages.currentActiveTrack()
        );
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
        static_cast<unsigned>(trackChannel) + 1U,
        static_cast<unsigned>(cc)
    );
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = source.pages.currentActiveTrack(),
        .page = source.pages.currentActivePage(),
        .macro = macroIndex,
    };
    core::state::modulation::ProjectControlMacroDestinationView slotView{};
    const auto* slot =
        core::state::modulation::readProjectControlMacroDestination(
        source.pages.control,
        address,
        slotView
    ) ? &slotView : nullptr;
    const bool manualOverride =
        (source.macroUi.automationManualOverrideMask.get() &
         static_cast<uint16_t>(1U << macroIndex)) != 0;
    const bool automationStored =
        slot != nullptr && slot->automation.stored();
    const bool modulationStored =
        slot != nullptr && slot->modulationCount > 0U;
    core::state::modulation::ModulationBindingId focusedBindingId{};
    const core::state::modulation::ModulationBindingState* focusedBinding =
        nullptr;
    if (slot != nullptr && slot->modulationCount > 0U) {
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
    const auto& activeTake = source.macroUi.automationTake;
    const bool activeTakeForPreview =
        activeTake.phase ==
            core::state::macro::MacroAutomationTakePhase::RECORDING &&
        activeTake.track == address.track && activeTake.page == address.page &&
        activeTake.activeFor(macroIndex);
    previewRevision = mixRevision(
        previewRevision,
        activeTakeForPreview
            ? 0x80000000UL
            : 0U
    );
    if (activeTakeForPreview) {
        previewRevision = mixRevision(
            previewRevision,
            activeTake.durationTicks
        );
    }
    const auto& recordedShapeCapture =
        source.macroUi.recordedShapeCapture;
    previewRevision = mixRevision(
        previewRevision,
        source.macroUi.recordedShapeCaptureRevision.get()
    );
    if (recordedShapeCapture.active() &&
        recordedShapeCapture.take != nullptr) {
        previewRevision = mixRevision(
            previewRevision,
            recordedShapeCapture.take->scratchCurveRevision
        );
        previewRevision = mixRevision(
            previewRevision,
            static_cast<uint32_t>(recordedShapeCapture.durationTicks) |
                (static_cast<uint32_t>(recordedShapeCapture.mode) << 16U)
        );
        previewRevision = mixRevision(
            previewRevision,
            recordedShapeCapture.sourceId.value
        );
    }
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
            data.preview,
            data.frozenTimelineTempoBpm
        );
        if (activeTake.track == address.track &&
            activeTake.page == address.page) {
            core::ui::attachMacroAutomationTakePreview(
                activeTake,
                macroIndex,
                data.preview
            );
        }
        core::ui::attachProjectRecordedShapeCapturePreview(
            recordedShapeCapture,
            data.preview
        );
        data.previewRevision = previewRevision;
    }
    data.live = buildEditLiveValue(source);
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
    if (data.preview.recordedShapeCapture != nullptr) {
        std::snprintf(
            data.valueBuffers[2].data(),
            data.valueBuffers[2].size(),
            "%s",
            recordedShapeCaptureLabel(recordedShapeCapture)
        );
    }
    if (data.preview.recordedShapeCapture == nullptr &&
        slot != nullptr && focusedBinding != nullptr) {
        const auto* modulator = core::state::modulation::findProjectModulator(
            source.pages.control.authored.modulation,
            focusedBinding->sourceId
        );
        if (modulator != nullptr) {
            const int depth = bindingDepthPercent(
                source.pages.control,
                *focusedBinding
            );
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
    const auto focusedItem = menu::macroRootItemAt(
        source.macroEdit.focusedRow.get()
    );
    if (focusedItem == menu::MacroRootItem::DESTINATION) {
        std::snprintf(data.meta.data(), data.meta.size(), "Live · 2s");
    } else if (focusedItem == menu::MacroRootItem::AUTOMATION) {
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
    } else if (data.preview.recordedShapeCapture != nullptr) {
        std::array<char, 12> duration{};
        formatBeatDuration(
            duration.data(),
            duration.size(),
            recordedShapeCapture.durationTicks,
            "b"
        );
        std::snprintf(
            data.meta.data(),
            data.meta.size(),
            "Capture · %s",
            duration.data()
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
        const auto destination =
            core::state::modulation::projectControlDestination(address);
        const auto& graph = source.pages.control.authored.modulation;
        const auto modulationRows = menu::buildMacroModulationRows(
            graph,
            destination
        );
        const auto action = menu::macroContextActionAt(
            graph,
            modulationRows,
            focusedItem,
            source.macroEdit.contextPropertyIndex.get()
        );
        if (action.action == menu::MacroContextAction::DESTINATION_CC ||
            action.action ==
                menu::MacroContextAction::DESTINATION_CHANNEL) {
            const bool channel = action.action ==
                menu::MacroContextAction::DESTINATION_CHANNEL;
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
                        ? trackChannel + 1U
                        : source.macroEdit.tempCC.get()
                )
            );
        } else if (focusedItem == menu::MacroRootItem::AUTOMATION) {
            const char* label =
                action.action == menu::MacroContextAction::AUTOMATION_RECORD
                ? "RECORD MACRO"
                : (action.action == menu::MacroContextAction::AUTOMATION_PLAYBACK
                ? "PLAYBACK"
                : (action.action ==
                       menu::MacroContextAction::AUTOMATION_LENGTH
                    ? "LENGTH"
                    : (action.action ==
                           menu::MacroContextAction::AUTOMATION_OFFSET
                        ? "OFFSET"
                        : "CONVERT")));
            data.interactionIcon = ::standalone::icons::MACRO_AUTOMATION;
            data.interactionColor = ::standalone::theme::color::MACRO_AUTOMATION;
            std::snprintf(
                data.interactionLabel.data(),
                data.interactionLabel.size(),
                "%s",
                label
            );
            if (action.action == menu::MacroContextAction::AUTOMATION_RECORD) {
                const auto& take = source.macroUi.automationTake;
                const bool recording =
                    take.phase == core::state::macro::MacroAutomationTakePhase::RECORDING &&
                    take.track == address.track && take.page == address.page &&
                    take.activeFor(macroIndex);
                std::snprintf(
                    data.interactionValue.data(),
                    data.interactionValue.size(),
                    "%s",
                    recording ? "RECORDING" : "TURN OPT TO RECORD"
                );
            } else if (action.action ==
                menu::MacroContextAction::AUTOMATION_PLAYBACK) {
                const bool manual =
                    (source.macroUi.automationManualOverrideMask.get() &
                     static_cast<uint16_t>(1U << macroIndex)) != 0U;
                std::snprintf(
                    data.interactionValue.data(),
                    data.interactionValue.size(),
                    "%s",
                    manual
                        ? "TURN OPT FOR AUTO"
                        : (slot != nullptr && slot->automation.enabled
                            ? "ON"
                            : "OFF")
                );
            } else if (action.action ==
                           menu::MacroContextAction::AUTOMATION_LENGTH ||
                       action.action ==
                           menu::MacroContextAction::AUTOMATION_OFFSET) {
                const uint16_t ticks = slot == nullptr
                    ? 0U
                    : (action.action ==
                               menu::MacroContextAction::AUTOMATION_LENGTH
                        ? slot->automation.spec.durationTicks
                        : slot->automation.spec.windowOffsetTicks);
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
                    slot != nullptr && slot->automation.stored()
                        ? "TURN OPT TO PREVIEW"
                        : "NO AUTOMATION"
                );
            }
        } else {
            data.interactionIcon = ::standalone::icons::MACRO_MODULATION;
            data.interactionColor = ::standalone::theme::color::MACRO_MODULATION;
            if (action.action ==
                menu::MacroContextAction::MODULATION_RECORD_NEW_SHAPE) {
                data.interactionIcon = ::standalone::icons::MACRO_AUTOMATION;
                std::snprintf(
                    data.interactionLabel.data(),
                    data.interactionLabel.size(),
                    "RECORD NEW SHAPE"
                );
                std::snprintf(
                    data.interactionValue.data(),
                    data.interactionValue.size(),
                    "%s",
                    recordedShapeCaptureLabel(recordedShapeCapture)
                );
            } else if (action.action ==
                menu::MacroContextAction::MODULATION_EDGE_DEPTH) {
                const auto* binding = menu::macroModulationBinding(
                    graph,
                    {
                        menu::MacroModulationRowKind::ASSIGNMENT,
                        action.bindingId,
                        modulationRows.destination,
                    }
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
                    ? bindingDepthPercent(source.pages.control, *binding)
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
            slot->automation.pointCount
        );
        revision = mixRevision(
            revision,
            slot->primaryModulation.recordedShape.pointCount
        );
        revision = mixRevision(
            revision,
            static_cast<uint32_t>(slot->automation.enabled)
        );
        revision = mixRevision(
            revision,
            static_cast<uint32_t>(slot->activeModulationCount)
        );
        uint32_t depthBits = 0;
        std::memcpy(
            &depthBits,
            &slot->primaryModulation.amount,
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
            source.macroUi.automationEditRevision.get()
        ),
        data.previewRevision
    );

}

FLASHMEM EditRenderData buildEditRenderData(Source& source) {
    EditRenderData data{};
    buildEditRenderData(source, data);
    return data;
}
}  // namespace core::context::standalone::macro_overlay_presenter

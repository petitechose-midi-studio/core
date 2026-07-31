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
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "state/macro/MacroSourceDetailPolicy.hpp"
#include "ui/modulation/ModulatorSparklineModel.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::context::standalone::macro_overlay_presenter {

using namespace internal;
namespace menu = core::state::macro;

FLASHMEM void buildAutomationRenderData(
    const Source& source,
    AutomationRenderData& data
) {
    data = {};
    if (!source.macroEdit.automationVisible.get()) {
        return;
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
    core::state::modulation::ProjectControlMacroDestinationView slotView{};
    const auto* slot =
        core::state::modulation::readProjectControlMacroDestination(
        source.pages.control,
        address,
        slotView
    ) ? &slotView : nullptr;
    const bool automationStored =
        slot != nullptr && slot->automation.stored();
    const bool modulationStored =
        slot != nullptr && slot->modulationCount > 0U;
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
                  source.macroEdit.modulatorPickerIndex.get(),
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
        data.dataRevision = revision;
        data.visible = data.rowCount > 0;
        return;
    }

    if (phase == core::state::MacroEditFlowPhase::CONVERT_PREVIEW) {
        const auto& plan = source.macroEdit.conversionPreview.plan;
        std::snprintf(data.valueBuffers[0].data(), data.valueBuffers[0].size(), "%s", conversionPolicyLabel(plan.policy));
        std::snprintf(data.valueBuffers[1].data(), data.valueBuffers[1].size(), "%u%%", static_cast<unsigned>(plan.reference * 100.0f + 0.5f));
        std::snprintf(data.valueBuffers[2].data(), data.valueBuffers[2].size(), "%s", "Stored · Off");
        std::snprintf(data.valueBuffers[3].data(), data.valueBuffers[3].size(), "%s", "On · 100%");
        std::snprintf(
            data.valueBuffers[4].data(), data.valueBuffers[4].size(), "%s",
            plan.status == core::state::modulation::
                ProjectAutomationConversionStatus::READY
                ? "Tap apply"
                : (plan.status == core::state::modulation::
                       ProjectAutomationConversionStatus::OVERWRITE_REQUIRED
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
        return;
    }

    if (phase == core::state::MacroEditFlowPhase::MODULATION &&
        modulationStored) {
        const auto destination =
            core::state::modulation::projectControlDestination(address);
        const auto rows = menu::buildMacroModulationRows(
            source.pages.control.authored.modulation,
            destination
        );
        const uint16_t count = rows.assignmentCount;
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
        data.rowCount = rows.rowCount();
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
        return;
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
            {.key = "New DAHDSR", .value = data.valueBuffers[1].data(), .icon = ::standalone::icons::NOTE_PROP_GATE, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION},
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
        return;
    }

    const auto detailContext = sourceDetailContext(slot, manualOverride);
    if (phase == core::state::MacroEditFlowPhase::AUTOMATION) {
        formatAutomationState(
            data.valueBuffers[0].data(),
            data.valueBuffers[0].size(),
            slot,
            manualOverride
        );
        if (automationStored) {
            formatBeatDuration(
                data.valueBuffers[2].data(),
                data.valueBuffers[2].size(),
                slot->automation.spec.durationTicks,
                " beats"
            );
            formatBeatDuration(
                data.valueBuffers[3].data(),
                data.valueBuffers[3].size(),
                slot->automation.spec.windowOffsetTicks,
                " beats"
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
            core::state::macro::buildAutomationDetailPolicy(detailContext);
        for (uint8_t i = 0; i < layout.count; ++i) {
            ms::ui::KeyValueRow row{};
            switch (layout.items[i]) {
                case core::state::macro::AutomationDetailItem::PLAYBACK:
                    row = {.key = "Playback", .value = data.valueBuffers[0].data(), .icon = ::standalone::icons::MACRO_AUTOMATION, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_AUTOMATION};
                    break;
                case core::state::macro::AutomationDetailItem::RESUME:
                    row = {.key = "Automation", .value = data.valueBuffers[5].data(), .icon = ::standalone::icons::STATUS_RESUME, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_AUTOMATION};
                    break;
                case core::state::macro::AutomationDetailItem::CONVERT_TO_MODULATION:
                    row = {.key = "Convert to Mod", .value = data.valueBuffers[1].data(), .icon = ::standalone::icons::STATUS_PREVIEW, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION};
                    break;
                case core::state::macro::AutomationDetailItem::LENGTH:
                    row = {.key = "Length", .value = data.valueBuffers[2].data(), .icon = ::standalone::icons::LENGTH, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::STEP_LENGTH};
                    break;
                case core::state::macro::AutomationDetailItem::OFFSET:
                    row = {.key = "Offset", .value = data.valueBuffers[3].data(), .icon = ::standalone::icons::OFFSET, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::STEP_OFFSET};
                    break;
                case core::state::macro::AutomationDetailItem::INVALID:
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
            std::abs(slot->primaryModulation.amount) < 0.0001f;
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
                        slot->primaryModulation.amount,
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
            } else if (!slot->primaryModulation.isRecordedShape()) {
                modulationDomainLabel = "Centered LFO";
            } else {
                const auto* curve = core::state::modulation::findProjectCurve(
                    source.pages.control.authored.curves,
                    slot->primaryModulation.recordedShape.id
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
                    : (slot->primaryModulation.isRecordedShape()
                        ? modulationOriginLabel(
                            slot->primaryModulation.recordedShape.spec.origin
                        )
                        : "Generated"))
        );
        if (modulationStored) {
            core::ui::buildMacroEditorPreviewModel(
                source.pages.activePageData().values[macroIndex],
                source.pages.control,
                address,
                manualOverride,
                data.modulationPreview
            );
        }
        const auto modulationSparkline = modulationStored
            ? buildModulationSparkline(data.modulationPreview)
            : ms::ui::KeyValueSparkline{};
        const auto layout =
            core::state::macro::buildModulationDetailPolicy(detailContext);
        for (uint8_t i = 0; i < layout.count; ++i) {
            ms::ui::KeyValueRow row{};
            switch (layout.items[i]) {
                case core::state::macro::ModulationDetailItem::PLAYBACK:
                    row = {.key = "Playback", .value = data.valueBuffers[0].data(), .icon = paused ? ::standalone::icons::STATUS_PAUSED : ::standalone::icons::MACRO_MODULATION, .iconFont = standalone_fonts.icons_14, .iconColor = paused ? ::standalone::theme::color::MACRO_PAUSED : ::standalone::theme::color::MACRO_MODULATION};
                    break;
                case core::state::macro::ModulationDetailItem::DEPTH:
                    row = {.key = "Depth", .value = data.valueBuffers[1].data(), .icon = paused ? ::standalone::icons::STATUS_PAUSED : ::standalone::icons::KNOB, .iconFont = standalone_fonts.icons_14, .iconColor = paused ? ::standalone::theme::color::MACRO_PAUSED : ::standalone::theme::color::MACRO_MODULATION};
                    break;
                case core::state::macro::ModulationDetailItem::SHAPE:
                    row = {.key = "Shape", .value = data.valueBuffers[2].data(), .icon = ::standalone::icons::MACRO_MODULATION, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::MACRO_MODULATION, .sparkline = modulationSparkline};
                    break;
                case core::state::macro::ModulationDetailItem::ORIGIN:
                    row = {.key = "Origin", .value = data.valueBuffers[3].data(), .icon = ::standalone::icons::STATUS_PREVIEW, .iconFont = standalone_fonts.icons_14, .iconColor = ::standalone::theme::color::TEXT_SECONDARY};
                    break;
                case core::state::macro::ModulationDetailItem::INVALID:
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
            slot->automation.pointCount
        );
        revision = mixRevision(
            revision,
            slot->automation.spec.durationTicks
        );
        revision = mixRevision(
            revision,
            slot->automation.spec.sourceDurationTicks
        );
        revision = mixRevision(
            revision,
            slot->automation.spec.windowOffsetTicks
        );
    }
    if (modulationStored) {
        revision = mixRevision(revision, slot->modulationCount);
        revision = mixRevision(revision, slot->activeModulationCount);
        uint32_t depthBits = 0;
        std::memcpy(
            &depthBits,
            &slot->primaryModulation.amount,
            sizeof(depthBits)
        );
        revision = mixRevision(revision, depthBits);
    }
    revision = mixRevision(revision, source.pages.control.authoredRevision);
    data.dataRevision = mixRevision(
        revision,
        source.macroUi.automationEditRevision.get()
    );
    data.visible = true;
}
}  // namespace core::context::standalone::macro_overlay_presenter

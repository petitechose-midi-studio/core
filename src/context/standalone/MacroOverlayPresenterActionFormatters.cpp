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
    const auto item = menu::macroRootItemAt(
        source.macroEdit.focusedRow.get()
    );
    const auto address = currentAddress(source);
    core::state::modulation::ProjectControlMacroDestinationView slot{};
    const bool slotReadable =
        core::state::modulation::readProjectControlMacroDestination(
        source.pages.control,
        address,
        slot
    );
    const bool automationStored =
        slotReadable && slot.automation.stored();
    const bool modulationStored =
        slotReadable && slot.modulationCount > 0U;
    const bool automationPlayback =
        automationStored && slot.automation.enabled;
    const bool modulationPlayback = modulationStored &&
        slot.activeModulationCount > 0U;

    if (item == menu::MacroRootItem::DESTINATION) {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            ::standalone::icons::ACTION_REMOVE,
            Visual::ACTIVE,
            Tone::DESTRUCTIVE
        );
        props.slots[1] = scopeLabel("Destination");
    } else {
        const bool automation = item == menu::MacroRootItem::AUTOMATION;
        const bool stored = automation ? automationStored : modulationStored;
        const bool playback = automation
            ? automationPlayback
            : modulationPlayback;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            playback
                ? (automation ? ::standalone::icons::MACRO_AUTOMATION
                              : ::standalone::icons::MACRO_MODULATION)
                : ::standalone::icons::STATUS_PAUSED,
            stored ? Visual::ACTIVE : Visual::DISABLED,
            Tone::NEUTRAL
        );
        props.slots[1] = scopeLabel(
            automation ? "Automation" : "Modulation"
        );
    }
    const bool canCopy = item == menu::MacroRootItem::DESTINATION ||
        (item == menu::MacroRootItem::AUTOMATION
            ? automationStored
            : modulationStored);
    const bool canPasteAssignment =
        item == menu::MacroRootItem::MODULATION && !modulationStored &&
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
    using Status =
        core::state::modulation::ProjectAutomationConversionStatus;
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
    core::state::modulation::ProjectControlMacroDestinationView slot{};
    const bool slotReadable =
        core::state::modulation::readProjectControlMacroDestination(
        source.pages.control,
        address,
        slot
    );
    const bool modulation = phase == core::state::MacroEditFlowPhase::MODULATION;
    const bool automationStored =
        slotReadable && slot.automation.stored();
    const bool modulationStored =
        slotReadable && slot.modulationCount > 0U;
    if (modulation && modulationStored) {
        const auto destination =
            core::state::modulation::projectControlDestination(address);
        const auto& graph = source.pages.control.authored.modulation;
        const auto rows = menu::buildMacroModulationRows(graph, destination);
        const int row = std::clamp(
            static_cast<int>(source.macroEdit.modulationFocusedRow.get()),
            0,
            rows.addSourceRow()
        );
        const auto descriptor = menu::macroModulationRowAt(
            graph,
            rows,
            row
        );
        if (descriptor.kind == menu::MacroModulationRowKind::ADD_SOURCE) {
            props.slots[0].visualState = Visual::HIDDEN;
            props.slots[1] = scopeLabel("Add source");
            props.slots[2].visualState = Visual::HIDDEN;
            return props;
        }
        if (descriptor.kind == menu::MacroModulationRowKind::ALL) {
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

        const auto* binding = menu::macroModulationBinding(graph, descriptor);
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
        : slot.automation.enabled);
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
}  // namespace core::context::standalone::macro_overlay_presenter

#include "context/standalone/ux/StandaloneUxSurfaces.hpp"

#if defined(MS_UX_RECORDER)

#include <cstdio>
#include <cstring>

#include "config/InputIDs.hpp"
#include "context/standalone/MacroOverlayPresenterFormatters.hpp"
#include "state/MacroEditState.hpp"
#include "state/MacroState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "validation/ux/SemanticUxTraceState.hpp"

namespace core::context::standalone::ux {
namespace {

bool isMacroEncoderTurn(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Encoder &&
           Config::macroEncoderIndex(event.encoderId, index);
}

bool isMacroButtonLongPress(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonType == oc::core::input::ButtonBindingType::LONG_PRESS &&
           Config::macroButtonIndex(event.buttonId, index);
}

bool isMacroButtonPress(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonType == oc::core::input::ButtonBindingType::PRESS &&
           Config::macroButtonIndex(event.buttonId, index);
}

bool isMacroButtonRelease(const oc::core::input::InputBindingTraceEvent& event, uint8_t& index) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonType == oc::core::input::ButtonBindingType::RELEASE &&
           Config::macroButtonIndex(event.buttonId, index);
}

bool isButton(const oc::core::input::InputBindingTraceEvent& event,
              Config::ButtonID button,
              oc::core::input::ButtonBindingType type) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonId == static_cast<oc::type::ButtonID>(button) &&
           event.buttonType == type;
}

bool isEncoder(const oc::core::input::InputBindingTraceEvent& event, Config::EncoderID encoder) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Encoder &&
           event.encoderId == static_cast<oc::type::EncoderID>(encoder);
}

void copyValueLabel(char (&out)[16], const char* value) {
    if (!value) return;
    std::snprintf(out, sizeof(out), "%s", value);
}

void copyIndexLabel(char (&out)[16], unsigned value) {
    std::snprintf(out, sizeof(out), "%u", value + 1U);
}

void copyPointCountLabel(char (&out)[16], unsigned value) {
    std::snprintf(out, sizeof(out), "%u pts", value);
}

const char* macroPerformancePropertyName(core::state::macro::MacroPerformanceProperty property) {
    switch (property) {
        case core::state::macro::MacroPerformanceProperty::CC:
            return "CC";
        case core::state::macro::MacroPerformanceProperty::AUTOMATION:
            return "Automation";
        case core::state::macro::MacroPerformanceProperty::VALUE:
        default:
            return "Value";
    }
}

const char* macroPerformanceEffect(core::state::macro::MacroPerformanceProperty property) {
    switch (property) {
        case core::state::macro::MacroPerformanceProperty::CC:
            return "edit_macro_cc";
        case core::state::macro::MacroPerformanceProperty::AUTOMATION:
            return "restore_macro_automation";
        case core::state::macro::MacroPerformanceProperty::VALUE:
        default:
            return "edit_macro_value";
    }
}

const char* structureTarget(core::state::StructureNavigationFocus focus) {
    switch (focus) {
        case core::state::StructureNavigationFocus::TRACK:
            return "track";
        case core::state::StructureNavigationFocus::STEP:
            return "macro";
        case core::state::StructureNavigationFocus::PAGE:
        default:
            return "page";
    }
}

const char* structureTarget(core::state::StructureSelectionScope scope) {
    switch (scope) {
        case core::state::StructureSelectionScope::TRACK:
            return "track";
        case core::state::StructureSelectionScope::PAGE:
        default:
            return "page";
    }
}

void fillSelectedItem(core::validation::ux::SemanticUxContext& out,
                      const core::context::standalone::macro_overlay_presenter::SelectorRenderData& data) {
    out.targetIndex = static_cast<int16_t>(data.selectedIndex);
    if (data.items && data.selectedIndex >= 0 && data.selectedIndex < data.itemCount) {
        out.property = data.items[data.selectedIndex];
        copyValueLabel(out.valueLabel, data.items[data.selectedIndex]);
    }
}

bool isAddSlot(const core::validation::ux::SemanticUxContext& out) {
    return out.property && std::strcmp(out.property, "add_slot") == 0;
}

void markNoop(core::validation::ux::SemanticUxContext& out, const char* reason) {
    out.outcome = "noop";
    out.reason = reason;
}

void markIgnored(core::validation::ux::SemanticUxContext& out, const char* reason) {
    out.effect = "release_ignored";
    out.outcome = "ignored";
    out.reason = reason;
}

}  // namespace

MacroValueUxSurface::MacroValueUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::MacroState& macros,
    core::state::macro::MacroPagesState& pages,
    core::state::macro::MacroUiState& macroUi,
    core::state::MacroEditState& macroEdit
) : active_view_(activeView),
    macros_(macros),
    pages_(pages),
    macro_ui_(macroUi),
    macro_edit_(macroEdit) {}

bool MacroValueUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::MACRO || macro_edit_.visible.get()) {
        return false;
    }

    uint8_t index = 0;
    if (isMacroButtonPress(event, index) && index < Config::MACRO_COUNT &&
        macro_ui_.clutchActive.get() &&
        macro_ui_.activeProperty.get() ==
            core::state::macro::MacroPerformanceProperty::AUTOMATION) {
        out.mode = "macro.performance";
        out.target = "macro";
        out.targetIndex = static_cast<int16_t>(index);
        out.property = "Automation";
        out.effect = "restore_macro_automation";
        copyValueLabel(out.valueLabel, "Auto");
        return true;
    }

    if (isMacroButtonPress(event, index) && index < Config::MACRO_COUNT) {
        out.mode = "macro.automation";
        out.target = "macro";
        out.targetIndex = static_cast<int16_t>(index);
        out.property = "Automation";
        out.effect = "arm_macro_automation";
        copyValueLabel(out.valueLabel, macro_ui_.automationRecording.active ? "REC" : "Arm");
        return true;
    }

    if (isMacroButtonRelease(event, index) && index < Config::MACRO_COUNT) {
        out.mode = "macro.automation";
        out.target = "macro";
        out.targetIndex = static_cast<int16_t>(index);
        out.property = "Automation";
        out.effect = "commit_macro_automation";
        if (macro_ui_.automationRecording.active) {
            copyPointCountLabel(out.valueLabel, macro_ui_.automationRecording.lane.pointCount);
        } else {
            copyValueLabel(out.valueLabel, "Done");
        }
        return true;
    }

    if (!isMacroEncoderTurn(event, index) || index >= Config::MACRO_COUNT) {
        return false;
    }

    if (macro_ui_.automationRecording.active &&
        macro_ui_.automationRecording.address.macro == index) {
        out.mode = "macro.automation";
        out.target = "macro";
        out.targetIndex = static_cast<int16_t>(index);
        out.property = "Automation";
        out.effect = "record_macro_automation_point";
        copyValueLabel(out.valueLabel, macros_.slots[index].displayValue.get());
        return true;
    }

    const auto property = macro_ui_.clutchActive.get()
        ? macro_ui_.activeProperty.get()
        : core::state::macro::MacroPerformanceProperty::VALUE;
    out.mode = "macro";
    out.target = "macro";
    out.targetIndex = static_cast<int16_t>(index);
    out.property = macroPerformancePropertyName(property);
    out.effect = macroPerformanceEffect(property);

    switch (property) {
        case core::state::macro::MacroPerformanceProperty::CC:
            std::snprintf(
                out.valueLabel,
                sizeof(out.valueLabel),
                "%u",
                static_cast<unsigned>(pages_.activeConfigs[index].cc)
            );
            break;
        case core::state::macro::MacroPerformanceProperty::AUTOMATION: {
            const auto address = core::state::macro::MacroAutomationSlotAddress{
                .track = pages_.currentActiveTrack(),
                .page = pages_.currentActivePage(),
                .macro = index,
            };
            const auto* slot = core::state::macro::macroAutomationFindSlot(
                pages_.automation,
                address
            );
            const bool active = slot != nullptr && slot->automation.active;
            const bool manual =
                (macro_ui_.automationManualOverrideMask.get() &
                 static_cast<uint16_t>(1U << index)) != 0;
            copyValueLabel(out.valueLabel, !active ? "Off" : (manual ? "Manual" : "Auto"));
            break;
        }
        case core::state::macro::MacroPerformanceProperty::VALUE:
        default:
            copyValueLabel(out.valueLabel, macros_.slots[index].displayValue.get());
            break;
    }
    return true;
}

MacroPerformanceUxSurface::MacroPerformanceUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::macro::MacroUiState& macroUi,
    core::state::MacroEditState& macroEdit
) : active_view_(activeView), macro_ui_(macroUi), macro_edit_(macroEdit) {}

bool MacroPerformanceUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::MACRO || macro_edit_.visible.get()) {
        return false;
    }

    const bool opening =
        isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::PRESS);
    if (!opening && !macro_ui_.clutchActive.get()) {
        return false;
    }

    const auto property = macro_ui_.activeProperty.get();
    out.mode = "macro.performance";
    out.target = "macro_property";
    out.targetIndex = static_cast<int16_t>(core::state::macro::performancePropertyIndex(property));
    out.property = macroPerformancePropertyName(property);

    if (opening) {
        out.effect = "open_macro_slot_property_selector";
    } else if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = "select_macro_property";
    } else if (isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "apply_macro_slot_property_selector";
    } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "cancel_macro_slot_property_selector";
    }
    return true;
}

MacroStructureUxSurface::MacroStructureUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus,
    core::state::TrackNavigationState& trackNavigation,
    core::state::StructureClipboardState& structureClipboard,
    core::state::macro::MacroUiState& macroUi,
    core::state::macro::MacroPagesState& pages,
    core::state::MacroEditState& macroEdit,
    const core::validation::ux::StructureUxTraceState* traceState
) : active_view_(activeView),
    navigation_focus_(navigationFocus),
    track_navigation_(trackNavigation),
    structure_clipboard_(structureClipboard),
    macro_ui_(macroUi),
    pages_(pages),
    macro_edit_(macroEdit),
    trace_state_(traceState) {}

bool MacroStructureUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::MACRO || macro_edit_.visible.get() ||
        macro_ui_.clutchActive.get()) {
        return false;
    }

    const bool leftTopRelease =
        isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE);
    const bool structureEvent =
        isEncoder(event, Config::EncoderID::NAV) ||
        isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE) ||
        isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::LONG_PRESS) ||
        leftTopRelease ||
        isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS) ||
        isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE) ||
        isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::LONG_PRESS) ||
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS) ||
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE) ||
        isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::LONG_PRESS);
    if (!structureEvent) {
        return false;
    }

    const bool selectionActive =
        track_navigation_.selection.active.get() || macro_ui_.pageSelection.active.get();
    if (leftTopRelease && !selectionActive) {
        return false;
    }

    const auto focus = navigation_focus_.get();
    auto scope = core::state::selectionScopeForFocus(focus);
    if (track_navigation_.selection.active.get()) {
        scope = track_navigation_.selection.scope.get();
    } else if (macro_ui_.pageSelection.active.get()) {
        scope = macro_ui_.pageSelection.scope.get();
    }

    out.mode = selectionActive ? "macro.structure_selection" : "macro.structure";
    out.target = selectionActive ? structureTarget(scope) : structureTarget(focus);

    uint8_t index = 0;
    const bool targetMacro =
        !selectionActive && focus == core::state::StructureNavigationFocus::STEP;
    if (targetMacro) {
        index = macro_ui_.focusedMacroSlot.get();
    }
    const bool targetTrack =
        selectionActive ? scope == core::state::StructureSelectionScope::TRACK
                        : focus == core::state::StructureNavigationFocus::TRACK;
    const uint16_t targetMask =
        targetMacro ? pages_.activePageData().activeMacroMask
                    : (targetTrack ? pages_.currentTrackEnabledMask()
                                   : pages_.currentEnabledPageMask());
    const bool canPaste = targetMacro ? (!pages_.isMacroAddSlot(index) &&
                                         structure_clipboard_.hasMacroAutomation())
                        : (targetTrack ? structure_clipboard_.hasMacroTrack()
                                       : structure_clipboard_.hasMacroPage());
    out.targetMask = targetMask;

    if (targetMacro) {
        out.property = pages_.isMacroAddSlot(index) ? "add_slot" : "existing";
    } else if (targetTrack) {
        index = track_navigation_.selection.active.get()
            ? track_navigation_.selection.cursorIndex.get()
            : track_navigation_.previewTrackIndex.get();
        out.property = track_navigation_.previewAddSlot.get() && !selectionActive
            ? "add_slot"
            : (selectionActive ? "selection" : "existing");
    } else {
        index = macro_ui_.pageSelection.active.get()
            ? macro_ui_.pageSelection.cursorIndex.get()
            : macro_ui_.previewPageIndex.get();
        out.property = macro_ui_.previewAddPageSlot.get() && !selectionActive
            ? "add_slot"
            : (selectionActive ? "selection" : "existing");
    }
    out.targetIndex = static_cast<int16_t>(index);
    copyIndexLabel(out.valueLabel, index);

    if (selectionActive) {
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "navigate_selection";
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "toggle_selection";
        } else if (leftTopRelease) {
            out.effect = "cancel_selection";
        } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "delete_selection";
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "duplicate_selection";
        }
        return true;
    }

    if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = "preview_structure";
    } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = "enter_selection";
    } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = (focus == core::state::StructureNavigationFocus::PAGE &&
                      macro_ui_.previewAddPageSlot.get()) ||
                             (focus == core::state::StructureNavigationFocus::TRACK &&
                              track_navigation_.previewAddSlot.get())
                         ? "create_structure"
                         : "switch_structure_focus";
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::PRESS)) {
        out.effect = targetMacro ? "arm_macro_automation_remove" : "arm_remove";
        if (isAddSlot(out) ||
            (!targetMacro &&
             core::state::shared::countEnabled(
                 targetMask,
                 targetTrack ? core::state::macro::TRACK_COUNT
                             : core::state::macro::PAGE_COUNT
             ) <= 1U)) {
            markNoop(out, isAddSlot(out) ? "add_slot" : "single_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = targetMacro ? "clear_macro_automation" : "erase_structure";
        if (trace_state_ && trace_state_->ignoreNextBottomLeftRelease) {
            markIgnored(out, "after_long_press");
        } else if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = targetMacro ? "remove_macro_automation" : "remove_structure";
        if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS)) {
        out.effect = targetMacro ? "arm_macro_automation_paste" : "arm_paste";
        if (!canPaste) {
            markNoop(out, "clipboard_empty");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = targetMacro ? "copy_macro_automation" : "copy_structure";
        if (trace_state_ && trace_state_->ignoreNextBottomRightRelease) {
            markIgnored(out, "after_long_press");
        } else if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = targetMacro ? "paste_macro_automation" : "paste_structure";
        if (!canPaste) {
            markNoop(out, "clipboard_empty");
        }
    }
    return true;
}

MacroEditUxSurface::MacroEditUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::MacroEditState& macroEdit,
    core::state::macro::MacroPagesState& pages,
    core::state::macro::MacroUiState& macroUi,
    oc::state::Signal<uint32_t>& configRevision
) : active_view_(activeView),
    macro_edit_(macroEdit),
    pages_(pages),
    macro_ui_(macroUi),
    config_revision_(configRevision) {
    core::context::standalone::macro_overlay_presenter::initializeStaticItems(static_items_);
}

bool MacroEditUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    uint8_t openingIndex = 0;
    uint8_t closingIndex = 0;
    const bool opening = active_view_.get() == core::ui::ViewType::MACRO &&
                         isMacroButtonLongPress(event, openingIndex);
    const bool macroButtonClose = macro_edit_.visible.get() &&
                                  isMacroButtonRelease(event, closingIndex) &&
                                  closingIndex == macro_edit_.editingIndex.get();
    if (!opening && !macro_edit_.visible.get()) {
        return false;
    }

    core::context::standalone::macro_overlay_presenter::Source source{
        macro_edit_,
        pages_,
        macro_ui_,
        config_revision_,
    };

    if (opening) {
        out.mode = "macro.edit";
        out.target = "macro";
        out.targetIndex = static_cast<int16_t>(openingIndex);
        out.effect = "open_macro_edit";
        copyIndexLabel(out.valueLabel, openingIndex);
        return true;
    }

    const auto phase = macro_edit_.flowPhase.get();
    if (phase == core::state::MacroEditFlowPhase::EDIT) {
        const auto data = core::context::standalone::macro_overlay_presenter::buildEditRenderData(source);
        const int row = data.selectedIndex;
        out.mode = "macro.edit";
        out.target = "macro_config";
        out.targetIndex = static_cast<int16_t>(macro_edit_.editingIndex.get());
        if (row >= 0 && row < static_cast<int>(data.rows.size())) {
            out.property = data.rows[row].key;
            copyValueLabel(out.valueLabel, data.rows[row].value);
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "focus_macro_config";
        } else if (isEncoder(event, Config::EncoderID::OPT)) {
            out.effect = "edit_macro_config";
        } else if (macroButtonClose) {
            out.effect = "apply_macro_edit";
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "open_macro_config_value";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "close_macro_edit";
        } else if (isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "open_macro_page_selector";
        } else if (isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "open_macro_target_selector";
        }
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::VALUE_SELECTOR) {
        const auto data =
            core::context::standalone::macro_overlay_presenter::buildEditSelectorRenderData(
                source,
                static_items_
            );
        if (!data.visible) return false;
        out.mode = "macro.edit.selector";
        out.target = "macro_config_value";
        out.property = data.meta;
        fillSelectedItem(out, data);
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_macro_config_value";
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "apply_macro_config_value";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "cancel_macro_config_value";
        }
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::PAGE_SELECTOR) {
        const auto data =
            core::context::standalone::macro_overlay_presenter::buildPageSelectorRenderData(source);
        if (!data.visible) return false;
        out.mode = "macro.page_selector";
        out.target = "page";
        fillSelectedItem(out, data);
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_macro_page";
        } else if (isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "apply_macro_page";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "cancel_macro_page";
        }
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::TARGET_SELECTOR) {
        const auto data =
            core::context::standalone::macro_overlay_presenter::buildTargetSelectorRenderData(
                source,
                static_items_
            );
        if (!data.visible) return false;
        out.mode = "macro.target_selector";
        out.target = "macro";
        fillSelectedItem(out, data);
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_macro_target";
        } else if (isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "apply_macro_target";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "cancel_macro_target";
        }
        return true;
    }

    if (phase == core::state::MacroEditFlowPhase::AUTOMATION) {
        const auto data =
            core::context::standalone::macro_overlay_presenter::buildAutomationRenderData(source);
        if (!data.visible) return false;
        const int row = data.selectedIndex;
        out.mode = "macro.automation_editor";
        out.target = "automation";
        out.targetIndex = static_cast<int16_t>(macro_edit_.editingIndex.get());
        if (row >= 0 && row < static_cast<int>(data.rows.size())) {
            out.property = data.rows[row].key;
            copyValueLabel(out.valueLabel, data.rows[row].value);
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "focus_macro_automation";
        } else if (isEncoder(event, Config::EncoderID::OPT)) {
            out.effect = row == 0 ? "edit_macro_automation_state" : "edit_macro_automation_length";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "back_macro_automation";
        } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "clear_macro_automation";
        } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
            out.effect = "remove_macro_automation";
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "copy_macro_automation";
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
            out.effect = "paste_macro_automation";
        }
        return true;
    }

    return false;
}

}  // namespace core::context::standalone::ux

#endif

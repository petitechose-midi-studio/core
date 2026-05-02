#include "context/standalone/ux/StandaloneUxSurfaces.hpp"

#if defined(MS_UX_RECORDER)

#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "config/InputIDs.hpp"
#include "context/standalone/MacroOverlayPresenterFormatters.hpp"
#include "context/standalone/ux/StandaloneUxSurfaceUtils.hpp"
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

using detail::copyIndexLabel;
using detail::copyValueLabel;
using detail::isAddSlot;
using detail::isButton;
using detail::isEncoder;
using detail::isMacroButtonLongPress;
using detail::isMacroButtonRelease;
using detail::isMacroEncoderTurn;
using detail::markIgnored;
using detail::markNoop;
using detail::structureTarget;

FLASHMEM const char* macroPerformancePropertyName(
    core::state::macro::MacroPerformanceProperty property
) {
    switch (property) {
        case core::state::macro::MacroPerformanceProperty::CC:
            return "CC";
        case core::state::macro::MacroPerformanceProperty::CHANNEL:
            return "Channel";
        case core::state::macro::MacroPerformanceProperty::VALUE:
        default:
            return "Value";
    }
}

FLASHMEM const char* macroPerformanceEffect(
    core::state::macro::MacroPerformanceProperty property
) {
    switch (property) {
        case core::state::macro::MacroPerformanceProperty::CC:
            return "edit_macro_cc";
        case core::state::macro::MacroPerformanceProperty::CHANNEL:
            return "edit_macro_channel";
        case core::state::macro::MacroPerformanceProperty::VALUE:
        default:
            return "edit_macro_value";
    }
}

FLASHMEM const char* macroQuickControlName(core::state::macro::MacroQuickControlItem item) {
    switch (item) {
        case core::state::macro::MacroQuickControlItem::CC_OFFSET:
            return "CC Offset";
        case core::state::macro::MacroQuickControlItem::GLOBAL_CHANNEL:
        default:
            return "Global Channel";
    }
}

FLASHMEM void fillMacroQuickControlValueLabel(core::state::macro::MacroUiState& macroUi,
                                              core::state::macro::MacroQuickControlItem item,
                                              char (&out)[16]) {
    switch (item) {
        case core::state::macro::MacroQuickControlItem::CC_OFFSET:
            std::snprintf(out, sizeof(out), "%+d", static_cast<int>(macroUi.ccOffset.get()));
            return;
        case core::state::macro::MacroQuickControlItem::GLOBAL_CHANNEL:
        default:
            copyIndexLabel(out, macroUi.quickControlGlobalChannel.get());
            return;
    }
}

FLASHMEM void fillSelectedItem(
    core::validation::ux::SemanticUxContext& out,
    const core::context::standalone::macro_overlay_presenter::SelectorRenderData& data
) {
    out.targetIndex = static_cast<int16_t>(data.selectedIndex);
    if (data.items && data.selectedIndex >= 0 && data.selectedIndex < data.itemCount) {
        out.property = data.items[data.selectedIndex];
        copyValueLabel(out.valueLabel, data.items[data.selectedIndex]);
    }
}

}  // namespace

FLASHMEM MacroValueUxSurface::MacroValueUxSurface(
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

FLASHMEM bool MacroValueUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::MACRO || macro_edit_.visible.get() ||
        macro_ui_.quickControlsSelecting.get()) {
        return false;
    }

    uint8_t index = 0;
    if (!isMacroEncoderTurn(event, index) || index >= Config::MACRO_COUNT) {
        return false;
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
        case core::state::macro::MacroPerformanceProperty::CHANNEL:
            copyIndexLabel(out.valueLabel, pages_.activeConfigs[index].channel);
            break;
        case core::state::macro::MacroPerformanceProperty::VALUE:
        default:
            copyValueLabel(out.valueLabel, macros_.slots[index].displayValue.get());
            break;
    }
    return true;
}

FLASHMEM MacroPerformanceUxSurface::MacroPerformanceUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::macro::MacroUiState& macroUi,
    core::state::MacroEditState& macroEdit
) : active_view_(activeView), macro_ui_(macroUi), macro_edit_(macroEdit) {}

FLASHMEM bool MacroPerformanceUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::MACRO || macro_edit_.visible.get()) {
        return false;
    }

    const bool opening =
        isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::PRESS);
    const bool quickOpening =
        isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::PRESS);
    if (quickOpening || macro_ui_.quickControlsSelecting.get()) {
        const auto item = macro_ui_.focusedQuickControl.get();
        out.mode = "macro.quick_controls";
        out.target = "quick_control";
        out.targetIndex = static_cast<int16_t>(core::state::macro::quickControlIndex(item));
        out.property = macroQuickControlName(item);
        fillMacroQuickControlValueLabel(macro_ui_, item, out.valueLabel);
        if (quickOpening) {
            out.effect = "open_quick_controls";
        } else if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_quick_control";
        } else if (isEncoder(event, Config::EncoderID::OPT)) {
            out.effect = "edit_quick_control";
        } else if (isButton(event, Config::ButtonID::LEFT_CENTER, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "apply_quick_controls";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "cancel_quick_controls";
        }
        return true;
    }

    if (!opening && !macro_ui_.clutchActive.get()) {
        return false;
    }

    const auto property = macro_ui_.activeProperty.get();
    out.mode = "macro.performance";
    out.target = "macro_property";
    out.targetIndex = static_cast<int16_t>(core::state::macro::performancePropertyIndex(property));
    out.property = macroPerformancePropertyName(property);

    if (opening) {
        out.effect = "open_macro_clutch";
    } else if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = "select_macro_property";
    } else if (isButton(event, Config::ButtonID::LEFT_BOTTOM, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "apply_macro_clutch";
    }
    return true;
}

FLASHMEM MacroStructureUxSurface::MacroStructureUxSurface(
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

FLASHMEM bool MacroStructureUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (active_view_.get() != core::ui::ViewType::MACRO || macro_edit_.visible.get() ||
        macro_ui_.quickControlsSelecting.get() || macro_ui_.clutchActive.get()) {
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
    const bool targetTrack =
        selectionActive ? scope == core::state::StructureSelectionScope::TRACK
                        : focus == core::state::StructureNavigationFocus::TRACK;
    const uint16_t targetMask =
        targetTrack ? pages_.currentTrackEnabledMask() : pages_.currentEnabledPageMask();
    const bool canPaste = targetTrack ? structure_clipboard_.hasMacroTrack()
                                      : structure_clipboard_.hasMacroPage();
    out.targetMask = targetMask;

    if (targetTrack) {
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
        out.effect = "arm_remove";
        if (isAddSlot(out) ||
            core::state::shared::countEnabled(
                targetMask,
                targetTrack ? core::state::macro::TRACK_COUNT : core::state::macro::PAGE_COUNT
            ) <= 1U) {
            markNoop(out, isAddSlot(out) ? "add_slot" : "single_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "erase_structure";
        if (trace_state_ && trace_state_->ignoreNextBottomLeftRelease) {
            markIgnored(out, "after_long_press");
        } else if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = "remove_structure";
        if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::PRESS)) {
        out.effect = "arm_paste";
        if (!canPaste) {
            markNoop(out, "clipboard_empty");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "copy_structure";
        if (trace_state_ && trace_state_->ignoreNextBottomRightRelease) {
            markIgnored(out, "after_long_press");
        } else if (isAddSlot(out)) {
            markNoop(out, "add_slot");
        }
    } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.effect = "paste_structure";
        if (!canPaste) {
            markNoop(out, "clipboard_empty");
        }
    }
    return true;
}

FLASHMEM MacroEditUxSurface::MacroEditUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::MacroEditState& macroEdit,
    core::state::macro::MacroPagesState& pages,
    oc::state::Signal<uint32_t>& configRevision
) : active_view_(activeView),
    macro_edit_(macroEdit),
    pages_(pages),
    config_revision_(configRevision) {
    core::context::standalone::macro_overlay_presenter::initializeStaticItems(static_items_);
}

FLASHMEM bool MacroEditUxSurface::captureSemanticUxContext(
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

    return false;
}

}  // namespace core::context::standalone::ux

#endif

#include "context/standalone/ux/StandaloneUxSurfaces.hpp"

#if defined(MS_UX_RECORDER)

#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "config/InputIDs.hpp"
#include "state/DeviceSettingsState.hpp"
#include "state/MidiNoteDisplayState.hpp"
#include "state/MidiSyncState.hpp"
#include "state/StatusBarState.hpp"
#include "state/ViewSelectorItems.hpp"
#include "state/project/ProjectHistoryCoordinator.hpp"
#include "state/ViewSelectorState.hpp"
#include "state/settings/DeviceSettingsMenuModel.hpp"
#include "validation/ux/SemanticUxNames.hpp"

namespace core::context::standalone::ux {
namespace {

using ControllerIntent = core::state::interaction::ControllerIntent;

FLASHMEM bool isButton(const oc::core::input::InputBindingTraceEvent& event,
              Config::ButtonID button,
              oc::core::input::ButtonBindingType type) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Button &&
           event.buttonId == static_cast<oc::type::ButtonID>(button) &&
           event.buttonType == type;
}

FLASHMEM bool isEncoder(const oc::core::input::InputBindingTraceEvent& event, Config::EncoderID encoder) {
    return event.domain == oc::core::input::InputBindingTraceDomain::Encoder &&
           event.encoderId == static_cast<oc::type::EncoderID>(encoder);
}

FLASHMEM void copyValueLabel(char (&out)[16], const char* value) {
    if (!value) return;
    std::snprintf(out, sizeof(out), "%s", value);
}

constexpr const char* const MODE_ITEMS[] = {"MASTER", "SLAVE", "AUTO"};
constexpr const char* const FOLLOW_ITEMS[] = {"OFF", "ON"};
constexpr const char* const FALLBACK_ITEMS[] = {"150 ms", "250 ms", "500 ms", "750 ms", "1000 ms", "1500 ms", "2000 ms"};
constexpr const char* const LOCK_ITEMS[] = {"1", "2", "3", "4", "6", "8", "12", "24"};
constexpr const char* const NOTE_OCTAVE_ITEMS[] = {"C3", "C4", "C5"};

FLASHMEM void selectorItemsForRow(uint8_t row, const char* const*& items, int& itemCount) {
    switch (row) {
        case 0:
            items = MODE_ITEMS;
            itemCount = 3;
            break;
        case 1:
            items = FOLLOW_ITEMS;
            itemCount = 2;
            break;
        case 2:
            items = FALLBACK_ITEMS;
            itemCount = 7;
            break;
        case 3:
            items = LOCK_ITEMS;
            itemCount = 8;
            break;
        case 4:
            items = NOTE_OCTAVE_ITEMS;
            itemCount = 3;
            break;
        default:
            items = nullptr;
            itemCount = 0;
            break;
    }
}

}  // namespace

FLASHMEM ViewSelectorUxSurface::ViewSelectorUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::ViewSelectorState& viewSelector,
    core::state::project::ProjectHistoryCoordinator& history
) : active_view_(activeView), view_selector_(viewSelector), history_(history) {}

FLASHMEM bool ViewSelectorUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    // BOTTOM_CENTER is the product's reserved global Transport binding. Even
    // while this overlay is authoritative, leave that event to
    // TransportUxSurface so the semantic trace reports the action that
    // actually owned the gesture.
    if (isButton(
            event,
            Config::ButtonID::BOTTOM_CENTER,
            oc::core::input::ButtonBindingType::RELEASE
        )) {
        return false;
    }

    const bool opening =
        isButton(
            event,
            Config::ButtonID::LEFT_TOP,
            oc::core::input::ButtonBindingType::PRESS
        ) ||
        isButton(
            event,
            Config::ButtonID::LEFT_TOP,
            oc::core::input::ButtonBindingType::LONG_PRESS
        );
    const bool visible = view_selector_.visible.get();
    if (!opening && !visible) {
        return false;
    }

    int selected = visible ? view_selector_.selectedIndex.get()
                           : static_cast<int>(core::state::viewSelectorItemForView(active_view_.get()));
    if (selected < 0 || selected >= core::state::VIEW_SELECTOR_ITEM_COUNT) {
        selected = static_cast<int>(core::state::viewSelectorItemForView(active_view_.get()));
    }

    const bool undo = isButton(
        event,
        Config::ButtonID::LEFT_CENTER,
        oc::core::input::ButtonBindingType::RELEASE
    );
    const bool redo = isButton(
        event,
        Config::ButtonID::LEFT_BOTTOM,
        oc::core::input::ButtonBindingType::RELEASE
    );
    if (visible && (undo || redo)) {
        const auto* entry = undo ? history_.peekUndo() : history_.peekRedo();
        // The recorder asks once before and once after dispatch. After an Undo
        // the applied action is now the Redo top (and symmetrically for Redo),
        // so keep the semantic label stable across both snapshots.
        if (entry == nullptr) {
            entry = undo ? history_.peekRedo() : history_.peekUndo();
        }
        out.mode = "view_selector";
        out.target = "project_history";
        out.property = entry != nullptr
            ? core::state::project::ProjectHistoryCoordinator::actionLabel(*entry)
            : "Empty";
        copyValueLabel(out.valueLabel, out.property);
        out.effect = undo ? "undo_project_action" : "redo_project_action";
        out.intent = undo ? ControllerIntent::UNDO : ControllerIntent::REDO;
        return true;
    }

    const auto selectedItem = core::state::viewSelectorItemAt(selected);
    out.mode = "view_selector";
    out.target = "view";
    out.targetIndex = static_cast<int16_t>(selected);
    out.property = core::state::viewSelectorItemLabel(selectedItem);
    copyValueLabel(out.valueLabel, out.property);

    if (opening) {
        out.effect = "open_view_selector";
        // LEFT_TOP always moves toward the controller root. At the root, the
        // visible result is the View Selector rather than another back step.
        out.intent = ControllerIntent::BACK;
    } else if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = "select_view";
        out.intent = ControllerIntent::MOVE_FOCUS;
    } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE) ||
               isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "apply_view";
        out.intent = ControllerIntent::APPLY;
    }
    return true;
}

FLASHMEM DeviceSettingsUxSurface::DeviceSettingsUxSurface(
    core::state::DeviceSettingsState& deviceSettings,
    core::state::MidiSyncState& midiSync,
    core::state::MidiNoteDisplayState& midiNoteDisplay
)
    : device_settings_(deviceSettings)
    , midi_sync_(midiSync)
    , midi_note_display_(midiNoteDisplay) {}

FLASHMEM bool DeviceSettingsUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    const auto deviceSettingsPhase = device_settings_.flowPhase.get();
    if (deviceSettingsPhase == core::state::DeviceSettingsFlowPhase::VIEW) {
        const auto data = core::state::settings::buildDeviceSettingsMenuPage(
            device_settings_,
            core::state::settings::DeviceSettingsMenuContext{
                midi_sync_.mode.get(),
                midi_sync_.followTransport.get(),
                midi_sync_.autoFallbackMs.get(),
                midi_sync_.autoLockClockCount.get(),
                midi_note_display_.octaveConvention.get(),
                midi_sync_.activeSource.get(),
                midi_sync_.externalClockPresent.get(),
            }
        );
        const int focused = data.selectedIndex;
        out.mode = "device_settings";
        out.target = "setting";
        if (focused >= 0 && focused < static_cast<int>(data.rows.size())) {
            out.property = data.rows[focused].label;
            copyValueLabel(out.valueLabel, data.rows[focused].value);
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "focus_setting";
            out.intent = ControllerIntent::MOVE_FOCUS;
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "open_setting_value";
            out.intent = ControllerIntent::ACTIVATE;
        }
        return true;
    }

    if (deviceSettingsPhase == core::state::DeviceSettingsFlowPhase::VALUE_SELECTOR) {
        const uint8_t row = device_settings_.selector.editingRow.get();
        const char* const* items = nullptr;
        int itemCount = 0;
        selectorItemsForRow(row, items, itemCount);
        out.mode = "device_settings.selector";
        out.target = "setting_value";
        out.property = core::state::settings::deviceSettingsRowLabel(row);
        if (items && itemCount > 0) {
            const int selected = device_settings_.selector.selectedIndex.get();
            if (selected >= 0 && selected < itemCount) {
                copyValueLabel(out.valueLabel, items[selected]);
            }
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_setting_value";
            out.intent = ControllerIntent::MOVE_FOCUS;
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "apply_setting_value";
            out.intent = ControllerIntent::APPLY;
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "cancel_setting_value";
            out.intent = ControllerIntent::CANCEL;
        }
        return true;
    }

    return false;
}

FLASHMEM TransportUxSurface::TransportUxSurface(
    core::state::StatusBarState& statusBar
)
    : status_bar_(statusBar) {}

FLASHMEM bool TransportUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (!isButton(event, Config::ButtonID::BOTTOM_CENTER, oc::core::input::ButtonBindingType::RELEASE)) {
        return false;
    }

    out.mode = "transport";
    out.target = "transport";
    out.effect = status_bar_.playing.get() ? "transport_stop" : "transport_start";
    out.intent = ControllerIntent::TRANSPORT;
    return true;
}

}  // namespace core::context::standalone::ux

#endif

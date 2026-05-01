#include "context/standalone/ux/StandaloneUxSurfaces.hpp"

#if defined(MS_UX_RECORDER)

#include <cstdio>

#include "config/InputIDs.hpp"
#include "context/standalone/DataManagerPresenterFormatters.hpp"
#include "state/DataManagerState.hpp"

namespace core::context::standalone::ux {
namespace {

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

const char* dialogTarget(core::state::DataManagerFlowPhase phase) {
    switch (phase) {
        case core::state::DataManagerFlowPhase::ASSIGN_SHORTCUT:
            return "shortcut_command";
        case core::state::DataManagerFlowPhase::COMMAND_PALETTE:
            return "command";
        case core::state::DataManagerFlowPhase::SLOT_PICKER:
            return "slot";
        case core::state::DataManagerFlowPhase::SET_LOAD_MODE:
            return "load_mode";
        case core::state::DataManagerFlowPhase::CONFIRM:
            return "confirmation";
        case core::state::DataManagerFlowPhase::CLOSED:
        case core::state::DataManagerFlowPhase::MANAGER:
        default:
            return "item";
    }
}

}  // namespace

DataManagerUxSurface::DataManagerUxSurface(core::state::DataManagerState& dataManager)
    : data_manager_(dataManager) {}

bool DataManagerUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    const bool opening =
        isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::LONG_PRESS);
    if (!opening && !data_manager_.visible.get()) {
        return false;
    }

    if (opening) {
        out.mode = "data_manager";
        out.target = "manager";
        out.effect = "open_data_manager";
        return true;
    }

    const auto phase = data_manager_.flowPhase.get();
    core::context::standalone::data_manager_presenter::Source source{data_manager_};

    if (phase == core::state::DataManagerFlowPhase::MANAGER) {
        const auto data =
            core::context::standalone::data_manager_presenter::buildOverlayRenderData(source);
        const int row = data.selectedIndex;
        out.mode = "data_manager";
        out.target = "shortcut";
        out.targetIndex = static_cast<int16_t>(row);
        if (row >= 0 && row < static_cast<int>(data.rows.size())) {
            out.property = data.rows[row].key;
            copyValueLabel(out.valueLabel, data.rows[row].value);
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "focus_data_manager_shortcut";
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "open_shortcut_assignment";
        } else if (isButton(event, Config::ButtonID::BOTTOM_LEFT, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "run_left_shortcut";
        } else if (isButton(event, Config::ButtonID::BOTTOM_CENTER, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "open_command_palette";
        } else if (isButton(event, Config::ButtonID::BOTTOM_RIGHT, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "run_right_shortcut";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "close_data_manager";
        }
        return true;
    }

    if (core::state::dataManagerFlowShowsDialog(phase)) {
        const auto data =
            core::context::standalone::data_manager_presenter::buildDialogRenderData(source);
        if (!data.visible) return false;

        out.mode = "data_manager.dialog";
        out.target = dialogTarget(phase);
        out.targetIndex = static_cast<int16_t>(data.selectedIndex);
        out.property = data.title;
        if (data.items && data.selectedIndex >= 0 && data.selectedIndex < data.itemCount) {
            copyValueLabel(out.valueLabel, data.items[data.selectedIndex]);
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_data_manager_item";
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "apply_data_manager_item";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "cancel_data_manager_dialog";
        }
        return true;
    }

    return false;
}

}  // namespace core::context::standalone::ux

#endif

#include "context/standalone/ux/StandaloneUxSurfaces.hpp"

#if defined(MS_UX_RECORDER)

#include <config/PlatformCompat.hpp>

#include "config/InputIDs.hpp"
#include "context/standalone/GlobalSettingsOverlayPresenterFormatters.hpp"
#include "context/standalone/ux/StandaloneUxSurfaceUtils.hpp"
#include "state/GlobalSettingsState.hpp"
#include "state/MidiSyncState.hpp"
#include "state/StatusBarState.hpp"
#include "state/ViewSelectorState.hpp"
#include "validation/ux/SemanticUxNames.hpp"

namespace core::context::standalone::ux {
namespace {

using detail::copyValueLabel;
using detail::isButton;
using detail::isEncoder;

}  // namespace

FLASHMEM ViewSelectorUxSurface::ViewSelectorUxSurface(
    oc::state::Signal<core::ui::ViewType, 8>& activeView,
    core::state::ViewSelectorState& viewSelector
) : active_view_(activeView), view_selector_(viewSelector) {}

FLASHMEM bool ViewSelectorUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    const bool opening = isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::PRESS);
    const bool visible = view_selector_.visible.get();
    if (!opening && !visible) {
        return false;
    }

    int selected = visible ? view_selector_.selectedIndex.get() : static_cast<int>(active_view_.get());
    if (selected < 0 || selected >= static_cast<int>(core::ui::ViewType::COUNT)) {
        selected = static_cast<int>(active_view_.get());
    }

    const auto selectedView = static_cast<core::ui::ViewType>(selected);
    out.mode = "view_selector";
    out.target = "view";
    out.targetIndex = static_cast<int16_t>(selected);
    out.property = core::validation::ux::viewName(selectedView);
    copyValueLabel(out.valueLabel, out.property);

    if (opening) {
        out.effect = "open_view_selector";
    } else if (isEncoder(event, Config::EncoderID::NAV)) {
        out.effect = "select_view";
    } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE) ||
               isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
        out.effect = "apply_view";
    }
    return true;
}

FLASHMEM GlobalSettingsUxSurface::GlobalSettingsUxSurface(
    core::state::GlobalSettingsState& globalSettings,
    core::state::MidiSyncState& midiSync
) : global_settings_(globalSettings), midi_sync_(midiSync) {}

FLASHMEM bool GlobalSettingsUxSurface::captureSemanticUxContext(
    const oc::core::input::InputBindingTraceEvent& event,
    core::validation::ux::SemanticUxContext& out
) const {
    if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::LONG_PRESS)) {
        out.mode = "global_settings";
        out.target = "settings";
        out.effect = "open_global_settings";
        return true;
    }

    const auto globalSettingsPhase = global_settings_.flowPhase.get();
    if (globalSettingsPhase == core::state::GlobalSettingsFlowPhase::OVERLAY) {
        auto data = core::context::standalone::global_settings_presenter::buildOverlayRenderData({
            global_settings_,
            midi_sync_,
        });
        const int focused = data.selectedIndex;
        out.mode = "global_settings";
        out.target = "setting";
        if (focused >= 0 && focused < static_cast<int>(data.rows.size())) {
            out.property = data.rows[focused].key;
            copyValueLabel(out.valueLabel, data.rows[focused].value);
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "focus_setting";
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "open_setting_value";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "close_global_settings";
        }
        return true;
    }

    if (globalSettingsPhase == core::state::GlobalSettingsFlowPhase::VALUE_SELECTOR) {
        auto data = core::context::standalone::global_settings_presenter::buildSelectorRenderData({
            global_settings_,
            midi_sync_,
        });
        out.mode = "global_settings.selector";
        out.target = "setting_value";
        out.property = data.title;
        if (data.items && data.itemCount > 0) {
            const int selected = data.selectedIndex;
            if (selected >= 0 && selected < data.itemCount) {
                copyValueLabel(out.valueLabel, data.items[selected]);
            }
        }
        if (isEncoder(event, Config::EncoderID::NAV)) {
            out.effect = "select_setting_value";
        } else if (isButton(event, Config::ButtonID::NAV, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "apply_setting_value";
        } else if (isButton(event, Config::ButtonID::LEFT_TOP, oc::core::input::ButtonBindingType::RELEASE)) {
            out.effect = "cancel_setting_value";
        }
        return true;
    }

    return false;
}

FLASHMEM TransportUxSurface::TransportUxSurface(core::state::StatusBarState& statusBar)
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
    return true;
}

}  // namespace core::context::standalone::ux

#endif

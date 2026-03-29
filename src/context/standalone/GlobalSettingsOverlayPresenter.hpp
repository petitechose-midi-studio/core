#pragma once

#include <oc/state/SignalWatcher.hpp>

#include "context/standalone/GlobalSettingsOverlayPresenterFormatters.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}  // namespace ms::ui

namespace core::context::standalone {

class GlobalSettingsOverlayPresenter {
public:
    using StateRefs = global_settings_presenter::Source;

    GlobalSettingsOverlayPresenter(StateRefs stateRefs,
                                   ms::ui::VirtualListKeyValueOverlay& overlay,
                                   ms::ui::VirtualListSelectorOverlay& selectorOverlay);

    void bind();
    void renderOverlay();
    void renderSelector();

private:
    StateRefs state_refs_;
    ms::ui::VirtualListKeyValueOverlay& overlay_;
    ms::ui::VirtualListSelectorOverlay& selector_overlay_;
    oc::state::SignalWatcher overlay_watcher_;
    oc::state::SignalWatcher selector_watcher_;
};

}  // namespace core::context::standalone

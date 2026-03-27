#pragma once

#include <oc/state/SignalWatcher.hpp>

#include "state/CoreState.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}  // namespace ms::ui

namespace core::context::standalone {

class GlobalSettingsOverlayPresenter {
public:
    GlobalSettingsOverlayPresenter(core::state::CoreState& state,
                                   ms::ui::VirtualListKeyValueOverlay& overlay,
                                   ms::ui::VirtualListSelectorOverlay& selectorOverlay);

    void bind();
    void renderOverlay();
    void renderSelector();

private:
    core::state::CoreState& state_;
    ms::ui::VirtualListKeyValueOverlay& overlay_;
    ms::ui::VirtualListSelectorOverlay& selector_overlay_;
    oc::state::SignalWatcher overlay_watcher_;
    oc::state::SignalWatcher selector_watcher_;
};

}  // namespace core::context::standalone

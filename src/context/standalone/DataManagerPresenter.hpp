#pragma once

#include <oc/state/SignalWatcher.hpp>

#include "context/standalone/DataManagerPresenterFormatters.hpp"

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}

namespace core::ui {
class ContextSoftkeyBar;
class TransportBar;
}

namespace core::context::standalone {

/**
 * Projects Data Manager state into overlays and the context softkey bar.
 *
 * Rendering can temporarily swap the transport bar for command shortcuts, but
 * command execution and shortcut persistence remain in Data Manager services.
 */
class DataManagerPresenter {
public:
    using StateRefs = data_manager_presenter::Source;

    DataManagerPresenter(StateRefs stateRefs,
                         ms::ui::VirtualListKeyValueOverlay& overlay,
                         ms::ui::VirtualListSelectorOverlay& dialogOverlay,
                         core::ui::ContextSoftkeyBar& softkeyBar,
                         core::ui::TransportBar& transportBar);

    void bind();
    void renderOverlay();
    void renderDialog();
    void renderSoftkeyBar();

private:
    StateRefs state_refs_;
    ms::ui::VirtualListKeyValueOverlay& overlay_;
    ms::ui::VirtualListSelectorOverlay& dialog_overlay_;
    core::ui::ContextSoftkeyBar& softkey_bar_;
    core::ui::TransportBar& transport_bar_;
    oc::state::SignalWatcher overlay_watcher_;
    oc::state::SignalWatcher dialog_watcher_;
    oc::state::SignalWatcher softkey_bar_watcher_;
};

}  // namespace core::context::standalone

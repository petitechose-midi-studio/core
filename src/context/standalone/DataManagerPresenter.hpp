#pragma once

#include <oc/state/SignalWatcher.hpp>

namespace core::state {
struct CoreState;
}

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}

namespace core::ui {
class ContextSoftkeyBar;
class TransportBar;
}

namespace core::context::standalone {

class DataManagerPresenter {
public:
    DataManagerPresenter(core::state::CoreState& state,
                         ms::ui::VirtualListKeyValueOverlay& overlay,
                         ms::ui::VirtualListSelectorOverlay& dialogOverlay,
                         core::ui::ContextSoftkeyBar& softkeyBar,
                         core::ui::TransportBar& transportBar);

    void bind();
    void renderOverlay();
    void renderDialog();
    void renderSoftkeyBar();

private:
    core::state::CoreState& state_;
    ms::ui::VirtualListKeyValueOverlay& overlay_;
    ms::ui::VirtualListSelectorOverlay& dialog_overlay_;
    core::ui::ContextSoftkeyBar& softkey_bar_;
    core::ui::TransportBar& transport_bar_;
    oc::state::SignalWatcher overlay_watcher_;
    oc::state::SignalWatcher dialog_watcher_;
    oc::state::SignalWatcher softkey_bar_watcher_;
};

}  // namespace core::context::standalone

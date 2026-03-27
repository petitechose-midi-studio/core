#pragma once

#include <array>

#include <oc/state/SignalWatcher.hpp>

#include "state/CoreState.hpp"

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
    std::array<std::array<char, 8>, 32> dialog_slot_labels_{};
    std::array<const char*, 32> dialog_slot_items_{};
    std::array<const char*, core::state::DATA_MANAGER_MAX_COMMANDS_PER_CONTEXT>
        dialog_command_items_{};
};

}  // namespace core::context::standalone

#pragma once

#include <oc/state/StaticSignalWatcher.hpp>

#include "context/standalone/DataManagerPresenterFormatters.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"

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
    ~DataManagerPresenter();

    [[nodiscard]] bool bind();

private:
    static constexpr uint32_t RENDER_OVERLAY = 1U << 0;
    static constexpr uint32_t RENDER_DIALOG = 1U << 1;
    static constexpr uint32_t RENDER_SOFTKEY_BAR = 1U << 2;

    static void drainRenderQueue(void* context, uint32_t flags);
    void requestOverlayRender();
    void requestDialogRender();
    void requestSoftkeyBarRender();
    void renderPending(uint32_t flags);
    void renderOverlay();
    void renderDialog();
    void renderSoftkeyBar();

    StateRefs state_refs_;
    ms::ui::VirtualListKeyValueOverlay& overlay_;
    ms::ui::VirtualListSelectorOverlay& dialog_overlay_;
    core::ui::ContextSoftkeyBar& softkey_bar_;
    core::ui::TransportBar& transport_bar_;
    core::ui::CoalescedLvglRenderScheduler render_scheduler_;
    oc::state::StaticWatchGroup<8> overlay_watcher_;
    oc::state::StaticWatchGroup<7> dialog_watcher_;
    oc::state::StaticWatchGroup<6> softkey_bar_watcher_;
};

}  // namespace core::context::standalone

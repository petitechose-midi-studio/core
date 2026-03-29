#pragma once

#include <oc/state/SignalWatcher.hpp>

#include "context/standalone/MacroOverlayPresenterFormatters.hpp"

namespace core::state {
struct CoreState;
}

namespace ms::ui {
class VirtualListKeyValueOverlay;
class VirtualListSelectorOverlay;
}  // namespace ms::ui

namespace core::context::standalone {

class MacroOverlayPresenter {
public:
    MacroOverlayPresenter(core::state::CoreState& state,
                          ms::ui::VirtualListKeyValueOverlay& macroEditOverlay,
                          ms::ui::VirtualListSelectorOverlay& macroEditSelectorOverlay,
                          ms::ui::VirtualListSelectorOverlay& pageSelectorOverlay,
                          ms::ui::VirtualListSelectorOverlay& macroTargetSelectorOverlay);

    void bind();
    void renderEdit();
    void renderEditSelector();
    void renderPageSelector();
    void renderTargetSelector();

private:
    void initializeStaticItems_();

    core::state::CoreState& state_;
    ms::ui::VirtualListKeyValueOverlay& macro_edit_overlay_;
    ms::ui::VirtualListSelectorOverlay& macro_edit_selector_overlay_;
    ms::ui::VirtualListSelectorOverlay& page_selector_overlay_;
    ms::ui::VirtualListSelectorOverlay& macro_target_selector_overlay_;
    oc::state::SignalWatcher edit_watcher_;
    oc::state::SignalWatcher edit_selector_watcher_;
    oc::state::SignalWatcher page_selector_watcher_;
    oc::state::SignalWatcher macro_target_selector_watcher_;
    bool static_items_initialized_ = false;
    macro_overlay_presenter::StaticItems static_items_{};
};

}  // namespace core::context::standalone

#pragma once

#include <oc/state/SignalWatcher.hpp>

#include "state/CoreState.hpp"

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
    std::array<std::array<char, 4>, 16> channel_labels_{};
    std::array<const char*, 16> channel_items_{};
    std::array<std::array<char, 4>, 128> cc_labels_{};
    std::array<const char*, 128> cc_items_{};
    std::array<std::array<char, 16>, core::state::MACRO_COUNT> macro_labels_{};
    std::array<const char*, core::state::MACRO_COUNT> macro_items_{};
};

}  // namespace core::context::standalone

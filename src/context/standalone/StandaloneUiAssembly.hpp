#pragma once

#include <memory>

#include <lvgl.h>

#include <oc/type/Ids.hpp>

namespace core::state {
struct CoreState;
}

namespace ms::ui {
class ViewContainer;
}  // namespace ms::ui

namespace core::ui {
class ContextSoftkeyBar;
class MacroView;
class SequencerView;
class TransportBar;
}  // namespace core::ui

namespace core::context::standalone {

class StandaloneUiAssembly {
public:
    explicit StandaloneUiAssembly(core::state::CoreState& state);
    ~StandaloneUiAssembly();

    StandaloneUiAssembly(const StandaloneUiAssembly&) = delete;
    StandaloneUiAssembly& operator=(const StandaloneUiAssembly&) = delete;

    void show();
    lv_obj_t* mainZone() const;
    oc::type::ScopeID macroViewScope() const;
    oc::type::ScopeID sequencerViewScope() const;
    lv_obj_t* macroViewElement() const;
    lv_obj_t* sequencerViewElement() const;
    core::ui::TransportBar& transportBar() const;
    core::ui::ContextSoftkeyBar& contextSoftkeyBar() const;
    void activateMacroView() const;
    void deactivateMacroView() const;
    void activateSequencerView() const;
    void deactivateSequencerView() const;

private:
    void createViewContainer();
    void createViews();
    void createBottomBar();
    void cacheViewScopes();

    core::state::CoreState& core_state_;
    oc::type::ScopeID macro_view_scope_ = 0;
    oc::type::ScopeID sequencer_view_scope_ = 0;
    std::unique_ptr<ms::ui::ViewContainer> view_container_;
    std::unique_ptr<core::ui::MacroView> macro_view_;
    std::unique_ptr<core::ui::SequencerView> sequencer_view_;
    std::unique_ptr<core::ui::TransportBar> transport_bar_;
    std::unique_ptr<core::ui::ContextSoftkeyBar> context_softkey_bar_;
};

}  // namespace core::context::standalone

#include "context/standalone/StandaloneUiAssembly.hpp"

#include <memory>

#include <config/PlatformCompat.hpp>
#include <oc/ui/lvgl/Screen.hpp>
#include <oc/ui/lvgl/Scope.hpp>

#include "state/CoreState.hpp"
#include "ui/transportbar/ContextSoftkeyBar.hpp"
#include "ui/transportbar/TransportBar.hpp"
#include "ui/view/MacroView.hpp"
#include "ui/view/SequencerView.hpp"
#include <ms/ui/ViewContainer.hpp>

namespace core::context::standalone {

FLASHMEM StandaloneUiAssembly::StandaloneUiAssembly(core::state::CoreState& state)
    : core_state_(state) {
    createViewContainer();
    createViews();
    createBottomBar();
}

FLASHMEM StandaloneUiAssembly::~StandaloneUiAssembly() {
    if (macro_view_) {
        macro_view_->onDeactivate();
    }
    if (sequencer_view_) {
        sequencer_view_->onDeactivate();
    }
}

FLASHMEM void StandaloneUiAssembly::show() {
    view_container_->show();
}

FLASHMEM lv_obj_t* StandaloneUiAssembly::mainZone() const {
    return view_container_->getMainZone();
}

FLASHMEM oc::type::ScopeID StandaloneUiAssembly::macroViewScope() const {
    return macro_view_scope_;
}

FLASHMEM oc::type::ScopeID StandaloneUiAssembly::sequencerViewScope() const {
    return sequencer_view_scope_;
}

FLASHMEM lv_obj_t* StandaloneUiAssembly::macroViewElement() const {
    return macro_view_->getElement();
}

FLASHMEM lv_obj_t* StandaloneUiAssembly::sequencerViewElement() const {
    return sequencer_view_->getElement();
}

FLASHMEM core::ui::TransportBar& StandaloneUiAssembly::transportBar() const {
    return *transport_bar_;
}

FLASHMEM core::ui::ContextSoftkeyBar& StandaloneUiAssembly::contextSoftkeyBar() const {
    return *context_softkey_bar_;
}

FLASHMEM void StandaloneUiAssembly::activateMacroView() const {
    macro_view_->onActivate();
}

FLASHMEM void StandaloneUiAssembly::deactivateMacroView() const {
    macro_view_->onDeactivate();
}

FLASHMEM void StandaloneUiAssembly::activateSequencerView() const {
    sequencer_view_->onActivate();
}

FLASHMEM void StandaloneUiAssembly::deactivateSequencerView() const {
    sequencer_view_->onDeactivate();
}

FLASHMEM void StandaloneUiAssembly::createViewContainer() {
    view_container_ = core::app::makeExtmemUnique<ms::ui::ViewContainer>(
        oc::ui::lvgl::Screen::root()
    );
}

FLASHMEM void StandaloneUiAssembly::createViews() {
    lv_obj_t* mainZone = view_container_->getMainZone();

    macro_view_ = core::app::makeExtmemUnique<core::ui::MacroView>(
        mainZone,
        core::ui::MacroView::StateRefs{
            core_state_.macros,
            core_state_.pages,
            core_state_.macroUi,
            core_state_.structureNavigationFocus,
            core_state_.structureClipboard,
            core_state_.configRevision,
            core_state_.statusBar,
        }
    );
    sequencer_view_ = core::app::makeExtmemUnique<core::ui::SequencerView>(
        mainZone,
        core::ui::SequencerView::StateRefs{
            core_state_.sequencer,
            core_state_.sequencerTracks,
            core_state_.structureNavigationFocus,
            core_state_.structureClipboard,
            core_state_.statusBar,
            core_state_.viewSelector,
            core_state_.globalSettings,
            core_state_.dataManager,
        }
    );
    cacheViewScopes();
}

FLASHMEM void StandaloneUiAssembly::createBottomBar() {
    lv_obj_t* bottomZone = view_container_->getBottomZone();
    transport_bar_ = core::app::makeExtmemUnique<core::ui::TransportBar>(
        bottomZone,
        core_state_.statusBar
    );
    context_softkey_bar_ = core::app::makeExtmemUnique<core::ui::ContextSoftkeyBar>(bottomZone);
}

FLASHMEM void StandaloneUiAssembly::cacheViewScopes() {
    macro_view_scope_ = macro_view_ ? oc::ui::lvgl::scopeID(macro_view_->getElement()) : 0;
    sequencer_view_scope_ =
        sequencer_view_ ? oc::ui::lvgl::scopeID(sequencer_view_->getElement()) : 0;
}

}  // namespace core::context::standalone

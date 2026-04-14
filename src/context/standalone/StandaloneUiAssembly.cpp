#include "context/standalone/StandaloneUiAssembly.hpp"

#include <memory>

#include <config/PlatformCompat.hpp>
#include <oc/ui/lvgl/Screen.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>

#include "state/CoreState.hpp"
#include "ui/common/GlobalTrackNavigationStripModel.hpp"
#include "ui/common/TrackNavigationStrip.hpp"
#include "ui/transportbar/ContextSoftkeyBar.hpp"
#include "ui/transportbar/TransportBar.hpp"
#include "ui/view/MacroView.hpp"
#include "ui/view/PausableLvglTimer.hpp"
#include "ui/view/SequencerView.hpp"
#include <ms/ui/ViewContainer.hpp>

namespace core::context::standalone {

namespace style = oc::ui::lvgl::style;

FLASHMEM StandaloneUiAssembly::StandaloneUiAssembly(core::state::CoreState& state)
    : core_state_(state) {
    createViewContainer();
    createGlobalTrackStrip();
    createViews();
    createBottomBar();
    bindGlobalTrackStrip();
    scheduleGlobalTrackStripRender(true);
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

    lv_obj_t* mainZone = view_container_->getMainZone();
    views_host_ = lv_obj_create(mainZone);
    style::apply(views_host_).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_width(views_host_, LV_PCT(100));
    lv_obj_set_flex_grow(views_host_, 1);
}

FLASHMEM void StandaloneUiAssembly::createGlobalTrackStrip() {
    lv_obj_t* mainZone = view_container_->getMainZone();
    global_track_strip_container_ = lv_obj_create(mainZone);
    style::apply(global_track_strip_container_)
        .size(LV_PCT(100), LV_SIZE_CONTENT)
        .transparent()
        .noBorder()
        .pad(0)
        .noScroll();
    lv_obj_move_to_index(global_track_strip_container_, 0);
    global_track_strip_ = std::make_unique<core::ui::TrackNavigationStrip>(
        global_track_strip_container_
    );

    constexpr uint32_t targetHz = Config::Timing::LVGL_HZ;
    constexpr uint32_t periodMs = (targetHz > 1000) ? 1 : ((1000 + targetHz - 1) / targetHz);
    global_track_strip_timer_ = std::make_unique<core::ui::PausableLvglTimer>(
        periodMs,
        onGlobalTrackStripTimer,
        this
    );
    global_track_strip_timer_->resume(true);
}

FLASHMEM void StandaloneUiAssembly::createViews() {
    lv_obj_t* viewsHost = views_host_ ? views_host_ : view_container_->getMainZone();
    macro_view_ = core::app::makeExtmemUnique<core::ui::MacroView>(
        viewsHost,
        core::ui::MacroView::StateRefs{
            core_state_.macros,
            core_state_.pages,
            core_state_.macroUi,
            core_state_.structureNavigationFocus,
            core_state_.sharedTrackActive,
            core_state_.sharedTrackEnabledMask,
            core_state_.structureClipboard,
            core_state_.configRevision,
            core_state_.statusBar,
            core_state_.macroEdit,
            core_state_.viewSelector,
            core_state_.globalSettings,
            core_state_.dataManager,
        }
    );
    sequencer_view_ = core::app::makeExtmemUnique<core::ui::SequencerView>(
        viewsHost,
        core::ui::SequencerView::StateRefs{
            core_state_.sequencer,
            core_state_.sequencerTracks,
            core_state_.structureNavigationFocus,
            core_state_.sharedTrackActive,
            core_state_.sharedTrackEnabledMask,
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

FLASHMEM void StandaloneUiAssembly::bindGlobalTrackStrip() {
    global_track_strip_watcher_.watchAll(
        [this]() { scheduleGlobalTrackStripRender(true); },
        core_state_.activeView,
        core_state_.structureNavigationFocus
    );

    global_track_strip_watcher_.watchAll(
        [this]() { scheduleGlobalTrackStripRender(); },
        core_state_.sharedTrackActive,
        core_state_.sharedTrackEnabledMask,
        core_state_.macroUi.previewAddSlot,
        core_state_.macroUi.previewTrackIndex,
        core_state_.macroUi.previewPageIndex,
        core_state_.macroUi.structureSelection.active,
        core_state_.macroUi.structureSelection.scope,
        core_state_.macroUi.structureSelection.cursorIndex,
        core_state_.macroUi.structureSelection.selectedMask
    );

    global_track_strip_watcher_.watchAll(
        [this]() { scheduleGlobalTrackStripRender(); },
        core_state_.sequencer.structureUi.previewAddSlot,
        core_state_.sequencer.structureUi.previewTrackIndex,
        core_state_.sequencer.structureUi.previewPageIndex,
        core_state_.sequencer.structureUi.selection.active,
        core_state_.sequencer.structureUi.selection.scope,
        core_state_.sequencer.structureUi.selection.cursorIndex,
        core_state_.sequencer.structureUi.selection.selectedMask
    );

    global_track_strip_watcher_.watchAll(
        [this]() { scheduleGlobalTrackStripRender(); },
        core_state_.statusBar.trackNoteActivity[0],
        core_state_.statusBar.trackNoteActivity[1],
        core_state_.statusBar.trackNoteActivity[2],
        core_state_.statusBar.trackNoteActivity[3],
        core_state_.statusBar.trackNoteActivity[4],
        core_state_.statusBar.trackNoteActivity[5],
        core_state_.statusBar.trackNoteActivity[6],
        core_state_.statusBar.trackNoteActivity[7]
    );

    global_track_strip_watcher_.watchAll(
        [this]() { scheduleGlobalTrackStripRender(); },
        core_state_.statusBar.trackNoteActivity[8],
        core_state_.statusBar.trackNoteActivity[9],
        core_state_.statusBar.trackNoteActivity[10],
        core_state_.statusBar.trackNoteActivity[11],
        core_state_.statusBar.trackNoteActivity[12],
        core_state_.statusBar.trackNoteActivity[13],
        core_state_.statusBar.trackNoteActivity[14],
        core_state_.statusBar.trackNoteActivity[15]
    );

}

FLASHMEM void StandaloneUiAssembly::scheduleGlobalTrackStripRender(bool ready) {
    global_track_strip_dirty_ = true;
    if (global_track_strip_timer_) {
        global_track_strip_timer_->resume(ready);
    }
}

FLASHMEM void StandaloneUiAssembly::renderGlobalTrackStrip() {
    if (!global_track_strip_) return;

    const bool hasBlockingOverlay = core_state_.overlays.hasVisible();
    if (hasBlockingOverlay) {
        if (global_track_strip_container_ &&
            !lv_obj_has_flag(global_track_strip_container_, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(global_track_strip_container_, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (global_track_strip_container_ &&
        lv_obj_has_flag(global_track_strip_container_, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(global_track_strip_container_, LV_OBJ_FLAG_HIDDEN);
        global_track_strip_dirty_ = true;
    }

    if (!global_track_strip_dirty_) return;

    const auto props = core::ui::buildGlobalTrackNavigationStripProps(core_state_);
    if (!global_track_strip_props_initialized_ ||
        !core::ui::globalTrackNavigationStripPropsEqual(global_track_strip_props_cache_, props)) {
        global_track_strip_->render(props);
        global_track_strip_props_cache_ = props;
        global_track_strip_props_initialized_ = true;
    }

    global_track_strip_dirty_ = false;
}

FLASHMEM void StandaloneUiAssembly::onGlobalTrackStripTimer(lv_timer_t* timer) {
    auto* self = static_cast<StandaloneUiAssembly*>(lv_timer_get_user_data(timer));
    if (!self) return;
    self->renderGlobalTrackStrip();
}

}  // namespace core::context::standalone

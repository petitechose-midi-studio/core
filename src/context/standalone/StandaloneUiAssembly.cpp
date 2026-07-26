#include "context/standalone/StandaloneUiAssembly.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/Screen.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include <oc/ui/lvgl/style/StyleBuilder.hpp>
#include <oc/ui/lvgl/theme/BaseTheme.hpp>

#include "state/CoreState.hpp"
#include "ui/common/GlobalTrackNavigationStripModel.hpp"
#include "ui/common/TrackNavigationStrip.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
#include "ui/transportbar/ContextSoftkeyBar.hpp"
#include "ui/transportbar/TransportBar.hpp"
#include "ui/view/DeviceSettingsView.hpp"
#include "ui/view/MacroView.hpp"
#include "ui/view/ProjectView.hpp"
#include "ui/view/RetainedViewRenderPolicy.hpp"
#include "ui/view/SequencerView.hpp"
#include <ms/ui/ViewContainer.hpp>

namespace core::context::standalone {

namespace style = oc::ui::lvgl::style;
namespace theme = oc::ui::lvgl::base_theme;

FLASHMEM StandaloneUiAssembly::StandaloneUiAssembly(core::state::CoreState& state)
    : core_state_(state) {}

FLASHMEM bool StandaloneUiAssembly::initialize() {
    if (initialized_) return true;

    if (!createViewContainer()) return false;

    if (!createGlobalTrackStrip()) return false;

    if (!createViews()) return false;

    if (overlay_curtain_) lv_obj_move_foreground(overlay_curtain_);

    if (!createBottomBar()) return false;

    if (!bindGlobalTrackStrip()) return false;

    scheduleGlobalTrackStripRender(true);
    initialized_ = true;
    return true;
}

FLASHMEM StandaloneUiAssembly::~StandaloneUiAssembly() {
    global_track_strip_scheduler_.reset();
    if (macro_view_) {
        macro_view_->onDeactivate();
    }
    if (sequencer_view_) {
        sequencer_view_->onDeactivate();
    }
    if (project_view_) {
        project_view_->onDeactivate();
    }
    if (device_settings_view_) {
        device_settings_view_->onDeactivate();
    }

    device_settings_view_.reset();
    project_view_.reset();
    sequencer_view_.reset();
    macro_view_.reset();
    macro_view_parking_host_ = nullptr;
    sequencer_view_parking_host_ = nullptr;
    project_view_parking_host_ = nullptr;
    device_settings_view_parking_host_ = nullptr;
}

FLASHMEM void StandaloneUiAssembly::show() {
    if (!initialized_ || !view_container_) return;
    view_container_->show();
    applyOverlayExclusivity();
}

FLASHMEM lv_obj_t* StandaloneUiAssembly::mainZone() const {
    return view_container_->getMainZone();
}

FLASHMEM lv_obj_t* StandaloneUiAssembly::overlayRoot() const {
    return view_container_->getContainer();
}

FLASHMEM oc::type::ScopeID StandaloneUiAssembly::macroViewScope() const {
    return macro_view_scope_;
}

FLASHMEM oc::type::ScopeID StandaloneUiAssembly::sequencerViewScope() const {
    return sequencer_view_scope_;
}

FLASHMEM oc::type::ScopeID StandaloneUiAssembly::projectViewScope() const {
    return project_view_scope_;
}

FLASHMEM oc::type::ScopeID StandaloneUiAssembly::deviceSettingsViewScope() const {
    return device_settings_view_scope_;
}

FLASHMEM lv_obj_t* StandaloneUiAssembly::macroViewElement() const {
    return macro_view_->getElement();
}

FLASHMEM lv_obj_t* StandaloneUiAssembly::sequencerViewElement() const {
    return sequencer_view_->getElement();
}

FLASHMEM lv_obj_t* StandaloneUiAssembly::projectViewElement() const {
    return project_view_->getElement();
}

FLASHMEM lv_obj_t* StandaloneUiAssembly::deviceSettingsViewElement() const {
    return device_settings_view_->getElement();
}

FLASHMEM core::ui::TransportBar& StandaloneUiAssembly::transportBar() const {
    return *transport_bar_;
}

FLASHMEM core::ui::ContextSoftkeyBar& StandaloneUiAssembly::contextSoftkeyBar() const {
    return *context_softkey_bar_;
}

FLASHMEM void StandaloneUiAssembly::activateMacroView() const {
    core::ui::RetainedViewRenderPolicy::attach(macro_view_->getElement(), views_host_);
    macro_view_->onActivate();
}

FLASHMEM void StandaloneUiAssembly::deactivateMacroView() const {
    macro_view_->onDeactivate();
    oc::ui::lvgl::RetainedSurfaceParkingLot::park(
        macro_view_->getElement(), macro_view_parking_host_
    );
}

FLASHMEM void StandaloneUiAssembly::activateSequencerView() const {
    core::ui::RetainedViewRenderPolicy::attach(
        sequencer_view_->getElement(), views_host_
    );
    sequencer_view_->onActivate();
}

FLASHMEM void StandaloneUiAssembly::deactivateSequencerView() const {
    sequencer_view_->onDeactivate();
    oc::ui::lvgl::RetainedSurfaceParkingLot::park(
        sequencer_view_->getElement(), sequencer_view_parking_host_
    );
}

FLASHMEM void StandaloneUiAssembly::activateProjectView() const {
    core::ui::RetainedViewRenderPolicy::attach(
        project_view_->getElement(), full_view_host_
    );
    project_view_->onActivate();
}

FLASHMEM void StandaloneUiAssembly::deactivateProjectView() const {
    project_view_->onDeactivate();
    oc::ui::lvgl::RetainedSurfaceParkingLot::park(
        project_view_->getElement(), project_view_parking_host_
    );
}

FLASHMEM void StandaloneUiAssembly::activateDeviceSettingsView() const {
    core::ui::RetainedViewRenderPolicy::attach(
        device_settings_view_->getElement(), full_view_host_
    );
    device_settings_view_->onActivate();
}

FLASHMEM void StandaloneUiAssembly::deactivateDeviceSettingsView() const {
    device_settings_view_->onDeactivate();
    oc::ui::lvgl::RetainedSurfaceParkingLot::park(
        device_settings_view_->getElement(), device_settings_view_parking_host_
    );
}

FLASHMEM bool StandaloneUiAssembly::createViewContainer() {
    view_container_ = core::app::makeExtmemUnique<ms::ui::ViewContainer>(
        oc::ui::lvgl::Screen::root()
    );
    if (!view_container_) {
        OC_LOG_ERROR("StandaloneUiAssembly: ViewContainer PSRAM allocation failed");
        return false;
    }

    if (!retained_view_parking_.initialize()) {
        OC_LOG_ERROR("StandaloneUiAssembly: retained parking screen allocation failed");
        return false;
    }
    macro_view_parking_host_ = retained_view_parking_.createHost();
    sequencer_view_parking_host_ = retained_view_parking_.createHost();
    project_view_parking_host_ = retained_view_parking_.createHost();
    device_settings_view_parking_host_ = retained_view_parking_.createHost();
    if (!macro_view_parking_host_ || !sequencer_view_parking_host_ ||
        !project_view_parking_host_ || !device_settings_view_parking_host_) {
        OC_LOG_ERROR("StandaloneUiAssembly: retained parking host allocation failed");
        return false;
    }

    lv_obj_t* mainZone = view_container_->getMainZone();
    if (!mainZone) {
        OC_LOG_ERROR("StandaloneUiAssembly: ViewContainer main zone unavailable");
        return false;
    }
    views_host_ = lv_obj_create(mainZone);
    if (!views_host_) {
        OC_LOG_ERROR("StandaloneUiAssembly: performance view host allocation failed");
        return false;
    }
    style::apply(views_host_).transparent().noBorder().pad(0).noScroll();
    lv_obj_set_width(views_host_, LV_PCT(100));
    lv_obj_set_flex_grow(views_host_, 1);

    // Project and Device Settings use the complete main zone, including the
    // space occupied by the track strip in performance views. Keeping a
    // separate absolute host avoids resizing the whole retained view tree when
    // switching between those two presentation families.
    full_view_host_ = lv_obj_create(mainZone);
    if (!full_view_host_) {
        OC_LOG_ERROR("StandaloneUiAssembly: full view host allocation failed");
        return false;
    }
    style::apply(full_view_host_).fullSize().transparent().noBorder().pad(0).noScroll();
    lv_obj_add_flag(full_view_host_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(full_view_host_, LV_OBJ_FLAG_CLICKABLE);

    // A single opaque object hides the active view below semi-transparent
    // overlays. Toggling this leaf avoids propagating opacity changes through
    // the complete active view tree.
    overlay_curtain_ = lv_obj_create(mainZone);
    if (!overlay_curtain_) {
        OC_LOG_ERROR("StandaloneUiAssembly: overlay curtain allocation failed");
        return false;
    }
    style::apply(overlay_curtain_)
        .fullSize()
        .bgColor(theme::color::BACKGROUND)
        .noBorder()
        .pad(0)
        .noScroll();
    lv_obj_add_flag(
        overlay_curtain_,
        static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT)
    );
    lv_obj_remove_flag(overlay_curtain_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(overlay_curtain_, LV_OPA_TRANSP, 0);
    lv_obj_align(overlay_curtain_, LV_ALIGN_CENTER, 0, 0);
    return true;
}

FLASHMEM bool StandaloneUiAssembly::createGlobalTrackStrip() {
    lv_obj_t* mainZone = view_container_->getMainZone();
    global_track_strip_container_ = lv_obj_create(mainZone);
    if (!global_track_strip_container_) {
        OC_LOG_ERROR("StandaloneUiAssembly: global track strip root allocation failed");
        return false;
    }
    style::apply(global_track_strip_container_)
        .size(LV_PCT(100), LV_SIZE_CONTENT)
        .transparent()
        .noBorder()
        .pad(0)
        .noScroll();
    lv_obj_move_to_index(global_track_strip_container_, 0);
    global_track_strip_ = core::app::makeExtmemUnique<core::ui::TrackNavigationStrip>(
        global_track_strip_container_
    );
    if (!global_track_strip_ || !global_track_strip_->getElement()) {
        OC_LOG_ERROR("StandaloneUiAssembly: global track strip PSRAM allocation failed");
        return false;
    }

    constexpr uint32_t targetHz = Config::Timing::LVGL_HZ;
    constexpr uint32_t periodMs = (targetHz > 1000) ? 1 : ((1000 + targetHz - 1) / targetHz);
    global_track_strip_scheduler_ =
        core::app::makeExtmemUnique<core::ui::CoalescedLvglRenderScheduler>(
            core::ui::renderSchedulerDebugLabel("GlobalTrackStrip"),
            &StandaloneUiAssembly::drainGlobalTrackStripRender,
            this,
            periodMs
        );
    if (!global_track_strip_scheduler_ || !global_track_strip_scheduler_->valid()) {
        OC_LOG_ERROR("StandaloneUiAssembly: global track strip scheduler allocation failed");
        return false;
    }
    return true;
}

FLASHMEM bool StandaloneUiAssembly::createViews() {
    lv_obj_t* viewsHost = views_host_ ? views_host_ : view_container_->getMainZone();
    lv_obj_t* fullViewHost = full_view_host_ ? full_view_host_ : viewsHost;
    macro_view_ = core::app::makeExtmemUnique<core::ui::MacroView>(
        viewsHost,
        core::ui::MacroView::StateRefs{
            core_state_.macros,
            core_state_.pages,
            core_state_.macroUi,
            core_state_.trackNavigation,
            core_state_.structureNavigationFocus,
            core_state_.sharedTrackActive,
            core_state_.sharedTrackEnabledMask,
            core_state_.structureClipboard,
            core_state_.configRevision,
            core_state_.statusBar,
            core_state_.macroEdit,
            core_state_.viewSelector,
            core_state_.deviceSettings,
            core_state_.dataManager,
        }
    );
    if (!macro_view_ || !macro_view_->valid()) {
        OC_LOG_ERROR("StandaloneUiAssembly: MacroView initialization failed");
        return false;
    }

    sequencer_view_ = core::app::makeExtmemUnique<core::ui::SequencerView>(
        viewsHost,
        core::ui::SequencerView::StateRefs{
            core_state_.sequencer,
            core_state_.sequencerTracks,
            core_state_.projectTracks,
            core_state_.trackNavigation,
            core_state_.structureNavigationFocus,
            core_state_.sharedTrackActive,
            core_state_.sharedTrackEnabledMask,
            core_state_.structureClipboard,
            core_state_.statusBar,
            core_state_.viewSelector,
            core_state_.deviceSettings,
            core_state_.sequencerSettings,
            core_state_.dataManager,
            core_state_.projectNavigation,
            core_state_.sequencerTrackActivations,
        }
    );
    if (!sequencer_view_ || !sequencer_view_->valid()) {
        OC_LOG_ERROR("StandaloneUiAssembly: SequencerView initialization failed");
        return false;
    }

    project_view_ = core::app::makeExtmemUnique<core::ui::ProjectView>(
        fullViewHost,
        core::ui::ProjectView::StateRefs{
            core_state_.projectNavigation,
            core_state_.project,
            core_state_.pages,
            core_state_.macroUi,
            core_state_.projectTracks,
            core_state_.sequencerTracks,
            core_state_.statusBar,
            core_state_.midiSync,
        }
    );
    if (!project_view_ || !project_view_->valid()) {
        OC_LOG_ERROR("StandaloneUiAssembly: ProjectView initialization failed");
        return false;
    }

    device_settings_view_ = core::app::makeExtmemUnique<core::ui::DeviceSettingsView>(
        fullViewHost,
        core::ui::DeviceSettingsView::StateRefs{
            core_state_.deviceSettings,
            core_state_.midiSync,
        }
    );
    if (!device_settings_view_ || !device_settings_view_->valid()) {
        OC_LOG_ERROR("StandaloneUiAssembly: DeviceSettingsView initialization failed");
        return false;
    }
    cacheViewScopes();
    if (!macro_view_scope_ || !sequencer_view_scope_ || !project_view_scope_ ||
        !device_settings_view_scope_) {
        OC_LOG_ERROR("StandaloneUiAssembly: view root allocation failed");
        return false;
    }
    return true;
}

FLASHMEM bool StandaloneUiAssembly::createBottomBar() {
    lv_obj_t* bottomZone = view_container_->getBottomZone();
    if (!bottomZone) {
        OC_LOG_ERROR("StandaloneUiAssembly: bottom zone unavailable");
        return false;
    }
    transport_bar_ = core::app::makeExtmemUnique<core::ui::TransportBar>(
        bottomZone,
        core_state_.statusBar
    );
    context_softkey_bar_ = core::app::makeExtmemUnique<core::ui::ContextSoftkeyBar>(bottomZone);
    if (!transport_bar_ || !context_softkey_bar_) {
        OC_LOG_ERROR("StandaloneUiAssembly: bottom bar PSRAM allocation failed");
        return false;
    }
    return true;
}

FLASHMEM void StandaloneUiAssembly::cacheViewScopes() {
    macro_view_scope_ = macro_view_ ? oc::ui::lvgl::scopeID(macro_view_->getElement()) : 0;
    sequencer_view_scope_ =
        sequencer_view_ ? oc::ui::lvgl::scopeID(sequencer_view_->getElement()) : 0;
    project_view_scope_ = project_view_ ? oc::ui::lvgl::scopeID(project_view_->getElement()) : 0;
    device_settings_view_scope_ =
        device_settings_view_ ? oc::ui::lvgl::scopeID(device_settings_view_->getElement()) : 0;
}

FLASHMEM bool StandaloneUiAssembly::bindGlobalTrackStrip() {
    bool bound = true;
    global_track_context_watcher_.bind<
        &StandaloneUiAssembly::requestGlobalTrackStripRenderReady
    >(*this, 0, "GlobalTrackStrip.context");
    bound = global_track_context_watcher_.watchAll(
        core_state_.activeView,
        core_state_.structureNavigationFocus
    ) && bound;

    global_track_structure_watcher_.bind<
        &StandaloneUiAssembly::requestGlobalTrackStripRender
    >(*this, 1, "GlobalTrackStrip.structure");
    bound = global_track_structure_watcher_.watchAll(
        core_state_.sharedTrackActive,
        core_state_.sharedTrackEnabledMask,
        core_state_.projectTracks.revision,
        core_state_.trackNavigation.previewAddSlot,
        core_state_.trackNavigation.previewTrackIndex
    ) && bound;

    global_track_activity_low_watcher_.bind<
        &StandaloneUiAssembly::requestGlobalTrackStripRender
    >(*this, 2, "GlobalTrackStrip.activity0_7");
    bound = global_track_activity_low_watcher_.watchAll(
        core_state_.statusBar.trackNoteActivity[0],
        core_state_.statusBar.trackNoteActivity[1],
        core_state_.statusBar.trackNoteActivity[2],
        core_state_.statusBar.trackNoteActivity[3],
        core_state_.statusBar.trackNoteActivity[4],
        core_state_.statusBar.trackNoteActivity[5],
        core_state_.statusBar.trackNoteActivity[6],
        core_state_.statusBar.trackNoteActivity[7]
    ) && bound;

    global_track_activity_high_watcher_.bind<
        &StandaloneUiAssembly::requestGlobalTrackStripRender
    >(*this, 3, "GlobalTrackStrip.activity8_15");
    bound = global_track_activity_high_watcher_.watchAll(
        core_state_.statusBar.trackNoteActivity[8],
        core_state_.statusBar.trackNoteActivity[9],
        core_state_.statusBar.trackNoteActivity[10],
        core_state_.statusBar.trackNoteActivity[11],
        core_state_.statusBar.trackNoteActivity[12],
        core_state_.statusBar.trackNoteActivity[13],
        core_state_.statusBar.trackNoteActivity[14],
        core_state_.statusBar.trackNoteActivity[15]
    ) && bound;

    overlay_visibility_watcher_.bind<
        &StandaloneUiAssembly::requestGlobalTrackStripRenderReady
    >(*this, 4, "GlobalTrackStrip.overlays");
    bound = overlay_visibility_watcher_.watchAll(core_state_.overlays.revisionSignal()) && bound;
    if (!bound) {
        OC_LOG_ERROR("StandaloneUiAssembly: global track strip binding failed");
    }
    return bound;
}

FLASHMEM void StandaloneUiAssembly::applyOverlayExclusivity() {
    const bool hasOverlay = core_state_.overlays.hasVisible();
    if (overlay_exclusive_mode_ == hasOverlay) return;

    OC_PERF_SCOPE(perfExclusivity, "ui.overlay-exclusivity");
    OC_PERF_UNITS(perfExclusivity, hasOverlay ? 1U : 0U, 0U);
    overlay_exclusive_mode_ = hasOverlay;
    lv_obj_t* bottomZone = view_container_ ? view_container_->getBottomZone() : nullptr;

    if (hasOverlay) {
        if (overlay_curtain_) {
            lv_obj_set_style_bg_opa(overlay_curtain_, LV_OPA_COVER, 0);
        }
        if (bottomZone) {
            lv_obj_clear_flag(bottomZone, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(bottomZone);
        }
        return;
    }

    if (overlay_curtain_) {
        lv_obj_set_style_bg_opa(overlay_curtain_, LV_OPA_TRANSP, 0);
    }
    if (bottomZone) lv_obj_clear_flag(bottomZone, LV_OBJ_FLAG_HIDDEN);
}

void StandaloneUiAssembly::scheduleGlobalTrackStripRender(bool ready) {
    if (global_track_strip_scheduler_) {
        global_track_strip_scheduler_->request(1U, ready);
    }
}

void StandaloneUiAssembly::requestGlobalTrackStripRender() {
    scheduleGlobalTrackStripRender();
}

void StandaloneUiAssembly::requestGlobalTrackStripRenderReady() {
    scheduleGlobalTrackStripRender(true);
}

void StandaloneUiAssembly::renderGlobalTrackStrip() {
    if (!global_track_strip_) return;

    applyOverlayExclusivity();
    if (overlay_exclusive_mode_) {
        return;
    }

    if (core::ui::isProjectWorkspaceView(core_state_.activeView.get()) ||
        core_state_.activeView.get() == core::ui::ViewType::DEVICE_SETTINGS) {
        return;
    }

    const auto props = core::ui::buildGlobalTrackNavigationStripProps(
        core::ui::GlobalTrackNavigationStripSource{
            core_state_.trackNavigation,
            core_state_.structureNavigationFocus.get(),
            core_state_.sharedTrackEnabledMask.get(),
            core_state_.projectTracks,
            core_state_.sharedTrackActive.get(),
            core_state_.statusBar,
        }
    );
    if (!global_track_strip_props_initialized_ ||
        !core::ui::globalTrackNavigationStripPropsEqual(global_track_strip_props_cache_, props)) {
        global_track_strip_->render(props);
        global_track_strip_props_cache_ = props;
        global_track_strip_props_initialized_ = true;
    }

}

void StandaloneUiAssembly::drainGlobalTrackStripRender(
    void* context,
    uint32_t flags
) {
    if ((flags & 1U) == 0) return;
    auto* self = static_cast<StandaloneUiAssembly*>(context);
    if (self) self->renderGlobalTrackStrip();
}

}  // namespace core::context::standalone

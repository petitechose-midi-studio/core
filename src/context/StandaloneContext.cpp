#include "StandaloneContext.hpp"

#include <oc/log/Log.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/interface/IEncoder.hpp>
#include <oc/ui/lvgl/FontLoader.hpp>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include "context/standalone/ActiveViewLifecyclePlan.hpp"
#include "context/standalone/MacroViewActivationContract.hpp"
#include "context/standalone/StandaloneFeatureAssembly.hpp"
#include "context/standalone/StandaloneGlobalHandlerAssembly.hpp"
#include "context/standalone/StandaloneOverlayAssembly.hpp"
#include "context/standalone/StandaloneUiAssembly.hpp"
#include "diagnostics/MemoryFootprintReporter.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "persistence/ProductFileService.hpp"
#include "protocol/filesystem/FileSystemRpc.hpp"
#include "config/TimeCompat.hpp"
#include "state/CoreState.hpp"
#include "ui/common/CoalescedLvglRenderScheduler.hpp"
#include <ms/ui/font/CoreFonts.hpp>
#include "ui/font/StandaloneFonts.hpp"
#include "ui/transportbar/ContextSoftkeyBar.hpp"
#include "ui/transportbar/TransportBar.hpp"

namespace core::context {

namespace input_utils = core::handler::sequencer::input_utils;

// Constructor and destructor must be in .cpp where handler types are complete
FLASHMEM StandaloneContext::StandaloneContext(
    core::state::CoreState& state,
    core::persistence::ProductFileService& productFiles
) : core_state_(state), product_files_(productFiles) {}

FLASHMEM StandaloneContext::~StandaloneContext() = default;

// =============================================================================
// IContext Lifecycle
// =============================================================================

FLASHMEM oc::type::Result<void> StandaloneContext::init() {
    OC_LOG_INFO("StandaloneContext::initialize()");

#if OC_ENABLE_STATS
    core::diagnostics::logMemoryFootprint("standalone-entry");
#endif

#if defined(MS_UX_RECORDER)
    ux_surface_registry_.clear();
    core::validation::ux::setCurrentSemanticUxContextProvider(&ux_surface_registry_);
#endif

    oc::ui::lvgl::font::load(CORE_FONT_ENTRIES, CORE_FONT_COUNT);
    oc::ui::lvgl::font::load(STANDALONE_FONT_ENTRIES, STANDALONE_FONT_COUNT);
    linkCoreFontAliases();

    configureEncoders();
    if (!createUiAssembly()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::RESOURCE_EXHAUSTED, "standalone ui assembly"}
        );
    }
#if OC_ENABLE_STATS
    core::diagnostics::logMemoryFootprint("standalone-ui");
#endif
    if (!createOverlayAssembly()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::RESOURCE_EXHAUSTED, "standalone overlay assembly"}
        );
    }
#if OC_ENABLE_STATS
    core::diagnostics::logMemoryFootprint("standalone-overlays");
#endif
    if (!createFeatureAssembly()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::RESOURCE_EXHAUSTED, "standalone feature assembly"}
        );
    }
#if OC_ENABLE_STATS
    core::diagnostics::logMemoryFootprint("standalone-features");
#endif
    if (!createGlobalHandlerAssembly()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::RESOURCE_EXHAUSTED, "standalone handler assembly"}
        );
    }
#if OC_ENABLE_STATS
    core::diagnostics::logMemoryFootprint("standalone-global-handlers");
#endif
    if (!createFileSystemRpcEndpoint()) {
        return oc::type::Result<void>::err(
            {oc::type::ErrorCode::RESOURCE_EXHAUSTED, "filesystem rpc endpoint"}
        );
    }
#if OC_ENABLE_STATS
    core::diagnostics::logMemoryFootprint("standalone-filesystem-rpc");
#endif

    applyActiveView();
    ui_assembly_->show();

    OC_LOG_INFO("StandaloneContext ready");
    return oc::type::Result<void>::ok();
}

void StandaloneContext::update() {
    if (feature_assembly_) {
        feature_assembly_->update(core::time_compat::millis());
    }
    if (filesystem_rpc_endpoint_) {
        filesystem_rpc_endpoint_->update();
    }
}

FLASHMEM void StandaloneContext::onCleanup() {
#if defined(MS_UX_RECORDER)
    core::validation::ux::clearCurrentSemanticUxContextProvider(&ux_surface_registry_);
    ux_surface_registry_.clear();
#endif

    // Ensure overlay stack is reset while UI objects are still alive.
    // CoreState survives context switches.
    if (overlay_assembly_) {
        overlay_assembly_->controller().hideAll();
    } else {
        core_state_.overlays.hideAll();
    }
    cleanupFileSystemRpcEndpoint();
    core_state_.resetStandaloneTransientUi();
    cleanupGlobalHandlerAssembly();
    cleanupFeatureAssembly();
    cleanupOverlayAssembly();
    cleanupUiAssembly();
}

// =============================================================================
// Private Methods
// =============================================================================

FLASHMEM void StandaloneContext::configureEncoders() {
    // Configure special encoders once (avoid hidden handler coupling)
    encoders().setMode(Config::EncoderID::NAV, oc::interface::EncoderMode::RELATIVE);
    encoders().setMode(Config::EncoderID::OPT, oc::interface::EncoderMode::NORMALIZED);

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        encoders().setMode(Config::MACRO_ENCODERS[i], oc::interface::EncoderMode::NORMALIZED);
    }
}

FLASHMEM bool StandaloneContext::createUiAssembly() {
    ui_assembly_ = core::app::makeExtmemUnique<core::context::standalone::StandaloneUiAssembly>(
        core_state_
    );
    if (!ui_assembly_) {
        OC_LOG_ERROR("StandaloneContext: UI assembly PSRAM allocation failed");
        return false;
    }
    if (!ui_assembly_->initialize()) {
        OC_LOG_ERROR("StandaloneContext: UI assembly initialization failed");
        return false;
    }
    if (!setupActiveViewSwitching()) {
        OC_LOG_ERROR("StandaloneContext: active view binding failed");
        return false;
    }
    return true;
}

FLASHMEM bool StandaloneContext::createOverlayAssembly() {
    overlay_assembly_ =
        core::app::makeExtmemUnique<core::context::standalone::StandaloneOverlayAssembly>(
            core_state_,
            buttons(),
            ui_assembly_->overlayRoot(),
            [this]() -> oc::type::ScopeID { return activeViewScopeId(); }
        );
    if (!overlay_assembly_ || !overlay_assembly_->valid()) {
        OC_LOG_ERROR("StandaloneContext: overlay assembly initialization failed");
        return false;
    }
    view_selector_render_scheduler_ =
        core::app::makeExtmemUnique<core::ui::CoalescedLvglRenderScheduler>(
            core::ui::renderSchedulerDebugLabel("StandaloneViewSelector"),
            &StandaloneContext::drainViewSelectorRender,
            this
        );
    if (!view_selector_render_scheduler_ || !view_selector_render_scheduler_->valid()) {
        OC_LOG_ERROR("StandaloneContext: view selector scheduler allocation failed");
        return false;
    }
    return setupViewSelectorRendering();
}

FLASHMEM void StandaloneContext::cleanupOverlayAssembly() {
    view_selector_render_scheduler_.reset();
    overlay_assembly_.reset();
}

FLASHMEM void StandaloneContext::cleanupUiAssembly() {
    ui_assembly_.reset();
}

FLASHMEM bool StandaloneContext::createFeatureAssembly() {
    if (!ui_assembly_ || !overlay_assembly_) return false;
    syncEncodersFromState();
    feature_assembly_ = core::app::makeExtmemUnique<core::context::standalone::StandaloneFeatureAssembly>(
        core_state_,
        product_files_,
        overlay_assembly_->controller(),
        overlay_assembly_->presentationRegistry(),
        encoders(),
        buttons(),
        midi(),
        ui_assembly_->overlayRoot(),
        ui_assembly_->macroViewElement(),
        ui_assembly_->sequencerViewElement(),
        ui_assembly_->projectViewElement(),
        ui_assembly_->contextSoftkeyBar(),
        ui_assembly_->transportBar(),
        ui_assembly_->macroViewScope(),
        ui_assembly_->sequencerViewScope(),
        ui_assembly_->deviceSettingsViewScope()
#if defined(MS_UX_RECORDER)
        ,
        &ux_surface_registry_
#endif
    );
    if (!feature_assembly_ || !feature_assembly_->valid()) {
        OC_LOG_ERROR("StandaloneContext: feature assembly initialization failed");
        return false;
    }
    return true;
}

FLASHMEM bool StandaloneContext::createGlobalHandlerAssembly() {
    if (!ui_assembly_ || !overlay_assembly_) return false;
    global_handler_assembly_ =
        core::app::makeExtmemUnique<core::context::standalone::StandaloneGlobalHandlerAssembly>(
            core_state_,
            overlay_assembly_->controller(),
            overlay_assembly_->viewSelectorElement(),
            encoders(),
            buttons(),
            ui_assembly_->macroViewScope(),
            ui_assembly_->sequencerViewScope(),
            ui_assembly_->projectViewScope(),
            ui_assembly_->deviceSettingsViewScope()
#if defined(MS_UX_RECORDER)
            ,
            &ux_surface_registry_
#endif
        );
    if (!global_handler_assembly_ || !global_handler_assembly_->valid()) {
        OC_LOG_ERROR("StandaloneContext: global handler assembly initialization failed");
        return false;
    }
    registerMidiRouting();
    OC_LOG_INFO("Input bindings buttons={}/{} encoders={}/{}",
                static_cast<unsigned>(buttons().bindingCount()),
                static_cast<unsigned>(buttons().bindingCapacity()),
                static_cast<unsigned>(encoders().bindingCount()),
                static_cast<unsigned>(encoders().bindingCapacity()));
    return true;
}

FLASHMEM bool StandaloneContext::createFileSystemRpcEndpoint() {
    filesystem_rpc_endpoint_ =
        core::app::makeExtmemUnique<core::protocol::filesystem::FileSystemRpcEndpoint>(
            frames(),
            product_files_,
            &core::time_compat::millis
        );
    if (!filesystem_rpc_endpoint_) {
        OC_LOG_ERROR("Filesystem RPC endpoint allocation failed");
        return false;
    }
    filesystem_rpc_endpoint_->begin();
    return true;
}

FLASHMEM void StandaloneContext::registerMidiRouting() {
    // MIDI input is routed through the framework EventBus (never via MidiAPI callbacks)
    onMidiCC([this](uint8_t ch, uint8_t cc, uint8_t val) {
        if (feature_assembly_) feature_assembly_->onMacroCC(ch, cc, val);
    });
    onMidiNoteOn([this](uint8_t, uint8_t, uint8_t) {
        if (feature_assembly_) feature_assembly_->onMacroNoteIn();
    });
    onMidiNoteOff([this](uint8_t, uint8_t, uint8_t) {
        if (feature_assembly_) feature_assembly_->onMacroNoteIn();
    });
}

FLASHMEM void StandaloneContext::cleanupGlobalHandlerAssembly() {
    global_handler_assembly_.reset();
}

FLASHMEM void StandaloneContext::cleanupFileSystemRpcEndpoint() {
    filesystem_rpc_endpoint_.reset();
}

FLASHMEM void StandaloneContext::cleanupFeatureAssembly() {
    feature_assembly_.reset();
}

FLASHMEM void StandaloneContext::syncEncodersFromState() {
    for (uint8_t i = 0; i < core::state::MACRO_COUNT; ++i) {
        float value = core_state_.macros.slots[i].value.get();
        // Macro view defaults: 0..1 continuous
        encoders().setDiscreteTicksPerStep(
            Config::MACRO_ENCODERS[i],
            input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP
        );
        encoders().setNormalizedTurns(
            Config::MACRO_ENCODERS[i],
            input_utils::DEFAULT_NORMALIZED_TURNS
        );
        encoders().setContinuous(Config::MACRO_ENCODERS[i]);
        encoders().setPosition(Config::MACRO_ENCODERS[i], value);
    }

    encoders().setDiscreteTicksPerStep(
        Config::EncoderID::OPT,
        input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP
    );
    encoders().setMode(
        Config::EncoderID::OPT,
        oc::interface::EncoderMode::NORMALIZED
    );
    encoders().setBounds(Config::EncoderID::OPT, 0.0f, 1.0f);
    encoders().setNormalizedTurns(
        Config::EncoderID::OPT,
        input_utils::DEFAULT_NORMALIZED_TURNS
    );
    encoders().setContinuous(Config::EncoderID::OPT);

    // The Macro root owns a normalized continuous OPT contract. Reset the
    // Sequencer's cache as well so a later return reapplies its own contract.
    if (feature_assembly_) {
        feature_assembly_->resetSequencerEncoderSync();
    }

    OC_LOG_DEBUG("Synced encoder positions from restored state");
}

FLASHMEM bool StandaloneContext::setupViewSelectorRendering() {
    view_selector_watcher_.bind<&StandaloneContext::requestViewSelectorRender>(
        *this, 0, "Standalone.viewSelector"
    );
    return view_selector_render_scheduler_ && view_selector_render_scheduler_->valid() &&
           view_selector_watcher_.watchAll(
        core_state_.viewSelector.visible,
        core_state_.viewSelector.selectedIndex,
        core_state_.projectHistory.revision
    );
}

FLASHMEM void StandaloneContext::requestViewSelectorRender() {
    const bool visible = core_state_.viewSelector.visible.get();
    if (!visible && !view_selector_was_visible_) return;
    view_selector_was_visible_ = visible;
    syncViewSelectorChrome();
    if (view_selector_render_scheduler_) {
        view_selector_render_scheduler_->request(1U);
    }
}

FLASHMEM void StandaloneContext::drainViewSelectorRender(void* context, uint32_t flags) {
    if ((flags & 1U) == 0) return;
    auto* self = static_cast<StandaloneContext*>(context);
    if (self) self->renderViewSelectorProjection();
}

FLASHMEM void StandaloneContext::renderViewSelectorProjection() {
    if (!overlay_assembly_) return;
    overlay_assembly_->renderViewSelector(
        core_state_.viewSelector.selectedIndex.get(),
        core_state_.viewSelector.visible.get()
    );
}

FLASHMEM void StandaloneContext::syncViewSelectorChrome() {
    if (!ui_assembly_) return;

    if (core_state_.viewSelector.visible.get()) {
        char undoLabel[40]{};
        char redoLabel[40]{};
        core_state_.projectHistory.formatUndoLabel(undoLabel, sizeof(undoLabel));
        core_state_.projectHistory.formatRedoLabel(redoLabel, sizeof(redoLabel));
        ui_assembly_->contextSoftkeyBar().setLabels(
            undoLabel,
            redoLabel,
            "Nav Select"
        );
        ui_assembly_->contextSoftkeyBar().show();
        ui_assembly_->transportBar().hide();
    } else {
        ui_assembly_->contextSoftkeyBar().hide();
        ui_assembly_->transportBar().show();
    }
}

FLASHMEM bool StandaloneContext::setupActiveViewSwitching() {
    active_view_watcher_.bind<&StandaloneContext::applyActiveView>(
        *this, 0, "Standalone.activeView"
    );
    return active_view_watcher_.watchAll(core_state_.activeView);
}

FLASHMEM oc::type::ScopeID StandaloneContext::activeViewScopeId() const {
    if (!ui_assembly_) return 0;

    switch (core_state_.activeView.get()) {
        case core::ui::ViewType::SEQUENCER:
            return ui_assembly_->sequencerViewScope();
        case core::ui::ViewType::PROJECT:
        case core::ui::ViewType::MODULATORS:
            return ui_assembly_->projectViewScope();
        case core::ui::ViewType::DEVICE_SETTINGS:
            return ui_assembly_->deviceSettingsViewScope();
        case core::ui::ViewType::MACRO:
        default:
            return ui_assembly_->macroViewScope();
    }
}

FLASHMEM void StandaloneContext::applyActiveView() {
    const auto targetView = core_state_.activeView.get();
    OC_PERF_SCOPE(perfTransition, "ui.view-transition");
    OC_PERF_UNITS(perfTransition, static_cast<uint32_t>(targetView), 0U);
    for (auto step : core::context::standalone::makeActiveViewLifecyclePlan(targetView)) {
        switch (step) {
            case core::context::standalone::ActiveViewLifecycleStep::DEACTIVATE_MACRO:
                if (ui_assembly_) ui_assembly_->deactivateMacroView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER:
                if (ui_assembly_) ui_assembly_->deactivateSequencerView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::DEACTIVATE_PROJECT:
                if (ui_assembly_) ui_assembly_->deactivateProjectView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::DEACTIVATE_DEVICE_SETTINGS:
                if (ui_assembly_) ui_assembly_->deactivateDeviceSettingsView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::ACTIVATE_MACRO:
                core::context::standalone::prepareMacroViewActivation(core_state_);
                if (ui_assembly_) ui_assembly_->activateMacroView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::ACTIVATE_SEQUENCER:
                if (ui_assembly_) ui_assembly_->activateSequencerView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::ACTIVATE_PROJECT:
                if (ui_assembly_) ui_assembly_->activateProjectView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::ACTIVATE_DEVICE_SETTINGS:
                if (ui_assembly_) ui_assembly_->activateDeviceSettingsView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::SYNC_MACRO_ENCODERS:
                syncEncodersFromState();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::SYNC_SEQUENCER_ENCODERS:
                if (feature_assembly_) {
                    feature_assembly_->syncSequencerEncodersNow();
                }
                break;
            case core::context::standalone::ActiveViewLifecycleStep::SYNC_PROJECT_ENCODER:
                if (feature_assembly_) {
                    feature_assembly_->syncProjectEncoderNow();
                }
                break;
            case core::context::standalone::ActiveViewLifecycleStep::NONE:
                break;
        }
    }
}

}  // namespace core::context

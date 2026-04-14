#include "StandaloneContext.hpp"

#include <oc/log/Log.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/interface/IEncoder.hpp>
#include <oc/ui/lvgl/FontLoader.hpp>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include "context/standalone/ActiveViewLifecyclePlan.hpp"
#include "context/standalone/StandaloneFeatureAssembly.hpp"
#include "context/standalone/StandaloneGlobalHandlerAssembly.hpp"
#include "context/standalone/StandaloneOverlayAssembly.hpp"
#include "context/standalone/StandaloneUiAssembly.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "state/CoreState.hpp"

#include <ms/ui/font/CoreFonts.hpp>
#include "ui/font/StandaloneFonts.hpp"

namespace core::context {

namespace input_utils = core::handler::sequencer::input_utils;

// Constructor and destructor must be in .cpp where handler types are complete
StandaloneContext::StandaloneContext(core::state::CoreState& state) : core_state_(state) {}

StandaloneContext::~StandaloneContext() = default;

// =============================================================================
// IContext Lifecycle
// =============================================================================

FLASHMEM oc::type::Result<void> StandaloneContext::init() {
    OC_LOG_INFO("StandaloneContext::initialize()");

    oc::ui::lvgl::font::load(CORE_FONT_ENTRIES, CORE_FONT_COUNT);
    oc::ui::lvgl::font::load(STANDALONE_FONT_ENTRIES, STANDALONE_FONT_COUNT);
    linkCoreFontAliases();

    OC_LOG_DEBUG("StandaloneContext: configureEncoders");
    configureEncoders();
    OC_LOG_DEBUG("StandaloneContext: createUiAssembly");
    createUiAssembly();
    OC_LOG_DEBUG("StandaloneContext: createOverlayAssembly");
    createOverlayAssembly();
    OC_LOG_DEBUG("StandaloneContext: createFeatureAssembly");
    createFeatureAssembly();
    OC_LOG_DEBUG("StandaloneContext: createGlobalHandlerAssembly");
    createGlobalHandlerAssembly();

    OC_LOG_DEBUG("StandaloneContext: show");
    ui_assembly_->show();

    OC_LOG_INFO("StandaloneContext ready");
    return oc::type::Result<void>::ok();
}

void StandaloneContext::update() {
    if (feature_assembly_) {
        feature_assembly_->update();
    }
}

FLASHMEM void StandaloneContext::onCleanup() {
    // Ensure overlay stack is reset while UI objects are still alive.
    // CoreState survives context switches.
    if (overlay_assembly_) {
        overlay_assembly_->controller().hideAll();
    } else {
        core_state_.overlays.hideAll();
    }
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

FLASHMEM void StandaloneContext::createUiAssembly() {
    ui_assembly_ = core::app::makeExtmemUnique<core::context::standalone::StandaloneUiAssembly>(
        core_state_
    );
    setupActiveViewSwitching();
    applyActiveView();
}

FLASHMEM void StandaloneContext::createOverlayAssembly() {
    overlay_assembly_ =
        core::app::makeExtmemUnique<core::context::standalone::StandaloneOverlayAssembly>(
            core_state_,
            buttons(),
            ui_assembly_->mainZone(),
            [this]() -> oc::type::ScopeID { return activeViewScopeId(); }
        );
    setupViewSelectorRendering();
}

FLASHMEM void StandaloneContext::cleanupOverlayAssembly() {
    overlay_assembly_.reset();
}

FLASHMEM void StandaloneContext::cleanupUiAssembly() {
    ui_assembly_.reset();
}

FLASHMEM void StandaloneContext::createFeatureAssembly() {
    syncEncodersFromState();
    feature_assembly_ = core::app::makeExtmemUnique<core::context::standalone::StandaloneFeatureAssembly>(
        core_state_,
        overlay_assembly_->controller(),
        encoders(),
        buttons(),
        midi(),
        rawEvents(),
        ui_assembly_->mainZone(),
        ui_assembly_->macroViewElement(),
        ui_assembly_->sequencerViewElement(),
        ui_assembly_->contextSoftkeyBar(),
        ui_assembly_->transportBar(),
        ui_assembly_->macroViewScope(),
        ui_assembly_->sequencerViewScope()
    );
}

FLASHMEM void StandaloneContext::createGlobalHandlerAssembly() {
    registerMidiRouting();
    global_handler_assembly_ =
        core::app::makeExtmemUnique<core::context::standalone::StandaloneGlobalHandlerAssembly>(
            core_state_,
            overlay_assembly_->controller(),
            overlay_assembly_->viewSelectorElement(),
            encoders(),
            buttons(),
            ui_assembly_->macroViewScope(),
            ui_assembly_->sequencerViewScope()
        );
    OC_LOG_INFO("Input bindings buttons={}/{} encoders={}/{}",
                static_cast<unsigned>(buttons().bindingCount()),
                static_cast<unsigned>(buttons().bindingCapacity()),
                static_cast<unsigned>(encoders().bindingCount()),
                static_cast<unsigned>(encoders().bindingCapacity()));
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
    encoders().setNormalizedTurns(
        Config::EncoderID::OPT,
        input_utils::DEFAULT_NORMALIZED_TURNS
    );

    // Leaving Sequencer view disables discrete steps.
    if (feature_assembly_) {
        feature_assembly_->resetSequencerEncoderSync();
    }

    OC_LOG_DEBUG("Synced encoder positions from restored state");
}

FLASHMEM void StandaloneContext::setupViewSelectorRendering() {
    view_selector_watcher_.watchAll(
        [this]() {
            if (!overlay_assembly_) return;
            overlay_assembly_->renderViewSelector(
                core_state_.viewSelector.selectedIndex.get(),
                core_state_.viewSelector.visible.get()
            );
        },
        core_state_.viewSelector.visible,
        core_state_.viewSelector.selectedIndex
    );
}

FLASHMEM void StandaloneContext::setupActiveViewSwitching() {
    active_view_watcher_.watchAll(
        [this]() { applyActiveView(); },
        core_state_.activeView
    );
}

FLASHMEM oc::type::ScopeID StandaloneContext::activeViewScopeId() const {
    if (!ui_assembly_) return 0;

    switch (core_state_.activeView.get()) {
        case core::ui::ViewType::SEQUENCER:
            return ui_assembly_->sequencerViewScope();
        case core::ui::ViewType::MACRO:
        default:
            return ui_assembly_->macroViewScope();
    }
}

FLASHMEM void StandaloneContext::applyActiveView() {
    for (auto step :
         core::context::standalone::makeActiveViewLifecyclePlan(core_state_.activeView.get())) {
        switch (step) {
            case core::context::standalone::ActiveViewLifecycleStep::DEACTIVATE_MACRO:
                if (ui_assembly_) ui_assembly_->deactivateMacroView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER:
                if (ui_assembly_) ui_assembly_->deactivateSequencerView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::ACTIVATE_MACRO:
                if (ui_assembly_) ui_assembly_->activateMacroView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::ACTIVATE_SEQUENCER:
                if (ui_assembly_) ui_assembly_->activateSequencerView();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::SYNC_MACRO_ENCODERS:
                syncEncodersFromState();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::SYNC_SEQUENCER_ENCODERS:
                if (feature_assembly_) {
                    feature_assembly_->syncSequencerEncodersNow();
                }
                break;
        }
    }
}

}  // namespace core::context

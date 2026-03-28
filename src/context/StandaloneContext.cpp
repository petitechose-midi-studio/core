#include "StandaloneContext.hpp"

#include <string>
#include <vector>

#include <lvgl.h>

#include <oc/log/Log.hpp>
#include <oc/interface/IEncoder.hpp>
#include <oc/ui/lvgl/FontLoader.hpp>
#include <oc/ui/lvgl/Screen.hpp>
#include <oc/time/Time.hpp>

#include <config/App.hpp>
#include <config/PlatformCompat.hpp>
#include "context/standalone/ActiveViewLifecyclePlan.hpp"
#include "context/standalone/MacroFeatureModule.hpp"
#include "context/standalone/SequencerFeatureModule.hpp"
#include "context/standalone/SettingsFeatureModule.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "handler/transport/TransportHandler.hpp"
#include "handler/view/ViewSwitcherHandler.hpp"

#include <ms/ui/font/CoreFonts.hpp>
#include <ms/ui/widget/StringListSelector.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include "ui/font/StandaloneFonts.hpp"
#include <oc/context/OverlayManager.hpp>
#include "ui/transportbar/TransportBar.hpp"
#include "ui/transportbar/ContextSoftkeyBar.hpp"
#include "ui/view/MacroView.hpp"
#include "ui/view/SequencerView.hpp"
#include <ms/ui/ViewContainer.hpp>

namespace core::context {

namespace input_utils = core::handler::sequencer::input_utils;

namespace {
}  // namespace

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

    configureEncoders();
    createViewContainer();
    createViews();
    createBottomBar();
    createOverlayController();
    createViewSelectorOverlay();
    createFeatureModules();
    createGlobalHandlers();

    view_container_->show();

    OC_LOG_INFO("StandaloneContext ready");
    return oc::type::Result<void>::ok();
}

void StandaloneContext::update() {
}

FLASHMEM void StandaloneContext::onCleanup() {
    // Ensure overlay stack is reset while UI objects are still alive.
    // CoreState survives context switches.
    if (overlay_controller_) {
        overlay_controller_->hideAll();
    } else {
        core_state_.overlays.hideAll();
    }
    resetTransientUiState();
    cleanupGlobalHandlers();
    cleanupFeatureModules();
    cleanupOverlayController();
    cleanupViews();
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

FLASHMEM void StandaloneContext::createViewContainer() {
    view_container_ = std::make_unique<ms::ui::ViewContainer>(oc::ui::lvgl::Screen::root());
}

FLASHMEM void StandaloneContext::createViews() {
    lv_obj_t* mainZone = view_container_->getMainZone();

    macro_view_ = std::make_unique<core::ui::MacroView>(mainZone, core_state_);
    sequencer_view_ = std::make_unique<core::ui::SequencerView>(mainZone, core_state_);
    setupActiveViewSwitching();
    applyActiveView();
}

FLASHMEM void StandaloneContext::createBottomBar() {
    lv_obj_t* bottomZone = view_container_->getBottomZone();
    transport_bar_ = std::make_unique<core::ui::TransportBar>(bottomZone, core_state_.statusBar);
    context_softkey_bar_ = std::make_unique<core::ui::ContextSoftkeyBar>(bottomZone);
}

FLASHMEM void StandaloneContext::createOverlayController() {
    overlay_controller_ = std::make_unique<oc::context::OverlayManager<core::ui::OverlayType>>(
        core_state_.overlays,
        buttons()
    );
    overlay_controller_->setActiveViewProvider([this]() -> oc::type::ScopeID {
        switch (core_state_.activeView.get()) {
            case core::ui::ViewType::SEQUENCER:
                return sequencer_view_ ? oc::ui::lvgl::scopeID(sequencer_view_->getElement()) : 0;
            case core::ui::ViewType::MACRO:
            default:
                return macro_view_ ? oc::ui::lvgl::scopeID(macro_view_->getElement()) : 0;
        }
    });
}

FLASHMEM void StandaloneContext::createViewSelectorOverlay() {
    lv_obj_t* mainZone = view_container_->getMainZone();

    // Global overlay: ViewSelector (parent = mainZone so it covers views but not TransportBar)
    view_selector_ = std::make_unique<ms::ui::StringListSelector>(mainZone);
    view_selector_->setTitle("Select View");
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::VIEW_SELECTOR,
        reinterpret_cast<oc::type::ScopeID>(view_selector_->getElement()),
        static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP)
    );
    setupViewSelectorRendering();
}

FLASHMEM void StandaloneContext::createFeatureModules() {
    syncEncodersFromState();
    macro_feature_ = std::make_unique<core::context::standalone::MacroFeatureModule>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        midi(),
        view_container_->getMainZone(),
        macro_view_->getElement()
    );
    sequencer_feature_ = std::make_unique<core::context::standalone::SequencerFeatureModule>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        sequencer_view_->getElement()
    );
    settings_feature_ = std::make_unique<core::context::standalone::SettingsFeatureModule>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        view_container_->getMainZone(),
        *context_softkey_bar_,
        *transport_bar_,
        core::handler::DataManagerHandler::ViewScopes{
            macro_view_->getElement(),
            sequencer_view_->getElement(),
        }
    );
}

FLASHMEM void StandaloneContext::createGlobalHandlers() {
    // MIDI input is routed through the framework EventBus (never via MidiAPI callbacks)
    onMidiCC([this](uint8_t ch, uint8_t cc, uint8_t val) {
        if (macro_feature_) macro_feature_->onCC(ch, cc, val);
    });
    onMidiNoteOn([this](uint8_t, uint8_t, uint8_t) {
        if (macro_feature_) macro_feature_->onNoteIn();
    });
    onMidiNoteOff([this](uint8_t, uint8_t, uint8_t) {
        if (macro_feature_) macro_feature_->onNoteIn();
    });

    transport_handler_ = std::make_unique<core::handler::TransportHandler>(
        core_state_,
        encoders(),
        buttons(),
        macro_view_->getElement(),
        core::handler::TransportHandler::ViewScopes{
            macro_view_->getElement(),
            sequencer_view_->getElement(),
        }
    );

    {
        using OverlayCtx = ms::ui::OverlayBindingContext<core::ui::OverlayType>;
        OverlayCtx ctx{*overlay_controller_, nullptr, view_selector_->getElement()};
        view_switcher_handler_ = std::make_unique<core::handler::ViewSwitcherHandler>(
            core_state_,
            ctx,
            encoders(),
            buttons(),
            core::handler::ViewSwitcherHandler::ViewScopes{
                macro_view_->getElement(),
                sequencer_view_->getElement(),
            }
        );
    }
}

FLASHMEM void StandaloneContext::resetTransientUiState() {
    core_state_.macroEdit.reset();
    core_state_.sequencer.stepEdit.reset();
    core_state_.sequencer.stepPropertyInlineSelector.reset();
    core_state_.sequencer.patternQuickControls.reset();
    core_state_.sequencer.rangeSelection.reset();
    core_state_.globalSettings.reset();
    core_state_.dataManager.resetSession(core::state::DataManagerContext::MACRO);
}

FLASHMEM void StandaloneContext::cleanupGlobalHandlers() {
    view_switcher_handler_.reset();
    transport_handler_.reset();
}

FLASHMEM void StandaloneContext::cleanupFeatureModules() {
    settings_feature_.reset();
    sequencer_feature_.reset();
    macro_feature_.reset();
}

FLASHMEM void StandaloneContext::cleanupOverlayController() {
    view_selector_.reset();

    // Overlay controller clears authority resolver after overlay views are gone.
    overlay_controller_.reset();
}

FLASHMEM void StandaloneContext::cleanupViews() {
    context_softkey_bar_.reset();
    transport_bar_.reset();

    if (macro_view_) {
        macro_view_->onDeactivate();
        macro_view_.reset();
    }
    if (sequencer_view_) {
        sequencer_view_->onDeactivate();
        sequencer_view_.reset();
    }

    view_container_.reset();
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
    if (sequencer_feature_) {
        sequencer_feature_->resetEncoderSync();
    }

    OC_LOG_DEBUG("Synced encoder positions from restored state");
}

FLASHMEM void StandaloneContext::setupViewSelectorRendering() {
    view_selector_watcher_.watchAll(
        [this]() { renderViewSelector(); },
        core_state_.viewSelector.visible,
        core_state_.viewSelector.selectedIndex
    );
}

FLASHMEM void StandaloneContext::renderViewSelector() {
    if (!view_selector_) return;
    static const std::vector<std::string> VIEW_NAMES = {"Macros", "Sequencer"};

    view_selector_->render({
        .items = &VIEW_NAMES,
        .selectedIndex = core_state_.viewSelector.selectedIndex.get(),
        .visible = core_state_.viewSelector.visible.get()
    });
}

FLASHMEM void StandaloneContext::setupActiveViewSwitching() {
    active_view_watcher_.watchAll(
        [this]() { applyActiveView(); },
        core_state_.activeView
    );
}

FLASHMEM void StandaloneContext::applyActiveView() {
    for (auto step :
         core::context::standalone::makeActiveViewLifecyclePlan(core_state_.activeView.get())) {
        switch (step) {
            case core::context::standalone::ActiveViewLifecycleStep::DEACTIVATE_MACRO:
                if (macro_view_) macro_view_->onDeactivate();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER:
                if (sequencer_view_) sequencer_view_->onDeactivate();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::ACTIVATE_MACRO:
                if (macro_view_) macro_view_->onActivate();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::ACTIVATE_SEQUENCER:
                if (sequencer_view_) sequencer_view_->onActivate();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::SYNC_MACRO_ENCODERS:
                syncEncodersFromState();
                break;
            case core::context::standalone::ActiveViewLifecycleStep::SYNC_SEQUENCER_ENCODERS:
                if (sequencer_feature_) {
                    sequencer_feature_->syncEncodersNow();
                }
                break;
        }
    }
}

}  // namespace core::context

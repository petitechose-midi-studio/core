#include "StandaloneContext.hpp"

#include <string>
#include <vector>
#include <algorithm>

#include <lvgl.h>

#include <oc/log/Log.hpp>
#include <oc/interface/IEncoder.hpp>
#include <oc/ui/lvgl/FontLoader.hpp>
#include <oc/ui/lvgl/Screen.hpp>
#include <oc/time/Time.hpp>

#include <config/App.hpp>
#include "handler/macro/MacroEditHandler.hpp"
#include "handler/macro/MacroMidiHandler.hpp"
#include "handler/macro/MacroValueHandler.hpp"
#include "handler/sequencer/SequencerPatternConfigHandler.hpp"
#include "handler/sequencer/SequencerPropertySelectorHandler.hpp"
#include "handler/sequencer/SequencerMacroPropertyHandler.hpp"
#include "handler/sequencer/SequencerStepEditHandler.hpp"
#include "handler/sequencer/SequencerStepHandler.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"
#include "handler/transport/TransportHandler.hpp"
#include "handler/view/ViewSwitcherHandler.hpp"

#include <cstdio>

#include <ms/ui/font/CoreFonts.hpp>
#include <ms/ui/widget/StringListSelector.hpp>
#include <ms/ui/widget/VirtualListKeyValueOverlay.hpp>
#include <ms/ui/widget/VirtualListSelectorOverlay.hpp>
#include <oc/ui/lvgl/Scope.hpp>
#include "ui/font/StandaloneFonts.hpp"
#include "ui/macro/MacroEditOverlay.hpp"
#include <oc/context/OverlayManager.hpp>
#include "ui/transportbar/TransportBar.hpp"
#include "ui/view/MacroView.hpp"
#include "ui/view/SequencerView.hpp"
#include <ms/ui/ViewContainer.hpp>

#include "sequencer/SequencerPlaybackService.hpp"
#include "midi/MidiUtils.hpp"

namespace core::context {

namespace input_utils = core::handler::sequencer::input_utils;

// Constructor and destructor must be in .cpp where handler types are complete
StandaloneContext::StandaloneContext(core::state::CoreState& state) : core_state_(state) {}

StandaloneContext::~StandaloneContext() = default;

// =============================================================================
// IContext Lifecycle
// =============================================================================

oc::type::Result<void> StandaloneContext::init() {
    OC_LOG_INFO("StandaloneContext::initialize()");

    oc::ui::lvgl::font::load(CORE_FONT_ENTRIES, CORE_FONT_COUNT);
    oc::ui::lvgl::font::load(STANDALONE_FONT_ENTRIES, STANDALONE_FONT_COUNT);
    linkCoreFontAliases();

    // Configure special encoders once (avoid hidden handler coupling)
    encoders().setMode(Config::EncoderID::NAV, oc::interface::EncoderMode::RELATIVE);
    encoders().setMode(Config::EncoderID::OPT, oc::interface::EncoderMode::NORMALIZED);

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        encoders().setMode(Config::MACRO_ENCODERS[i], oc::interface::EncoderMode::NORMALIZED);
    }

    // Sync encoder positions with restored values BEFORE creating handlers
    syncEncodersFromState();

    // Create UI container with zones
    view_container_ = std::make_unique<ms::ui::ViewContainer>(oc::ui::lvgl::Screen::root());
    lv_obj_t* mainZone = view_container_->getMainZone();

    // Create views (activate via activeView signal)
    macro_view_ = std::make_unique<core::ui::MacroView>(mainZone, core_state_);
    sequencer_view_ = std::make_unique<core::ui::SequencerView>(mainZone, core_state_);
    setupActiveViewSwitching();
    applyActiveView();

    // Create TransportBar in bottom zone
    transport_bar_ = std::make_unique<core::ui::TransportBar>(
        view_container_->getBottomZone(),
        core_state_.statusBar
    );

    // Create overlay manager with AuthorityResolver
    overlay_controller_ = std::make_unique<oc::context::OverlayManager<core::ui::OverlayType>>(
        core_state_.overlays, buttons()
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

    // Global overlay: ViewSelector (parent = mainZone so it covers views but not TransportBar)
    view_selector_ = std::make_unique<ms::ui::StringListSelector>(mainZone);
    view_selector_->setTitle("Select View");
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::VIEW_SELECTOR,
        reinterpret_cast<oc::type::ScopeID>(view_selector_->getElement()),
        static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_TOP)
    );
    setupViewSelectorRendering();

    // Create MacroEdit overlay (parented to MacroView)
    macro_edit_overlay_ = std::make_unique<core::ui::MacroEditOverlay>(macro_view_->getElement());

    // Register overlay cleanup
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::MACRO_EDIT,
        reinterpret_cast<oc::type::ScopeID>(macro_edit_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)  // No latch button
    );

    // Setup rendering subscriptions for MacroEditOverlay (orchestrator pattern)
    setupMacroEditRendering();

    // Create Sequencer overlays (parented to SequencerView)
    seq_pattern_config_overlay_ = std::make_unique<ms::ui::VirtualListKeyValueOverlay>(
        sequencer_view_->getElement()
    );
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::SEQ_PATTERN_CONFIG,
        oc::ui::lvgl::scopeID(seq_pattern_config_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_CENTER)
    );
    setupSequencerPatternConfigRendering();

    seq_step_edit_overlay_ = std::make_unique<ms::ui::VirtualListKeyValueOverlay>(
        sequencer_view_->getElement()
    );
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::SEQ_STEP_EDIT,
        oc::ui::lvgl::scopeID(seq_step_edit_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );
    setupSequencerStepEditRendering();

    seq_property_selector_overlay_ = std::make_unique<ms::ui::VirtualListSelectorOverlay>(
        sequencer_view_->getElement()
    );
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::SEQ_PROPERTY_SELECTOR,
        oc::ui::lvgl::scopeID(seq_property_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(Config::ButtonID::LEFT_BOTTOM)
    );
    setupSequencerPropertySelectorRendering();
    setupSequencerMacroEncoderSync();

    // Create handlers (bindings scoped to view element)
    input_handler_ = std::make_unique<core::handler::MacroValueHandler>(
        core_state_, encoders(), midi(), macro_view_->getElement()
    );

    // Sequencer input handler (scoped to SequencerView)
    sequencer_step_handler_ = std::make_unique<core::handler::SequencerStepHandler>(
        core_state_, encoders(), buttons(), sequencer_view_->getElement()
    );

    // Sequencer PatternConfig handler (two-level scoping)
    sequencer_pattern_config_handler_ = std::make_unique<core::handler::SequencerPatternConfigHandler>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        sequencer_view_->getElement(),
        seq_pattern_config_overlay_->getElement()
    );

    // Sequencer StepEdit handler (two-level scoping)
    sequencer_step_edit_handler_ = std::make_unique<core::handler::SequencerStepEditHandler>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        sequencer_view_->getElement(),
        seq_step_edit_overlay_->getElement()
    );

    sequencer_property_selector_handler_ = std::make_unique<core::handler::SequencerPropertySelectorHandler>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        sequencer_view_->getElement(),
        seq_property_selector_overlay_->getElement()
    );

    sequencer_macro_property_handler_ = std::make_unique<core::handler::SequencerMacroPropertyHandler>(
        core_state_,
        encoders(),
        sequencer_view_->getElement()
    );

    midi_handler_ = std::make_unique<core::handler::MacroMidiHandler>(
        core_state_, encoders()
    );

    // MIDI input is routed through the framework EventBus (never via MidiAPI callbacks)
    onMidiCC([this](uint8_t ch, uint8_t cc, uint8_t val) {
        if (midi_handler_) midi_handler_->onCC(ch, cc, val);
    });
    onMidiNoteOn([this](uint8_t, uint8_t, uint8_t) {
        if (midi_handler_) midi_handler_->onNoteIn();
    });
    onMidiNoteOff([this](uint8_t, uint8_t, uint8_t) {
        if (midi_handler_) midi_handler_->onNoteIn();
    });
    transport_handler_ = std::make_unique<core::handler::TransportHandler>(
        core_state_, encoders(), buttons(),
        macro_view_->getElement(),  // Tempo only in Macro view scope
        // Play/Pause is explicitly mirrored per top-level view scope.
        // This keeps strict authority (overlay > active view > global) without
        // relying on global bindings.
        core::handler::TransportHandler::ViewScopes{
            macro_view_->getElement(),
            sequencer_view_->getElement(),
        }
    );

    // View selector handler (LEFT_TOP + NAV)
    {
        using OverlayCtx = ms::ui::OverlayBindingContext<core::ui::OverlayType>;
        OverlayCtx ctx{*overlay_controller_, mainZone, view_selector_->getElement()};
        view_switcher_handler_ = std::make_unique<core::handler::ViewSwitcherHandler>(
            core_state_,
            ctx,
            encoders(),
            buttons(),
            // LEFT_TOP open is explicitly mirrored per top-level view scope.
            // Overlay interactions remain scoped to the selector overlay.
            core::handler::ViewSwitcherHandler::ViewScopes{
                macro_view_->getElement(),
                sequencer_view_->getElement(),
            }
        );
    }

    // Create MacroEdit input handler (two-level scoping)
    macro_edit_handler_ = std::make_unique<core::handler::MacroEditHandler>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        macro_view_->getElement(),              // MacroView scope (open trigger)
        macro_edit_overlay_->getElement()   // Overlay scope (edit/close)
    );

    // Global services (not tied to any view scope)
    sequencer_playback_ = std::make_unique<core::sequencer::SequencerPlaybackService>(
        core_state_.sequencer,
        core_state_.statusBar,
        midi()
    );

    view_container_->show();

    OC_LOG_INFO("StandaloneContext ready");
    return oc::type::Result<void>::ok();
}

void StandaloneContext::update() {
    if (sequencer_playback_) {
        sequencer_playback_->update(oc::time::millis());
    }
}

void StandaloneContext::onCleanup() {
    // Ensure overlay stack is reset while UI objects are still alive.
    // CoreState survives context switches.
    if (overlay_controller_) {
        overlay_controller_->hideAll();
    } else {
        core_state_.overlays.hideAll();
    }
    core_state_.macroEdit.reset();
    core_state_.sequencer.patternConfig.reset();
    core_state_.sequencer.stepEdit.reset();
    core_state_.sequencer.propertySelector.reset();

    if (sequencer_playback_) {
        sequencer_playback_->stop();
        sequencer_playback_.reset();
    }

    // Handlers first (they reference state/APIs)
    macro_edit_handler_.reset();
    view_switcher_handler_.reset();
    sequencer_property_selector_handler_.reset();
    sequencer_macro_property_handler_.reset();
    sequencer_pattern_config_handler_.reset();
    sequencer_step_edit_handler_.reset();
    sequencer_step_handler_.reset();
    transport_handler_.reset();
    input_handler_.reset();
    midi_handler_.reset();

    // SignalWatcher cleanup is automatic (RAII)

    // Overlay UI
    macro_edit_overlay_.reset();
    seq_pattern_config_overlay_.reset();
    seq_step_edit_overlay_.reset();
    seq_property_selector_overlay_.reset();
    view_selector_.reset();

    // Overlay controller (clears authority resolver)
    overlay_controller_.reset();

    // TransportBar (TopBar is now managed by MacroView)
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

// =============================================================================
// Private Methods
// =============================================================================

void StandaloneContext::syncEncodersFromState() {
    for (uint8_t i = 0; i < core::state::MACRO_COUNT; ++i) {
        float value = core_state_.macros.slots[i].value.get();
        // Macro view defaults: 0..1 continuous
        encoders().setContinuous(Config::MACRO_ENCODERS[i]);
        encoders().setPosition(Config::MACRO_ENCODERS[i], value);
    }

    // Leaving Sequencer view disables discrete steps.
    seq_macro_steps_configured_ = 0;
    seq_opt_steps_configured_ = 0;

    OC_LOG_DEBUG("Synced encoder positions from restored state");
}

void StandaloneContext::setupMacroEditRendering() {
    macro_edit_watcher_.watchAll(
        [this]() { renderMacroEdit(); },
        core_state_.macroEdit.visible,
        core_state_.macroEdit.editingIndex,
        core_state_.macroEdit.tempChannel,
        core_state_.macroEdit.tempCC,
        core_state_.macroEdit.focusedRow
    );
}

void StandaloneContext::setupSequencerPatternConfigRendering() {
    seq_pattern_config_watcher_.watchAll(
        [this]() { renderSequencerPatternConfig(); },
        core_state_.sequencer.patternConfig.visible,
        core_state_.sequencer.patternConfig.focusedRow,
        core_state_.sequencer.length,
        core_state_.sequencer.stepsPerBeat,
        core_state_.sequencer.midiChannel
    );
}

void StandaloneContext::renderSequencerPatternConfig() {
    if (!seq_pattern_config_overlay_) return;

    const bool visible = core_state_.sequencer.patternConfig.visible.get();
    if (!visible) {
        seq_pattern_config_overlay_->render({.visible = false});
        return;
    }

    const uint8_t len = core_state_.sequencer.length.get();
    const uint8_t stepsPerBeat = core_state_.sequencer.stepsPerBeat.get();
    const uint8_t ch0 = core_state_.sequencer.midiChannel.get();

    const uint32_t dataRevision =
        (static_cast<uint32_t>(len) << 24) |
        (static_cast<uint32_t>(stepsPerBeat) << 16) |
        (static_cast<uint32_t>(ch0) << 8);

    const uint16_t denom = stepsPerBeat > 0 ? static_cast<uint16_t>(4U * stepsPerBeat) : 0;

    char meta[32];
    if (denom > 0) {
        snprintf(meta, sizeof(meta), "1/%u  CH %u", static_cast<unsigned>(denom),
                 static_cast<unsigned>(ch0) + 1);
    } else {
        snprintf(meta, sizeof(meta), "CH %u", static_cast<unsigned>(ch0) + 1);
    }

    char lenStr[8];
    snprintf(lenStr, sizeof(lenStr), "%u", static_cast<unsigned>(len));

    char divStr[16];
    if (denom > 0) {
        snprintf(divStr, sizeof(divStr), "1/%u", static_cast<unsigned>(denom));
    } else {
        snprintf(divStr, sizeof(divStr), "?");
    }

    char chStr[8];
    snprintf(chStr, sizeof(chStr), "%u", static_cast<unsigned>(ch0) + 1);

    const ms::ui::KeyValueRow rows[] = {
        {.key = "LEN", .value = lenStr},
        {.key = "DIV", .value = divStr},
        {.key = "CH", .value = chStr},
    };

    seq_pattern_config_overlay_->render({
        .title = "PATTERN",
        .meta = meta,
        .rows = rows,
        .rowCount = 3,
        .selectedIndex = core_state_.sequencer.patternConfig.focusedRow.get(),
        .visible = true,
        .dataRevision = dataRevision,
    });
}

void StandaloneContext::setupSequencerStepEditRendering() {
    seq_step_edit_watcher_.watchAll(
        [this]() { renderSequencerStepEdit(); },
        core_state_.sequencer.stepEdit.visible,
        core_state_.sequencer.stepEdit.stepIndex,
        core_state_.sequencer.stepEdit.focusedRow,
        core_state_.sequencer.length,
        core_state_.sequencer.stepDataRevision
    );
}

void StandaloneContext::renderSequencerStepEdit() {
    if (!seq_step_edit_overlay_) return;

    const bool visible = core_state_.sequencer.stepEdit.visible.get();
    if (!visible) {
        seq_step_edit_overlay_->render({.visible = false});
        return;
    }

    const uint8_t abs = core_state_.sequencer.stepEdit.stepIndex.get();
    if (abs >= core::state::sequencer::SequencerState::MAX_STEPS) return;

    const uint8_t len = core_state_.sequencer.length.get();

    char title[16];
    snprintf(title, sizeof(title), "STEP %u", static_cast<unsigned>(abs) + 1);

    char meta[16];
    if (len > 0) {
        snprintf(meta, sizeof(meta), "%u/%u", static_cast<unsigned>(abs) + 1,
                 static_cast<unsigned>(len));
    } else {
        snprintf(meta, sizeof(meta), "%u", static_cast<unsigned>(abs) + 1);
    }

    const uint8_t note = core_state_.sequencer.note[abs];
    const uint8_t vel = core_state_.sequencer.velocity[abs];
    const uint16_t gate = core_state_.sequencer.gate[abs];

    const uint32_t dataRevision =
        core_state_.sequencer.stepDataRevision.get() ^
        (static_cast<uint32_t>(abs) << 16) ^
        (static_cast<uint32_t>(len) << 24);

    char noteName[8];
    core::midi::formatNoteName(noteName, sizeof(noteName), note);
    char noteStr[8];
    snprintf(noteStr, sizeof(noteStr), "%s", noteName);

    char velStr[8];
    snprintf(velStr, sizeof(velStr), "%u", static_cast<unsigned>(vel));

    char gateStr[12];
    snprintf(gateStr, sizeof(gateStr), "%u%%", static_cast<unsigned>(gate));

    const ms::ui::KeyValueRow rows[] = {
        {.key = "Note", .value = noteStr},
        {.key = "Velocity", .value = velStr},
        {.key = "Gate", .value = gateStr},
    };

    seq_step_edit_overlay_->render({
        .title = title,
        .meta = meta,
        .rows = rows,
        .rowCount = 3,
        .selectedIndex = core_state_.sequencer.stepEdit.focusedRow.get(),
        .visible = true,
        .dataRevision = dataRevision,
    });
}

void StandaloneContext::renderMacroEdit() {
    macro_edit_overlay_->render({
        .editingIndex = core_state_.macroEdit.editingIndex.get(),
        .channel = core_state_.macroEdit.tempChannel.get(),
        .cc = core_state_.macroEdit.tempCC.get(),
        .focusedRow = core_state_.macroEdit.focusedRow.get(),
        .visible = core_state_.macroEdit.visible.get()
    });
}

void StandaloneContext::setupSequencerPropertySelectorRendering() {
    seq_property_selector_watcher_.watchAll(
        [this]() { renderSequencerPropertySelector(); },
        core_state_.sequencer.propertySelector.visible,
        core_state_.sequencer.propertySelector.selectedIndex
    );
}

void StandaloneContext::renderSequencerPropertySelector() {
    if (!seq_property_selector_overlay_) return;
    static const char* const ITEMS[] = {"Note", "Velocity", "Gate"};

    seq_property_selector_overlay_->render({
        .title = "PROPERTY",
        .meta = "MACROS",
        .items = ITEMS,
        .itemCount = 3,
        .selectedIndex = core_state_.sequencer.propertySelector.selectedIndex.get(),
        .showIndexColumn = true,
        .visible = core_state_.sequencer.propertySelector.visible.get(),
        .dataRevision = 1,
    });
}

void StandaloneContext::setupSequencerMacroEncoderSync() {
    seq_macro_encoder_watcher_.watchAll(
        [this]() { syncSequencerMacroEncoderPositions(); },
        core_state_.activeView,
        core_state_.sequencer.page,
        core_state_.sequencer.length,
        core_state_.sequencer.focusedStep,
        core_state_.sequencer.activeStepProperty,
        core_state_.sequencer.stepDataRevision,
        core_state_.sequencer.patternConfig.visible,
        core_state_.sequencer.stepEdit.visible,
        core_state_.sequencer.propertySelector.visible
    );
}

void StandaloneContext::syncSequencerMacroEncoderPositions() {
    if (core_state_.activeView.get() != core::ui::ViewType::SEQUENCER) return;

    const uint8_t len = core_state_.sequencer.length.get();
    const uint8_t page = core_state_.sequencer.normalizePage(core_state_.sequencer.page.get());

    const auto prop = core_state_.sequencer.activeStepProperty.get();

    // Absolute + discrete steps (framework quantizes [0..1])
    const uint8_t steps = input_utils::discreteStepsForProperty(prop);

    if (seq_macro_steps_configured_ != steps) {
        for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
            encoders().setDiscreteSteps(Config::MACRO_ENCODERS[i], steps);
        }
        seq_macro_steps_configured_ = steps;
    }

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        float normalized = 0.0f;
        uint8_t abs = 0;

        if (core_state_.sequencer.resolveStepInPage(page, i, abs)) {
            normalized = input_utils::stepPropertyToNormalized(
                prop,
                core_state_.sequencer.note[abs],
                core_state_.sequencer.velocity[abs],
                core_state_.sequencer.gate[abs]
            );
        }

        if (normalized < 0.0f) normalized = 0.0f;
        if (normalized > 1.0f) normalized = 1.0f;

        encoders().setPosition(Config::MACRO_ENCODERS[i], normalized);
    }

    if (core_state_.overlays.hasVisible()) {
        seq_opt_steps_configured_ = 0;
        return;
    }

    const uint8_t focused = core_state_.sequencer.focusedStep.get();
    if (len == 0 || focused >= len || focused >= core::state::sequencer::SequencerState::MAX_STEPS) {
        return;
    }

    const uint8_t optSteps = input_utils::discreteStepsForProperty(prop);

    if (seq_opt_steps_configured_ != optSteps) {
        encoders().setDiscreteSteps(Config::EncoderID::OPT, optSteps);
        seq_opt_steps_configured_ = optSteps;
    }

    const float optPosition = input_utils::stepPropertyToNormalized(
        prop,
        core_state_.sequencer.note[focused],
        core_state_.sequencer.velocity[focused],
        core_state_.sequencer.gate[focused]
    );

    encoders().setPosition(Config::EncoderID::OPT, optPosition);
}

void StandaloneContext::setupViewSelectorRendering() {
    view_selector_watcher_.watchAll(
        [this]() { renderViewSelector(); },
        core_state_.viewSelector.visible,
        core_state_.viewSelector.selectedIndex
    );
}

void StandaloneContext::renderViewSelector() {
    if (!view_selector_) return;
    static const std::vector<std::string> VIEW_NAMES = {"Macros", "Sequencer"};

    view_selector_->render({
        .items = &VIEW_NAMES,
        .selectedIndex = core_state_.viewSelector.selectedIndex.get(),
        .visible = core_state_.viewSelector.visible.get()
    });
}

void StandaloneContext::setupActiveViewSwitching() {
    active_view_watcher_.watchAll(
        [this]() { applyActiveView(); },
        core_state_.activeView
    );
}

void StandaloneContext::applyActiveView() {
    if (macro_view_) macro_view_->onDeactivate();
    if (sequencer_view_) sequencer_view_->onDeactivate();

    switch (core_state_.activeView.get()) {
        case core::ui::ViewType::SEQUENCER:
            if (sequencer_view_) sequencer_view_->onActivate();
            syncSequencerMacroEncoderPositions();
            break;
        case core::ui::ViewType::MACRO:
        default:
            if (macro_view_) macro_view_->onActivate();
            syncEncodersFromState();
            break;
    }
}

}  // namespace core::context

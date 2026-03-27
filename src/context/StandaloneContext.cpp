#include "StandaloneContext.hpp"

#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

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
#include "handler/settings/GlobalSettingsHandler.hpp"
#include "handler/settings/DataManagerHandler.hpp"
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
#include <oc/context/OverlayManager.hpp>
#include "ui/transportbar/TransportBar.hpp"
#include "ui/transportbar/ContextSoftkeyBar.hpp"
#include "ui/view/MacroView.hpp"
#include "ui/view/SequencerView.hpp"
#include <ms/ui/ViewContainer.hpp>

#include "sequencer/SequencerPlaybackService.hpp"
#include "sequencer/MidiClockSyncService.hpp"
#include "midi/MidiUtils.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"

namespace core::context {

namespace input_utils = core::handler::sequencer::input_utils;

namespace {

constexpr float ENCODER_POSITION_EPSILON = 0.0005f;
constexpr std::array<const char*, 4> SEQUENCER_PROPERTY_SELECTOR_ITEMS = {
    "Note",
    "Velocity",
    "Gate",
    "Nudge",
};
constexpr std::array<core::state::sequencer::StepProperty, 4> SEQUENCER_STEP_EDIT_PROPERTIES = {
    core::state::sequencer::StepProperty::NOTE,
    core::state::sequencer::StepProperty::VELOCITY,
    core::state::sequencer::StepProperty::GATE,
    core::state::sequencer::StepProperty::NUDGE,
};
constexpr std::array<const char*, 4> SEQUENCER_STEP_EDIT_KEYS = {
    "Note",
    "Velocity",
    "Gate",
    "Nudge",
};

inline bool hasMeaningfulEncoderDelta(float a, float b) {
    return std::fabs(a - b) > ENCODER_POSITION_EPSILON;
}

template <typename EncoderIdT>
inline void applySequencerEncoderConfig(
    oc::api::EncoderAPI& encoders,
    EncoderIdT encoderId,
    const input_utils::StepPropertyEncoderConfig& config
) {
    encoders.setDiscreteTicksPerStep(encoderId, config.discreteTicksPerStep);
    encoders.setNormalizedTurns(encoderId, config.normalizedTurns);
    encoders.setDiscreteSteps(encoderId, config.discreteSteps);
}

template <size_t N>
void formatSequencerStepEditRows(
    std::array<std::array<char, N>, 4>& valueBuffers,
    std::array<ms::ui::KeyValueRow, 4>& rows,
    uint8_t note,
    uint8_t velocity,
    uint16_t gate,
    int8_t nudge
) {
    for (size_t i = 0; i < rows.size(); ++i) {
        core::state::sequencer::formatStepPropertyValue(
            valueBuffers[i].data(),
            valueBuffers[i].size(),
            SEQUENCER_STEP_EDIT_PROPERTIES[i],
            note,
            velocity,
            gate,
            nudge
        );
        rows[i] = {
            .key = SEQUENCER_STEP_EDIT_KEYS[i],
            .value = valueBuffers[i].data(),
        };
    }
}

}  // namespace

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
    context_softkey_bar_ = std::make_unique<core::ui::ContextSoftkeyBar>(
        view_container_->getBottomZone()
    );
    setupDataManagerSoftkeyBarRendering();
    renderDataManagerSoftkeyBar();

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

    global_settings_overlay_ = std::make_unique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::GLOBAL_SETTINGS,
        oc::ui::lvgl::scopeID(global_settings_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );
    setupGlobalSettingsRendering();

    global_settings_selector_overlay_ = std::make_unique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::GLOBAL_SETTINGS_SELECTOR,
        oc::ui::lvgl::scopeID(global_settings_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );
    setupGlobalSettingsSelectorRendering();

    data_manager_overlay_ = std::make_unique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::DATA_MANAGER,
        oc::ui::lvgl::scopeID(data_manager_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );
    setupDataManagerRendering();

    data_manager_dialog_overlay_ = std::make_unique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::DATA_MANAGER_DIALOG,
        oc::ui::lvgl::scopeID(data_manager_dialog_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );
    setupDataManagerDialogRendering();

    // Macro edit overlays (main + selectors)
    macro_edit_overlay_ = std::make_unique<ms::ui::VirtualListKeyValueOverlay>(mainZone);
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::MACRO_EDIT,
        oc::ui::lvgl::scopeID(macro_edit_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );
    setupMacroEditRendering();

    macro_edit_selector_overlay_ = std::make_unique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::MACRO_EDIT_SELECTOR,
        oc::ui::lvgl::scopeID(macro_edit_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );
    setupMacroEditSelectorRendering();

    macro_page_selector_overlay_ = std::make_unique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::PAGE_SELECTOR,
        oc::ui::lvgl::scopeID(macro_page_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );
    setupMacroPageSelectorRendering();

    macro_target_selector_overlay_ = std::make_unique<ms::ui::VirtualListSelectorOverlay>(mainZone);
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::MACRO_EDIT_MACRO_SELECTOR,
        oc::ui::lvgl::scopeID(macro_target_selector_overlay_->getElement()),
        static_cast<oc::type::ButtonID>(0)
    );
    setupMacroTargetSelectorRendering();

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

    // Global sync service (clock source arbitration + MIDI realtime IO)
    midi_clock_sync_ = std::make_unique<core::sequencer::MidiClockSyncService>(
        core_state_.midiSync,
        core_state_.statusBar,
        midi()
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
    onMidiClock([this](uint64_t timestampUs) {
        if (!midi_clock_sync_) return;
        midi_clock_sync_->onClock(timestampUs, oc::time::millis());
    });
    onMidiStart([this]() {
        if (midi_clock_sync_) midi_clock_sync_->onStart();
    });
    onMidiContinue([this]() {
        if (midi_clock_sync_) midi_clock_sync_->onContinue();
    });
    onMidiStop([this]() {
        if (midi_clock_sync_) midi_clock_sync_->onStop();
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
        OverlayCtx ctx{*overlay_controller_, nullptr, view_selector_->getElement()};
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

    global_settings_handler_ = std::make_unique<core::handler::GlobalSettingsHandler>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        global_settings_overlay_->getElement(),
        global_settings_selector_overlay_->getElement()
    );

    data_manager_handler_ = std::make_unique<core::handler::DataManagerHandler>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        core::handler::DataManagerHandler::ViewScopes{
            macro_view_->getElement(),
            sequencer_view_->getElement(),
        },
        data_manager_overlay_->getElement(),
        data_manager_dialog_overlay_->getElement()
    );

    // Create MacroEdit input handler (two-level scoping)
    macro_edit_handler_ = std::make_unique<core::handler::MacroEditHandler>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        macro_view_->getElement(),
        macro_edit_overlay_->getElement(),
        macro_edit_selector_overlay_->getElement(),
        macro_page_selector_overlay_->getElement(),
        macro_target_selector_overlay_->getElement()
    );

    // Global playback service (not tied to any view scope)
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
    const uint32_t nowMs = oc::time::millis();

    if (midi_clock_sync_) {
        midi_clock_sync_->update(nowMs);
    }

    if (sequencer_playback_ && midi_clock_sync_) {
        if (midi_clock_sync_->consumeResyncRequest()) {
            sequencer_playback_->stop();
        }
        sequencer_playback_->update(midi_clock_sync_->tick(), midi_clock_sync_->playing());
    } else if (sequencer_playback_) {
        sequencer_playback_->update(0, false);
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
    core_state_.globalSettings.reset();
    core_state_.dataManager.resetSession(core::state::DataManagerContext::MACRO);

    if (sequencer_playback_) {
        sequencer_playback_->stop();
        sequencer_playback_.reset();
    }
    midi_clock_sync_.reset();

    // Handlers first (they reference state/APIs)
    global_settings_handler_.reset();
    data_manager_handler_.reset();
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
    macro_target_selector_overlay_.reset();
    macro_page_selector_overlay_.reset();
    macro_edit_selector_overlay_.reset();
    macro_edit_overlay_.reset();
    seq_pattern_config_overlay_.reset();
    seq_step_edit_overlay_.reset();
    seq_property_selector_overlay_.reset();
    global_settings_selector_overlay_.reset();
    global_settings_overlay_.reset();
    data_manager_dialog_overlay_.reset();
    data_manager_overlay_.reset();
    view_selector_.reset();

    // Overlay controller (clears authority resolver)
    overlay_controller_.reset();

    // TransportBar (TopBar is now managed by MacroView)
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

// =============================================================================
// Private Methods
// =============================================================================

void StandaloneContext::syncEncodersFromState() {
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
    resetSequencerEncoderSyncCache();

    OC_LOG_DEBUG("Synced encoder positions from restored state");
}

void StandaloneContext::resetSequencerEncoderSyncCache() {
    seq_macro_steps_configured_ = 0;
    seq_opt_steps_configured_ = 0;
    seq_macro_ticks_per_step_configured_ = 0;
    seq_opt_ticks_per_step_configured_ = 0;
    seq_macro_turns_configured_ = 0.0f;
    seq_opt_turns_configured_ = 0.0f;
    seq_macro_position_valid_.fill(false);
    seq_opt_position_valid_ = false;
}

void StandaloneContext::resetSequencerOptEncoderSyncCache() {
    seq_opt_steps_configured_ = 0;
    seq_opt_ticks_per_step_configured_ = 0;
    seq_opt_turns_configured_ = 0.0f;
    seq_opt_position_valid_ = false;
}

void StandaloneContext::ensureSequencerMacroEncoderConfig(
    const input_utils::StepPropertyEncoderConfig& config
) {
    if (seq_macro_steps_configured_ == config.discreteSteps &&
        seq_macro_ticks_per_step_configured_ == config.discreteTicksPerStep &&
        !hasMeaningfulEncoderDelta(seq_macro_turns_configured_, config.normalizedTurns)) {
        return;
    }

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        applySequencerEncoderConfig(encoders(), Config::MACRO_ENCODERS[i], config);
    }

    seq_macro_steps_configured_ = config.discreteSteps;
    seq_macro_ticks_per_step_configured_ = config.discreteTicksPerStep;
    seq_macro_turns_configured_ = config.normalizedTurns;
}

void StandaloneContext::syncSequencerMacroEncoderValues(
    uint8_t page,
    core::state::sequencer::StepProperty property
) {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        float normalized = 0.0f;
        uint8_t abs = 0;

        if (core_state_.sequencer.resolveStepInPage(page, i, abs)) {
            normalized = input_utils::stepPropertyToNormalized(core_state_.sequencer, abs, property);
        }

        normalized = input_utils::clampNormalized(normalized);

        if (!seq_macro_position_valid_[i] ||
            hasMeaningfulEncoderDelta(seq_macro_position_cache_[i], normalized)) {
            encoders().setPosition(Config::MACRO_ENCODERS[i], normalized);
            seq_macro_position_cache_[i] = normalized;
            seq_macro_position_valid_[i] = true;
        }
    }
}

void StandaloneContext::ensureSequencerOptEncoderConfig(
    const input_utils::StepPropertyEncoderConfig& config
) {
    if (seq_opt_steps_configured_ == config.discreteSteps &&
        seq_opt_ticks_per_step_configured_ == config.discreteTicksPerStep &&
        !hasMeaningfulEncoderDelta(seq_opt_turns_configured_, config.normalizedTurns)) {
        return;
    }

    applySequencerEncoderConfig(encoders(), Config::EncoderID::OPT, config);
    seq_opt_steps_configured_ = config.discreteSteps;
    seq_opt_ticks_per_step_configured_ = config.discreteTicksPerStep;
    seq_opt_turns_configured_ = config.normalizedTurns;
}

void StandaloneContext::syncSequencerOptEncoderValue(
    uint8_t length,
    uint8_t focusedStep,
    core::state::sequencer::StepProperty property
) {
    if (length == 0 ||
        focusedStep >= length ||
        focusedStep >= core::state::sequencer::SequencerState::MAX_STEPS) {
        return;
    }

    const float optPosition =
        input_utils::stepPropertyToNormalized(core_state_.sequencer, focusedStep, property);

    if (!seq_opt_position_valid_ || hasMeaningfulEncoderDelta(seq_opt_position_cache_, optPosition)) {
        encoders().setPosition(Config::EncoderID::OPT, optPosition);
        seq_opt_position_cache_ = optPosition;
        seq_opt_position_valid_ = true;
    }
}

void StandaloneContext::setupMacroEditRendering() {
    macro_edit_watcher_.watchAll(
        [this]() { renderMacroEdit(); },
        core_state_.macroEdit.visible,
        core_state_.macroEdit.editingIndex,
        core_state_.macroEdit.tempChannel,
        core_state_.macroEdit.tempCC,
        core_state_.macroEdit.focusedRow,
        core_state_.configRevision
    );
}

void StandaloneContext::setupMacroEditSelectorRendering() {
    macro_edit_selector_watcher_.watchAll(
        [this]() { renderMacroEditSelector(); },
        core_state_.macroEdit.selector.visible,
        core_state_.macroEdit.selector.editingRow,
        core_state_.macroEdit.selector.selectedIndex
    );
}

void StandaloneContext::setupMacroPageSelectorRendering() {
    macro_page_selector_watcher_.watchAll(
        [this]() { renderMacroPageSelector(); },
        core_state_.pages.selector.visible,
        core_state_.pages.selector.selectedIndex,
        core_state_.configRevision
    );
}

void StandaloneContext::setupMacroTargetSelectorRendering() {
    macro_target_selector_watcher_.watchAll(
        [this]() { renderMacroTargetSelector(); },
        core_state_.macroEdit.macroSelector.visible,
        core_state_.macroEdit.macroSelector.selectedIndex
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
    const int8_t nudge = core_state_.sequencer.nudge[abs];

    const uint32_t dataRevision =
        core_state_.sequencer.stepDataRevision.get() ^
        (static_cast<uint32_t>(abs) << 16) ^
        (static_cast<uint32_t>(len) << 24);

    std::array<std::array<char, 12>, 4> valueBuffers{};
    std::array<ms::ui::KeyValueRow, 4> rows{};
    formatSequencerStepEditRows(valueBuffers, rows, note, vel, gate, nudge);

    seq_step_edit_overlay_->render({
        .title = title,
        .meta = meta,
        .rows = rows.data(),
        .rowCount = static_cast<int>(rows.size()),
        .selectedIndex = core_state_.sequencer.stepEdit.focusedRow.get(),
        .visible = true,
        .dataRevision = dataRevision,
    });
}

void StandaloneContext::renderMacroEdit() {
    if (!macro_edit_overlay_) return;

    const bool visible = core_state_.macroEdit.visible.get();
    if (!visible) {
        macro_edit_overlay_->render({.visible = false});
        return;
    }

    if (core_state_.macroEdit.pendingOpenReleaseDecision && core_state_.macroEdit.openedAtMs == 0) {
        core_state_.macroEdit.openedAtMs = oc::time::millis();
    }

    const uint8_t macroIndex = core_state_.macroEdit.editingIndex.get();
    const uint8_t channel0 = core_state_.macroEdit.tempChannel.get();
    const uint8_t cc = core_state_.macroEdit.tempCC.get();

    char title[16];
    snprintf(title, sizeof(title), "MACRO %u", static_cast<unsigned>(macroIndex) + 1U);

    const unsigned page1 = static_cast<unsigned>(core_state_.pages.activePage) + 1U;

    char meta[16];
    snprintf(meta, sizeof(meta), "PAGE %u", page1);

    char channelStr[8];
    snprintf(channelStr, sizeof(channelStr), "%u", static_cast<unsigned>(channel0) + 1U);

    char ccStr[8];
    snprintf(ccStr, sizeof(ccStr), "%u", static_cast<unsigned>(cc));

    const ms::ui::KeyValueRow rows[] = {
        {.key = "Channel", .value = channelStr},
        {.key = "CC", .value = ccStr},
    };

    const uint32_t dataRevision =
        (static_cast<uint32_t>(macroIndex) << 24) |
        (static_cast<uint32_t>(channel0) << 16) |
        (static_cast<uint32_t>(cc) << 8) |
        (static_cast<uint32_t>(core_state_.pages.activePage & 0x0F) << 4) |
        static_cast<uint32_t>(core_state_.macroEdit.focusedRow.get() & 0x0F);

    macro_edit_overlay_->render({
        .title = title,
        .meta = meta,
        .rows = rows,
        .rowCount = 2,
        .selectedIndex = core_state_.macroEdit.focusedRow.get(),
        .visible = true,
        .dataRevision = dataRevision,
    });
}

void StandaloneContext::renderMacroEditSelector() {
    if (!macro_edit_selector_overlay_) return;

    const auto& selector = core_state_.macroEdit.selector;
    if (!selector.visible.get()) {
        macro_edit_selector_overlay_->render({.visible = false});
        return;
    }

    static bool labelsInitialized = false;
    static char channelLabels[16][4]{};
    static const char* channelItems[16]{};
    static char ccLabels[128][4]{};
    static const char* ccItems[128]{};

    if (!labelsInitialized) {
        for (int i = 0; i < 16; ++i) {
            snprintf(channelLabels[i], sizeof(channelLabels[i]), "%d", i + 1);
            channelItems[i] = channelLabels[i];
        }
        for (int i = 0; i < 128; ++i) {
            snprintf(ccLabels[i], sizeof(ccLabels[i]), "%d", i);
            ccItems[i] = ccLabels[i];
        }
        labelsInitialized = true;
    }

    const uint8_t row = selector.editingRow.get();
    const bool isChannel = (row == 0);
    const int itemCount = isChannel ? 16 : 128;
    const char* const* items = isChannel ? channelItems : ccItems;
    const int selected = std::clamp(selector.selectedIndex.get(), 0, itemCount - 1);

    const char* meta = isChannel ? "CHANNEL" : "CC";

    macro_edit_selector_overlay_->render({
        .title = "VALUE",
        .meta = meta,
        .items = items,
        .itemCount = itemCount,
        .selectedIndex = selected,
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = static_cast<uint32_t>(row + 1U),
    });
}

void StandaloneContext::renderMacroPageSelector() {
    if (!macro_page_selector_overlay_) return;

    if (!core_state_.pages.selector.visible.get()) {
        macro_page_selector_overlay_->render({.visible = false});
        return;
    }

    std::array<const char*, core::state::macro::PAGE_COUNT> pageItems{};
    for (uint8_t i = 0; i < core::state::macro::PAGE_COUNT; ++i) {
        pageItems[i] = core_state_.pages.pageName(i);
    }

    const int selected = std::clamp(
        static_cast<int>(core_state_.pages.selector.selectedIndex.get()),
        0,
        static_cast<int>(core::state::macro::PAGE_COUNT) - 1
    );

    macro_page_selector_overlay_->render({
        .title = "PAGE",
        .meta = "MACRO",
        .items = pageItems.data(),
        .itemCount = core::state::macro::PAGE_COUNT,
        .selectedIndex = selected,
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = core_state_.configRevision.get(),
    });
}

void StandaloneContext::renderMacroTargetSelector() {
    if (!macro_target_selector_overlay_) return;

    if (!core_state_.macroEdit.macroSelector.visible.get()) {
        macro_target_selector_overlay_->render({.visible = false});
        return;
    }

    static bool labelsInitialized = false;
    static char macroLabels[core::state::MACRO_COUNT][16]{};
    static const char* macroItems[core::state::MACRO_COUNT]{};

    if (!labelsInitialized) {
        for (uint8_t i = 0; i < core::state::MACRO_COUNT; ++i) {
            snprintf(macroLabels[i], sizeof(macroLabels[i]), "Macro %u", static_cast<unsigned>(i) + 1U);
            macroItems[i] = macroLabels[i];
        }
        labelsInitialized = true;
    }

    const int selected = std::clamp(
        core_state_.macroEdit.macroSelector.selectedIndex.get(),
        0,
        static_cast<int>(core::state::MACRO_COUNT) - 1
    );

    const char* meta = "TARGET";

    macro_target_selector_overlay_->render({
        .title = "MACRO",
        .meta = meta,
        .items = macroItems,
        .itemCount = core::state::MACRO_COUNT,
        .selectedIndex = selected,
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = 1,
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

    seq_property_selector_overlay_->render({
        .title = "PROPERTY",
        .meta = "MACROS",
        .items = SEQUENCER_PROPERTY_SELECTOR_ITEMS.data(),
        .itemCount = static_cast<int>(SEQUENCER_PROPERTY_SELECTOR_ITEMS.size()),
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
        core_state_.sequencer.patternConfig.visible,
        core_state_.sequencer.stepEdit.visible,
        core_state_.sequencer.propertySelector.visible
    );
}

void StandaloneContext::syncSequencerMacroEncoderPositions() {
    if (core_state_.activeView.get() != core::ui::ViewType::SEQUENCER) return;

    if (core_state_.overlays.hasVisible()) {
        // Overlay scope owns controls; defer expensive knob position sync
        // until overlay closes (visibility change triggers this watcher).
        resetSequencerOptEncoderSyncCache();
        return;
    }

    const uint8_t len = core_state_.sequencer.length.get();
    const uint8_t page = core_state_.sequencer.normalizePage(core_state_.sequencer.page.get());
    const auto prop = core_state_.sequencer.activeStepProperty.get();
    const auto config = input_utils::encoderConfigForProperty(prop);
    ensureSequencerMacroEncoderConfig(config);
    syncSequencerMacroEncoderValues(page, prop);
    ensureSequencerOptEncoderConfig(config);
    syncSequencerOptEncoderValue(len, core_state_.sequencer.focusedStep.get(), prop);
}

void StandaloneContext::setupViewSelectorRendering() {
    view_selector_watcher_.watchAll(
        [this]() { renderViewSelector(); },
        core_state_.viewSelector.visible,
        core_state_.viewSelector.selectedIndex
    );
}

void StandaloneContext::setupGlobalSettingsRendering() {
    global_settings_watcher_.watchAll(
        [this]() { renderGlobalSettings(); },
        core_state_.globalSettings.visible,
        core_state_.globalSettings.focusedRow,
        core_state_.midiSync.mode,
        core_state_.midiSync.followTransport,
        core_state_.midiSync.autoFallbackMs,
        core_state_.midiSync.autoLockClockCount,
        core_state_.midiSync.activeSource,
        core_state_.midiSync.externalClockPresent
    );
}

void StandaloneContext::renderGlobalSettings() {
    if (!global_settings_overlay_) return;

    const bool visible = core_state_.globalSettings.visible.get();
    if (!visible) {
        global_settings_overlay_->render({.visible = false});
        return;
    }

    const auto mode = core_state_.midiSync.mode.get();
    const bool followTransport = core_state_.midiSync.followTransport.get();
    const uint16_t fallbackMs = core_state_.midiSync.autoFallbackMs.get();
    const uint8_t lockCount = core_state_.midiSync.autoLockClockCount.get();

    const char* modeLabel = "AUTO";
    switch (mode) {
        case core::state::MidiSyncMode::MASTER: modeLabel = "MASTER"; break;
        case core::state::MidiSyncMode::SLAVE: modeLabel = "SLAVE"; break;
        case core::state::MidiSyncMode::AUTO:
        default:
            modeLabel = "AUTO";
            break;
    }

    const char* sourceLabel =
        (core_state_.midiSync.activeSource.get() == core::state::ClockSourceActive::EXTERNAL) ? "EXT" : "INT";
    const char* signalLabel = core_state_.midiSync.externalClockPresent.get() ? "IN" : "-";

    char meta[24];
    snprintf(meta, sizeof(meta), "%s  CLK %s", sourceLabel, signalLabel);

    char fallbackStr[16];
    snprintf(fallbackStr, sizeof(fallbackStr), "%ums", static_cast<unsigned>(fallbackMs));

    char lockStr[16];
    snprintf(lockStr, sizeof(lockStr), "%u clocks", static_cast<unsigned>(lockCount));

    const ms::ui::KeyValueRow rows[] = {
        {.key = "Mode", .value = modeLabel},
        {.key = "Follow", .value = followTransport ? "ON" : "OFF"},
        {.key = "Timeout", .value = fallbackStr},
        {.key = "Lock", .value = lockStr},
    };

    const uint32_t dataRevision =
        (static_cast<uint32_t>(mode) << 24) |
        (static_cast<uint32_t>(followTransport ? 1 : 0) << 20) |
        (static_cast<uint32_t>(fallbackMs) << 4) |
        (static_cast<uint32_t>(lockCount) & 0x0F);

    global_settings_overlay_->render({
        .title = "SETTINGS",
        .meta = meta,
        .rows = rows,
        .rowCount = 4,
        .selectedIndex = core_state_.globalSettings.focusedRow.get(),
        .visible = true,
        .dataRevision = dataRevision,
    });
}

void StandaloneContext::setupGlobalSettingsSelectorRendering() {
    global_settings_selector_watcher_.watchAll(
        [this]() { renderGlobalSettingsSelector(); },
        core_state_.globalSettings.selector.visible,
        core_state_.globalSettings.selector.selectedIndex,
        core_state_.globalSettings.selector.editingRow,
        core_state_.midiSync.mode,
        core_state_.midiSync.followTransport,
        core_state_.midiSync.autoFallbackMs,
        core_state_.midiSync.autoLockClockCount
    );
}

void StandaloneContext::renderGlobalSettingsSelector() {
    if (!global_settings_selector_overlay_) return;

    const bool visible = core_state_.globalSettings.selector.visible.get();
    if (!visible) {
        global_settings_selector_overlay_->render({.visible = false});
        return;
    }

    static const char* const MODE_ITEMS[] = {"MASTER", "SLAVE", "AUTO"};
    static const char* const FOLLOW_ITEMS[] = {"OFF", "ON"};
    static const char* const FALLBACK_ITEMS[] = {"150 ms", "250 ms", "500 ms", "750 ms", "1000 ms", "1500 ms", "2000 ms"};
    static const char* const LOCK_ITEMS[] = {"1", "2", "3", "4", "6", "8", "12", "24"};

    const uint8_t row = core_state_.globalSettings.selector.editingRow.get();
    const char* title = "VALUE";
    const char* meta = "GLOBAL";
    const char* const* items = MODE_ITEMS;
    int itemCount = 3;

    switch (row) {
        case 0:
            title = "SYNC MODE";
            items = MODE_ITEMS;
            itemCount = 3;
            break;
        case 1:
            title = "FOLLOW";
            items = FOLLOW_ITEMS;
            itemCount = 2;
            break;
        case 2:
            title = "AUTO TIMEOUT";
            items = FALLBACK_ITEMS;
            itemCount = 7;
            break;
        case 3:
            title = "AUTO LOCK";
            items = LOCK_ITEMS;
            itemCount = 8;
            break;
        default:
            break;
    }

    const uint32_t dataRevision =
        (static_cast<uint32_t>(row) << 24) |
        (static_cast<uint32_t>(core_state_.midiSync.mode.get()) << 16) |
        (static_cast<uint32_t>(core_state_.midiSync.followTransport.get() ? 1 : 0) << 12) |
        (static_cast<uint32_t>(core_state_.midiSync.autoFallbackMs.get()) & 0x0FFF);

    global_settings_selector_overlay_->render({
        .title = title,
        .meta = meta,
        .items = items,
        .itemCount = itemCount,
        .selectedIndex = core_state_.globalSettings.selector.selectedIndex.get(),
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = dataRevision,
    });
}

void StandaloneContext::setupDataManagerRendering() {
    data_manager_watcher_.watchAll(
        [this]() { renderDataManager(); },
        core_state_.dataManager.visible,
        core_state_.dataManager.focusedRow,
        core_state_.dataManager.context,
        core_state_.dataManager.macroShortcutLeft,
        core_state_.dataManager.macroShortcutRight,
        core_state_.dataManager.seqShortcutLeft,
        core_state_.dataManager.seqShortcutRight,
        core_state_.dataManager.feedback
    );
}

void StandaloneContext::renderDataManager() {
    if (!data_manager_overlay_) return;

    const auto& dm = core_state_.dataManager;
    if (!dm.visible.get()) {
        data_manager_overlay_->render({.visible = false});
        return;
    }

    const bool macroContext = dm.context.get() == core::state::DataManagerContext::MACRO;
    const char* title = macroContext ? "MACRO TOOLS" : "SEQUENCER TOOLS";

    const core::state::DataManagerCommand leftCommand = dm.shortcutForRow(0U);
    const core::state::DataManagerCommand rightCommand = dm.shortcutForRow(1U);

    const ms::ui::KeyValueRow rows[] = {
        {.key = "Bottom Left", .value = core::state::dataManagerCommandLabel(leftCommand)},
        {.key = "Bottom Right", .value = core::state::dataManagerCommandLabel(rightCommand)},
    };

    const uint8_t rowCount = 2U;
    const uint8_t selected = std::min<uint8_t>(dm.focusedRow.get(), 1U);

    const char* feedback = dm.feedback.get();
    const char* meta = (feedback && feedback[0] != '\0')
                           ? feedback
                           : "NAV=MAP  L/R=RUN  C=ALL";

    uint32_t feedbackHash = 0;
    if (feedback) {
        for (const char* p = feedback; *p; ++p) {
            feedbackHash = (feedbackHash * 131U) + static_cast<uint8_t>(*p);
        }
    }

    const uint32_t dataRevision =
        (static_cast<uint32_t>(dm.context.get()) << 24) |
        (static_cast<uint32_t>(leftCommand) << 16) |
        (static_cast<uint32_t>(rightCommand) << 8) |
        (feedbackHash & 0xFFU);

    data_manager_overlay_->render({
        .title = title,
        .meta = meta,
        .rows = rows,
        .rowCount = rowCount,
        .selectedIndex = selected,
        .visible = true,
        .dataRevision = dataRevision,
    });
}

void StandaloneContext::setupDataManagerDialogRendering() {
    data_manager_dialog_watcher_.watchAll(
        [this]() { renderDataManagerDialog(); },
        core_state_.dataManager.dialog.visible,
        core_state_.dataManager.dialog.mode,
        core_state_.dataManager.dialog.selectedIndex,
        core_state_.dataManager.dialog.editingShortcutRow,
        core_state_.dataManager.context,
        core_state_.dataManager.pendingCommand,
        core_state_.dataManager.pendingSlot,
        core_state_.dataManager.pendingSetLoadMode
    );
}

void StandaloneContext::renderDataManagerDialog() {
    if (!data_manager_dialog_overlay_) return;

    const auto& dm = core_state_.dataManager;
    const auto& dialog = dm.dialog;

    if (!dialog.visible.get()) {
        data_manager_dialog_overlay_->render({.visible = false});
        return;
    }

    static const char* const SET_MODE_ITEMS[] = {"REPLACE", "MERGE"};
    static const char* const CONFIRM_ITEMS[] = {"CANCEL", "CONFIRM"};

    const auto context = dm.context.get();
    const int commandCount = static_cast<int>(core::state::dataManagerCommandCount(context));
    for (int i = 0; i < commandCount; ++i) {
        data_manager_dialog_command_items_[i] = core::state::dataManagerCommandLabel(
            core::state::dataManagerCommandAt(context, i)
        );
    }

    const auto mode = dialog.mode.get();
    const char* title = "COMMAND";
    const char* meta = "";
    const char* const* items = nullptr;
    int itemCount = 0;
    int selected = 0;

    if (mode == core::state::DataManagerDialogMode::ASSIGN_SHORTCUT) {
        title = (dialog.editingShortcutRow.get() == 0) ? "MAP LEFT" : "MAP RIGHT";
        meta = "SELECT COMMAND";
        items = data_manager_dialog_command_items_.data();
        itemCount = commandCount;
        selected = std::clamp(dialog.selectedIndex.get(), 0, itemCount - 1);
    } else if (mode == core::state::DataManagerDialogMode::COMMAND_PALETTE) {
        title = "COMMANDS";
        meta = "RUN COMMAND";
        items = data_manager_dialog_command_items_.data();
        itemCount = commandCount;
        selected = std::clamp(dialog.selectedIndex.get(), 0, itemCount - 1);
    } else if (mode == core::state::DataManagerDialogMode::SLOT_PICKER) {
        title = core::state::dataManagerCommandLabel(dm.pendingCommand.get());
        meta = "SELECT SLOT";
        const uint8_t slotCount = core_state_.dataManagerSlotCount(dm.pendingCommand.get());

        itemCount = static_cast<int>(slotCount);
        if (itemCount <= 0) {
            data_manager_dialog_overlay_->render({.visible = false});
            return;
        }

        const char slotTag = core::state::dataManagerCommandSlotTag(dm.pendingCommand.get());
        const char safeSlotTag = (slotTag == '\0') ? 'S' : slotTag;
        for (int i = 0; i < itemCount; ++i) {
            std::snprintf(data_manager_dialog_slot_labels_[i].data(),
                          data_manager_dialog_slot_labels_[i].size(),
                          "%c%02d",
                          safeSlotTag,
                          i + 1);
            data_manager_dialog_slot_items_[i] = data_manager_dialog_slot_labels_[i].data();
        }

        items = data_manager_dialog_slot_items_.data();
        selected = std::clamp(dialog.selectedIndex.get(), 0, itemCount - 1);
    } else if (mode == core::state::DataManagerDialogMode::SET_LOAD_MODE) {
        title = "LOAD SET";
        meta = "MODE";
        items = SET_MODE_ITEMS;
        itemCount = 2;
        selected = std::clamp(dialog.selectedIndex.get(), 0, 1);
    } else if (mode == core::state::DataManagerDialogMode::CONFIRM) {
        title = "CONFIRM";
        const auto cmd = dm.pendingCommand.get();
        const char slotTag = core::state::dataManagerCommandSlotTag(cmd);
        const char safeSlotTag = (slotTag == '\0') ? 'S' : slotTag;
        static char confirmMeta[24];
        if (core::state::dataManagerCommandIsErase(cmd)) {
            std::snprintf(confirmMeta, sizeof(confirmMeta), "ERASE %c%02u ?",
                          safeSlotTag,
                          static_cast<unsigned>(dm.pendingSlot.get() + 1U));
        } else {
            std::snprintf(confirmMeta, sizeof(confirmMeta), "OVERWRITE %c%02u ?",
                          safeSlotTag,
                          static_cast<unsigned>(dm.pendingSlot.get() + 1U));
        }
        meta = confirmMeta;
        items = CONFIRM_ITEMS;
        itemCount = 2;
        selected = std::clamp(dialog.selectedIndex.get(), 0, 1);
    }

    if (!items || itemCount <= 0) {
        data_manager_dialog_overlay_->render({.visible = false});
        return;
    }

    const uint32_t dataRevision =
        (static_cast<uint32_t>(mode) << 24) |
        (static_cast<uint32_t>(selected) << 16) |
        (static_cast<uint32_t>(dm.pendingCommand.get()) << 8) |
        static_cast<uint32_t>(dm.pendingSlot.get());

    data_manager_dialog_overlay_->render({
        .title = title,
        .meta = meta,
        .items = items,
        .itemCount = itemCount,
        .selectedIndex = selected,
        .showIndexColumn = false,
        .visible = true,
        .dataRevision = dataRevision,
    });
}

void StandaloneContext::setupDataManagerSoftkeyBarRendering() {
    data_manager_softkey_bar_watcher_.watchAll(
        [this]() { renderDataManagerSoftkeyBar(); },
        core_state_.dataManager.visible,
        core_state_.dataManager.context,
        core_state_.dataManager.macroShortcutLeft,
        core_state_.dataManager.macroShortcutRight,
        core_state_.dataManager.seqShortcutLeft,
        core_state_.dataManager.seqShortcutRight
    );
}

void StandaloneContext::renderDataManagerSoftkeyBar() {
    if (!context_softkey_bar_) return;

    const bool visible = core_state_.dataManager.visible.get();
    if (!visible) {
        context_softkey_bar_->hide();
        if (transport_bar_) transport_bar_->show();
        return;
    }

    const auto left = core_state_.dataManager.shortcutForRow(0U);
    const auto right = core_state_.dataManager.shortcutForRow(1U);

    char leftLabel[24];
    std::snprintf(leftLabel, sizeof(leftLabel), "L:%s", core::state::dataManagerCommandLabel(left));

    char rightLabel[24];
    std::snprintf(rightLabel, sizeof(rightLabel), "R:%s", core::state::dataManagerCommandLabel(right));

    context_softkey_bar_->setLabels(leftLabel, "C:Commands", rightLabel);
    context_softkey_bar_->show();
    if (transport_bar_) transport_bar_->hide();
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

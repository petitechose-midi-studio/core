#include "StandaloneContext.hpp"

#include <lvgl.h>

#include <oc/log/Log.hpp>
#include <oc/ui/lvgl/FontLoader.hpp>

#include <config/App.hpp>
#include "handler/macro/MacroEditHandler.hpp"
#include "handler/macro/MacroMidiHandler.hpp"
#include "handler/macro/MacroValueHandler.hpp"
#include "handler/transport/TransportHandler.hpp"
#include "ui/font/CoreFonts.hpp"
#include "ui/font/StandaloneFonts.hpp"
#include "ui/macro/MacroEditOverlay.hpp"
#include "state/OverlayManager.hpp"
#include "ui/transportbar/TransportBar.hpp"
#include "ui/view/MacroView.hpp"
#include "ui/ViewContainer.hpp"

namespace core::context {

// Constructor and destructor must be in .cpp where handler types are complete
StandaloneContext::StandaloneContext(core::state::CoreState& state) : core_state_(state) {}

StandaloneContext::~StandaloneContext() = default;

// =============================================================================
// IContext Lifecycle
// =============================================================================

bool StandaloneContext::initialize() {
    OC_LOG_INFO("StandaloneContext::initialize()");

    oc::ui::lvgl::font::load(CORE_FONT_ENTRIES, CORE_FONT_COUNT);
    oc::ui::lvgl::font::load(STANDALONE_FONT_ENTRIES, STANDALONE_FONT_COUNT);
    linkCoreFontAliases();

    // Sync encoder positions with restored values BEFORE creating handlers
    syncEncodersFromState();

    // Create UI container with zones
    view_container_ = std::make_unique<core::ui::ViewContainer>(lv_screen_active());

    // Create MacroView in main zone (TopBar is now internal to MacroView)
    view_ = std::make_unique<core::ui::MacroView>(
        view_container_->getMainZone(),
        core_state_
    );
    view_->onActivate();

    // Create TransportBar in bottom zone
    transport_bar_ = std::make_unique<core::ui::TransportBar>(
        view_container_->getBottomZone(),
        core_state_.statusBar
    );

    // Create overlay manager with AuthorityResolver
    overlay_controller_ = std::make_unique<core::state::OverlayManager<core::ui::OverlayType>>(
        core_state_.overlays, buttons()
    );
    buttons().setAuthorityResolver(&overlay_controller_->authority());

    // Create MacroEdit overlay (parented to MacroView)
    macro_edit_overlay_ = std::make_unique<core::ui::MacroEditOverlay>(view_->getElement());

    // Register overlay cleanup
    overlay_controller_->registerCleanup(
        core::ui::OverlayType::MACRO_EDIT,
        reinterpret_cast<oc::core::ScopeID>(macro_edit_overlay_->getElement()),
        static_cast<oc::hal::ButtonID>(0)  // No latch button
    );

    // Setup rendering subscriptions for MacroEditOverlay (orchestrator pattern)
    setupMacroEditRendering();

    // Create handlers (bindings scoped to view element)
    input_handler_ = std::make_unique<core::handler::MacroValueHandler>(
        core_state_, encoders(), midi(), view_->getElement()
    );
    midi_handler_ = std::make_unique<core::handler::MacroMidiHandler>(
        core_state_, midi(), encoders()
    );
    transport_handler_ = std::make_unique<core::handler::TransportHandler>(
        core_state_, encoders(), buttons(), view_->getElement()
    );

    // Create MacroEdit input handler (two-level scoping)
    macro_edit_handler_ = std::make_unique<core::handler::MacroEditHandler>(
        core_state_,
        *overlay_controller_,
        encoders(),
        buttons(),
        view_->getElement(),              // MacroView scope (open trigger)
        macro_edit_overlay_->getElement()   // Overlay scope (edit/close)
    );

    view_container_->show();

    OC_LOG_INFO("StandaloneContext ready");
    return true;
}

void StandaloneContext::update() {
    // Handlers and views are self-updating via Signal subscriptions
}

void StandaloneContext::cleanup() {
    // Handlers first (they reference state/APIs)
    macro_edit_handler_.reset();
    transport_handler_.reset();
    input_handler_.reset();
    midi_handler_.reset();

    // SignalWatcher cleanup is automatic (RAII)

    // Overlay UI
    macro_edit_overlay_.reset();

    // Overlay controller (clears authority resolver)
    overlay_controller_.reset();

    // TransportBar (TopBar is now managed by MacroView)
    transport_bar_.reset();

    if (view_) {
        view_->onDeactivate();
        view_.reset();
    }

    view_container_.reset();
}

// =============================================================================
// Private Methods
// =============================================================================

void StandaloneContext::syncEncodersFromState() {
    for (uint8_t i = 0; i < core::state::MACRO_COUNT; ++i) {
        float value = core_state_.macros.slots[i].value.get();
        encoders().setPosition(Config::MACRO_ENCODERS[i], value);
    }
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

void StandaloneContext::renderMacroEdit() {
    macro_edit_overlay_->render({
        .editingIndex = core_state_.macroEdit.editingIndex.get(),
        .channel = core_state_.macroEdit.tempChannel.get(),
        .cc = core_state_.macroEdit.tempCC.get(),
        .focusedRow = core_state_.macroEdit.focusedRow.get(),
        .visible = core_state_.macroEdit.visible.get()
    });
}

}  // namespace core::context

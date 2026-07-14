#include "handler/sequencer/SequencerCcLaneHandler.hpp"

#include <cmath>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>
#include <oc/time/Time.hpp>

#include "handler/sequencer/SequencerInputUtils.hpp"
#include "handler/sequencer/SequencerPropertySelectorHandler.hpp"

namespace core::handler {

namespace seq = core::state::sequencer;
using ButtonID = Config::ButtonID;
using EncoderID = Config::EncoderID;

namespace {

constexpr float DIRECTIONAL_OPT_CENTER = 0.5f;
constexpr float DIRECTIONAL_OPT_EPSILON = 0.0005f;

FLASHMEM float directionalDeltaFromNormalized(float normalized) {
    if (!std::isfinite(normalized)) return 0.0f;
    if (normalized > DIRECTIONAL_OPT_CENTER + DIRECTIONAL_OPT_EPSILON) return 1.0f;
    if (normalized < DIRECTIONAL_OPT_CENTER - DIRECTIONAL_OPT_EPSILON) return -1.0f;
    return 0.0f;
}

}  // namespace

FLASHMEM SequencerCcLaneHandler::SequencerCcLaneHandler(
    seq::SequencerState& sequencer,
    SequencerCcLaneWorkflow& workflow,
    SequencerPropertySelectorHandler& propertySelector,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID overlayScope,
    NowProvider nowProvider
)
    : sequencer_(sequencer)
    , workflow_(workflow)
    , property_selector_(propertySelector)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , overlay_scope_(overlayScope)
    , now_provider_(nowProvider ? nowProvider : oc::time::millis) {
    setupBindings();
}

FLASHMEM void SequencerCcLaneHandler::setupBindings() {
    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(overlay_scope_)
        .then([this](float delta) { onNavTurn(delta); });

    encoders_.encoder(EncoderID::OPT)
        .turn()
        .scope(overlay_scope_)
        .then([this](float delta) { onOptTurn(delta); });

    buttons_.button(ButtonID::NAV)
        .release()
        .scope(overlay_scope_)
        .then([this]() { onNavTap(); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .press()
        .scope(overlay_scope_)
        .then([this]() { onActionPress(seq::SequencerCcLaneActionSlot::BOTTOM_LEFT); });
    buttons_.button(ButtonID::BOTTOM_LEFT)
        .release()
        .scope(overlay_scope_)
        .then([this]() { onActionRelease(seq::SequencerCcLaneActionSlot::BOTTOM_LEFT); });

    buttons_.button(ButtonID::BOTTOM_CENTER)
        .release()
        .scope(overlay_scope_)
        .then([this]() { onActionRelease(seq::SequencerCcLaneActionSlot::BOTTOM_CENTER); });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .press()
        .scope(overlay_scope_)
        .then([this]() { onActionPress(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT); });
    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(overlay_scope_)
        .then([this]() { onActionRelease(seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(overlay_scope_)
        .then([this]() { back(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(overlay_scope_)
        .when([this]() {
            return sequencer_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID;
        })
        .then([this]() { openPropertyGrammar(); });
}

void SequencerCcLaneHandler::update(uint32_t nowMs) {
    syncOptEncoderContract();
    workflow_.update(nowMs);
}

FLASHMEM void SequencerCcLaneHandler::syncOptEncoderContract() {
    const bool ownsOpt = overlays_.isCurrent(core::ui::OverlayType::SEQ_CC_LANE);
    if (!ownsOpt) {
        opt_directional_configured_ = false;
        return;
    }
    if (opt_directional_configured_) return;

    configureDirectionalOpt();
    opt_directional_configured_ = true;
}

FLASHMEM void SequencerCcLaneHandler::configureDirectionalOpt() {
    encoders_.setMode(EncoderID::OPT, oc::interface::EncoderMode::NORMALIZED);
    encoders_.setBounds(EncoderID::OPT, 0.0f, 1.0f);
    encoders_.setDiscreteTicksPerStep(
        EncoderID::OPT,
        sequencer::input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP
    );
    encoders_.setNormalizedTurns(
        EncoderID::OPT,
        sequencer::input_utils::DEFAULT_NORMALIZED_TURNS
    );
    encoders_.setContinuous(EncoderID::OPT);
    recenterDirectionalOpt();
}

FLASHMEM void SequencerCcLaneHandler::recenterDirectionalOpt() {
    encoders_.setPosition(EncoderID::OPT, DIRECTIONAL_OPT_CENTER);
}

FLASHMEM uint32_t SequencerCcLaneHandler::now() const {
    return now_provider_ ? now_provider_() : 0;
}

FLASHMEM void SequencerCcLaneHandler::onNavTurn(float delta) {
    switch (sequencer_.ccLaneUi.mode) {
        case seq::SequencerCcLaneUiMode::LANE_SELECTOR:
            workflow_.moveSelector(delta);
            break;
        case seq::SequencerCcLaneUiMode::LANE_GRID:
            workflow_.moveFocusedStep(delta, now());
            break;
        case seq::SequencerCcLaneUiMode::ADD_LANE_DRAFT:
        case seq::SequencerCcLaneUiMode::LANE_SETTINGS:
            workflow_.moveDraftField(delta);
            break;
        case seq::SequencerCcLaneUiMode::CLOSED:
            break;
    }
}

FLASHMEM void SequencerCcLaneHandler::onOptTurn(float normalized) {
    const float delta = directionalDeltaFromNormalized(normalized);
    recenterDirectionalOpt();
    if (delta == 0.0f) return;

    switch (sequencer_.ccLaneUi.mode) {
        case seq::SequencerCcLaneUiMode::LANE_GRID:
            (void)workflow_.editFocusedEvent(delta, now());
            break;
        case seq::SequencerCcLaneUiMode::ADD_LANE_DRAFT:
        case seq::SequencerCcLaneUiMode::LANE_SETTINGS:
            workflow_.editDraft(delta);
            break;
        case seq::SequencerCcLaneUiMode::LANE_SELECTOR:
        case seq::SequencerCcLaneUiMode::CLOSED:
            break;
    }
}

FLASHMEM void SequencerCcLaneHandler::onNavTap() {
    if (sequencer_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_SELECTOR) {
        (void)workflow_.activateSelector();
    }
}

FLASHMEM void SequencerCcLaneHandler::onActionPress(
    seq::SequencerCcLaneActionSlot slot
) {
    (void)workflow_.beginGuard(slot, now());
}

FLASHMEM void SequencerCcLaneHandler::onActionRelease(
    seq::SequencerCcLaneActionSlot slot
) {
    (void)workflow_.releaseGuard(slot, now());
}

FLASHMEM void SequencerCcLaneHandler::back() {
    workflow_.closeOneLevel(now());
    if (!sequencer_.ccLaneUi.visible()) overlays_.hide();
}

FLASHMEM void SequencerCcLaneHandler::openPropertyGrammar() {
    (void)workflow_.commitEventEdit(now());
    overlays_.hide();
    property_selector_.openCcLaneShortcut();
}

}  // namespace core::handler

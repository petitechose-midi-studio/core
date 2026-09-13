#include "handler/sequencer/SequencerCcLaneHandler.hpp"
#include "handler/common/EncoderDefaults.hpp"

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
    oc::type::ScopeID viewScope,
    oc::type::ScopeID overlayScope,
    NowProvider nowProvider
)
    : sequencer_(sequencer)
    , workflow_(workflow)
    , property_selector_(propertySelector)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , view_scope_(viewScope)
    , overlay_scope_(overlayScope)
    , now_provider_(nowProvider ? nowProvider : oc::time::millis) {
    setupBindings();
}

FLASHMEM void SequencerCcLaneHandler::setupBindings() {
    // Share declarations, not ownership: the overlay still has its own scope,
    // while the view is gated by both CC mode and overlay visibility.
    const auto encoder = [this](EncoderID id, oc::type::ScopeID scope) {
        auto binding = encoders_.encoder(id);
        binding.turn().scope(scope);
        if (scope == view_scope_) binding.when([this]() { return mainGridOwnsInput(); });
        return binding;
    };
    const auto button = [this](ButtonID id, oc::type::ScopeID scope) {
        auto binding = buttons_.button(id);
        binding.scope(scope);
        if (scope == view_scope_) binding.when([this]() { return mainGridOwnsInput(); });
        return binding;
    };
    const auto onBack = [this]() { back(); };
    const auto onProperty = [this]() { openPropertyGrammar(); };
    for (const auto scope : {view_scope_, overlay_scope_}) {
        encoder(EncoderID::NAV, scope).then([this](float delta) { onNavTurn(delta); });
        encoder(EncoderID::OPT, scope).then([this](float delta) { onOptTurn(delta); });
        button(ButtonID::NAV, scope).release().then([this]() { onNavRelease(); });

        // Keep the original registration order and the two distinct entry gestures.
        if (scope == view_scope_) {
            button(ButtonID::LEFT_TOP, scope).release().then(onBack);
            button(ButtonID::LEFT_BOTTOM, scope).press().latch().then(onProperty);
        }
        for (const auto id : {ButtonID::BOTTOM_LEFT, ButtonID::BOTTOM_RIGHT}) {
            const auto slot = id == ButtonID::BOTTOM_LEFT
                ? seq::SequencerCcLaneActionSlot::BOTTOM_LEFT
                : seq::SequencerCcLaneActionSlot::BOTTOM_RIGHT;
            button(id, scope).press().then([this, slot]() { onActionPress(slot); });
            button(id, scope).release().then([this, slot]() { onActionRelease(slot); });
        }
        if (scope == overlay_scope_) {
            button(ButtonID::LEFT_TOP, scope).release().then(onBack);
            button(ButtonID::LEFT_CENTER, scope)
                .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
                .when([this]() {
                    return sequencer_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID;
                })
                .then(onProperty);
        }
    }

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const auto onTurn = [this, i](float value) { onMacroTurn(i, value); };
        encoder(Config::MACRO_ENCODERS[i], view_scope_).then(onTurn);
        encoder(Config::MACRO_ENCODERS[i], overlay_scope_)
            .when([this]() {
                const auto mode = sequencer_.ccLaneUi.mode;
                return mode == seq::SequencerCcLaneUiMode::LANE_GRID ||
                       mode == seq::SequencerCcLaneUiMode::TRANSITION_PICKER;
            })
            .then(onTurn);
    }
}

FLASHMEM bool SequencerCcLaneHandler::mainGridOwnsInput() const {
    return sequencer_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID &&
           !overlays_.hasVisible();
}

FLASHMEM bool SequencerCcLaneHandler::ccOverlayOwnsInput() const {
    return overlays_.isCurrent(core::ui::OverlayType::SEQ_CC_LANE);
}

FLASHMEM void SequencerCcLaneHandler::syncOverlayVisibility() {
    if (!ccOverlayOwnsInput()) return;
    if (sequencer_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID ||
        sequencer_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::CLOSED) {
        overlays_.hide();
    }
}

void SequencerCcLaneHandler::update(uint32_t nowMs) {
    syncOverlayVisibility();
    const bool ownsGrid = mainGridOwnsInput();
    const bool ownsInput = ownsGrid || ccOverlayOwnsInput();
    updateNavButtonGesture(nowMs, ownsInput);
    syncOptEncoderContract(ownsInput);
    syncMacroEncoderContract(ownsGrid);
    updateMacroButtonGestures(nowMs, ownsInput);
    workflow_.update(nowMs);
}

FLASHMEM void SequencerCcLaneHandler::beginNavButtonTracking(uint32_t nowMs) {
    if (nav_button_tracked_) return;
    nav_button_tracked_ = true;
    nav_button_long_ = false;
    nav_button_turned_ = false;
    nav_press_started_at_ms_ = nowMs;
}

FLASHMEM void SequencerCcLaneHandler::resetNavButtonTracking() {
    nav_button_tracked_ = false;
    nav_button_long_ = false;
    nav_button_turned_ = false;
    nav_press_started_at_ms_ = 0;
}

FLASHMEM void SequencerCcLaneHandler::updateNavButtonGesture(uint32_t nowMs, bool ownsInput) {
    if (!ownsInput) {
        if (nav_button_tracked_ && !buttons_.isPressed(ButtonID::NAV)) resetNavButtonTracking();
        return;
    }
    if (!buttons_.isPressed(ButtonID::NAV)) return;
    beginNavButtonTracking(nowMs);
    if (static_cast<uint32_t>(nowMs - nav_press_started_at_ms_) >=
        Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS) {
        nav_button_long_ = true;
    }
}

FLASHMEM void SequencerCcLaneHandler::beginMacroButtonTracking(
    uint8_t indexInWindow,
    uint32_t nowMs
) {
    if (indexInWindow >= Config::MACRO_COUNT) return;
    const uint8_t bit = static_cast<uint8_t>(1U << indexInWindow);
    if ((macro_button_down_mask_ & bit) != 0) return;
    macro_button_down_mask_ |= bit;
    macro_press_started_at_ms_[indexInWindow] = nowMs;
    (void)configureTransitionEncoder(indexInWindow);
}

FLASHMEM bool SequencerCcLaneHandler::configureTransitionEncoder(
    uint8_t indexInWindow
) {
    if (indexInWindow >= Config::MACRO_COUNT) return false;
    const auto* bank = seq::sequencerCcLaneView(sequencer_.pattern());
    const auto& ui = sequencer_.ccLaneUi;
    if (bank == nullptr || ui.focusedLane >= bank->lanes.size()) return false;
    const auto& lane = bank->lanes[ui.focusedLane];
    if (!lane.occupied) return false;

    const uint8_t start = static_cast<uint8_t>(
        (ui.focusedStep / seq::SequencerPatternState::STEPS_PER_PAGE) *
        seq::SequencerPatternState::STEPS_PER_PAGE
    );
    const uint8_t step = ui.mode == seq::SequencerCcLaneUiMode::TRANSITION_PICKER
        ? ui.transitionStep
        : static_cast<uint8_t>(start + indexInWindow);
    if (step >= seq::SequencerCcLaneBank::MAX_STEPS ||
        step != static_cast<uint8_t>(start + indexInWindow) ||
        !lane.activeMask.test(step)) {
        return false;
    }

    const auto transition = ui.mode == seq::SequencerCcLaneUiMode::TRANSITION_PICKER
        ? ui.selectedTransition
        : seq::sequencerCcLaneTransition(lane, step);
    const auto encoder = Config::MACRO_ENCODERS[indexInWindow];
    encoders_.setMode(encoder, oc::interface::EncoderMode::NORMALIZED);
    encoders_.setBounds(encoder, 0.0f, 1.0f);
    encoders_.configureResolution(encoder, 5,
        encoder_defaults::DEFAULT_DISCRETE_TICKS_PER_STEP, encoder_defaults::DEFAULT_NORMALIZED_TURNS);
    encoders_.setPosition(
        encoder,
        static_cast<float>(transition) / 4.0f
    );
    transition_encoder_mask_ |= static_cast<uint8_t>(1U << indexInWindow);
    return true;
}

FLASHMEM void SequencerCcLaneHandler::invalidateMacroEncoderContract() {
    transition_encoder_mask_ = 0;
    macro_encoders_configured_ = false;
    synced_lane_revision_ = 0xFFFFFFFFU;
    synced_window_start_ = 0xFF;
}

FLASHMEM void SequencerCcLaneHandler::updateMacroButtonGestures(uint32_t nowMs, bool ownsInput) {
    if (!ownsInput) {
        if (transition_encoder_mask_ != 0) invalidateMacroEncoderContract();
        macro_button_down_mask_ = 0;
        macro_button_long_mask_ = 0;
        macro_button_turn_mask_ = 0;
        return;
    }

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const uint8_t bit = static_cast<uint8_t>(1U << i);
        const bool pressed = buttons_.isPressed(Config::MACRO_BUTTONS[i]);
        const bool tracked = (macro_button_down_mask_ & bit) != 0;

        if (!tracked) {
            if (pressed && sequencer_.ccLaneUi.mode ==
                    seq::SequencerCcLaneUiMode::LANE_GRID) {
                beginMacroButtonTracking(i, nowMs);
            }
            continue;
        }

        if (pressed) {
            if ((macro_button_long_mask_ & bit) == 0 &&
                (macro_button_turn_mask_ & bit) == 0 &&
                sequencer_.ccLaneUi.mode ==
                    seq::SequencerCcLaneUiMode::LANE_GRID &&
                static_cast<uint32_t>(nowMs - macro_press_started_at_ms_[i]) >=
                    Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS) {
                // Poll the eight physical keys only while this overlay owns
                // input. This preserves the one-tap/hold grammar without
                // spending sixteen permanent global binding slots.
                macro_button_long_mask_ |= bit;
                onMacroLongPress(i);
            }
            continue;
        }

        const bool wasLong = (macro_button_long_mask_ & bit) != 0;
        const bool wasTurn = (macro_button_turn_mask_ & bit) != 0;
        macro_button_down_mask_ &= static_cast<uint8_t>(~bit);
        macro_button_long_mask_ &= static_cast<uint8_t>(~bit);
        macro_button_turn_mask_ &= static_cast<uint8_t>(~bit);
        if (wasTurn && sequencer_.ccLaneUi.mode ==
                seq::SequencerCcLaneUiMode::TRANSITION_PICKER) {
            (void)workflow_.applyTransition(nowMs);
        } else if (!wasLong && !wasTurn && sequencer_.ccLaneUi.mode ==
                seq::SequencerCcLaneUiMode::LANE_GRID) {
            onMacroRelease(i);
        }
        invalidateMacroEncoderContract();
    }
}

FLASHMEM void SequencerCcLaneHandler::syncMacroEncoderContract(bool ownsGrid) {
    if (!ownsGrid) {
        macro_encoders_configured_ = false;
        synced_lane_revision_ = 0xFFFFFFFFU;
        synced_window_start_ = 0xFF;
        return;
    }
    if (!macro_encoders_configured_) {
        for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
            if ((transition_encoder_mask_ & static_cast<uint8_t>(1U << i)) != 0) {
                continue;
            }
            const auto encoder = Config::MACRO_ENCODERS[i];
            encoders_.setMode(encoder, oc::interface::EncoderMode::NORMALIZED);
            encoders_.setBounds(encoder, 0.0f, 1.0f);
            encoders_.configureResolution(encoder, 128,
                encoder_defaults::DEFAULT_DISCRETE_TICKS_PER_STEP, encoder_defaults::DEFAULT_NORMALIZED_TURNS);
        }
        macro_encoders_configured_ = true;
    }

    const auto* bank = seq::sequencerCcLaneView(sequencer_.pattern());
    const auto& ui = sequencer_.ccLaneUi;
    if (bank == nullptr || ui.focusedLane >= bank->lanes.size()) return;
    const auto& lane = bank->lanes[ui.focusedLane];
    const uint8_t start = static_cast<uint8_t>(
        (ui.focusedStep / seq::SequencerPatternState::STEPS_PER_PAGE) *
        seq::SequencerPatternState::STEPS_PER_PAGE
    );
    if (synced_lane_revision_ == bank->revision &&
        synced_window_start_ == start) return;

    const uint8_t range = static_cast<uint8_t>(
        lane.destination.maximum - lane.destination.minimum
    );
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        if ((transition_encoder_mask_ & static_cast<uint8_t>(1U << i)) != 0) {
            continue;
        }
        const uint8_t step = static_cast<uint8_t>(start + i);
        uint8_t value = lane.initialValue;
        if (step < seq::SequencerCcLaneBank::MAX_STEPS &&
            lane.activeMask.test(step)) {
            value = lane.values[step];
        }
        const float normalized = range > 0
            ? static_cast<float>(value - lane.destination.minimum) /
                static_cast<float>(range)
            : 0.0f;
        encoders_.setPosition(Config::MACRO_ENCODERS[i], normalized);
    }
    synced_lane_revision_ = bank->revision;
    synced_window_start_ = start;
}

FLASHMEM void SequencerCcLaneHandler::syncOptEncoderContract(bool ownsOpt) {
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
    encoders_.configureResolution(EncoderID::OPT, 0,
        encoder_defaults::DEFAULT_DISCRETE_TICKS_PER_STEP, encoder_defaults::DEFAULT_NORMALIZED_TURNS);
    recenterDirectionalOpt();
}

FLASHMEM void SequencerCcLaneHandler::recenterDirectionalOpt() {
    encoders_.setPosition(EncoderID::OPT, DIRECTIONAL_OPT_CENTER);
}

FLASHMEM uint32_t SequencerCcLaneHandler::now() const {
    return now_provider_ ? now_provider_() : 0;
}

FLASHMEM void SequencerCcLaneHandler::onNavTurn(float delta) {
    if (sequencer_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID &&
        buttons_.isPressed(ButtonID::NAV)) {
        beginNavButtonTracking(now());
        nav_button_turned_ = true;
        if (workflow_.openFocusedTransitionPicker(now())) {
            overlays_.show(core::ui::OverlayType::SEQ_CC_LANE, false);
            buttons_.handoffPress(ButtonID::NAV, overlay_scope_);
            workflow_.moveTransition(delta);
        }
        return;
    }
    switch (sequencer_.ccLaneUi.mode) {
        case seq::SequencerCcLaneUiMode::LANE_SELECTOR:
            workflow_.moveSelector(delta);
            break;
        case seq::SequencerCcLaneUiMode::LANE_GRID:
            workflow_.moveFocusedStep(delta, now());
            break;
        case seq::SequencerCcLaneUiMode::TRANSITION_PICKER:
            workflow_.moveTransition(delta);
            break;
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
        case seq::SequencerCcLaneUiMode::TRANSITION_PICKER:
            workflow_.moveTransition(delta);
            break;
        case seq::SequencerCcLaneUiMode::LANE_SETTINGS:
            workflow_.editDraft(delta);
            break;
        case seq::SequencerCcLaneUiMode::LANE_SELECTOR:
        case seq::SequencerCcLaneUiMode::CLOSED:
            break;
    }
}

FLASHMEM void SequencerCcLaneHandler::onNavRelease() {
    const bool wasLong = nav_button_long_;
    const bool wasTurned = nav_button_turned_;
    resetNavButtonTracking();

    if (sequencer_.ccLaneUi.mode ==
            seq::SequencerCcLaneUiMode::TRANSITION_PICKER &&
        sequencer_.ccLaneUi.compactTransitionPicker) {
        if (wasTurned) {
            (void)workflow_.applyTransition(now());
        } else {
            workflow_.cancelTransition();
        }
        if (ccOverlayOwnsInput() && sequencer_.ccLaneUi.mode ==
                seq::SequencerCcLaneUiMode::LANE_GRID) {
            overlays_.hide();
        }
        return;
    }
    if ((wasLong || wasTurned) && sequencer_.ccLaneUi.mode ==
            seq::SequencerCcLaneUiMode::LANE_GRID) {
        return;
    }
    executeNavTap();
}

FLASHMEM void SequencerCcLaneHandler::executeNavTap() {
    switch (sequencer_.ccLaneUi.mode) {
        case seq::SequencerCcLaneUiMode::LANE_SELECTOR:
            if (workflow_.activateSelector(now()) &&
                sequencer_.ccLaneUi.mode ==
                    seq::SequencerCcLaneUiMode::LANE_GRID &&
                ccOverlayOwnsInput()) {
                overlays_.hide();
            }
            break;
        case seq::SequencerCcLaneUiMode::LANE_GRID:
            (void)workflow_.toggleFocusedEvent(now());
            break;
        case seq::SequencerCcLaneUiMode::TRANSITION_PICKER:
            (void)workflow_.applyTransition(now());
            break;
        case seq::SequencerCcLaneUiMode::LANE_SETTINGS:
            (void)workflow_.activateDraftField();
            break;
        case seq::SequencerCcLaneUiMode::CLOSED:
            break;
    }
}

FLASHMEM void SequencerCcLaneHandler::onMacroTurn(
    uint8_t indexInWindow,
    float normalized
) {
    if (indexInWindow >= Config::MACRO_COUNT) return;
    const uint8_t bit = static_cast<uint8_t>(1U << indexInWindow);
    const bool held = buttons_.isPressed(Config::MACRO_BUTTONS[indexInWindow]);
    if (held) {
        if ((macro_button_down_mask_ & bit) == 0 &&
            sequencer_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID) {
            beginMacroButtonTracking(indexInWindow, now());
        }
        macro_button_turn_mask_ |= bit;
        if (sequencer_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID &&
            !workflow_.openTransitionPicker(indexInWindow, now())) {
            return;
        }
        if (sequencer_.ccLaneUi.mode ==
                seq::SequencerCcLaneUiMode::TRANSITION_PICKER &&
            !ccOverlayOwnsInput()) {
            overlays_.show(core::ui::OverlayType::SEQ_CC_LANE, false);
        }
        if (sequencer_.ccLaneUi.mode ==
                seq::SequencerCcLaneUiMode::TRANSITION_PICKER) {
            if ((transition_encoder_mask_ & bit) == 0) {
                (void)configureTransitionEncoder(indexInWindow);
            }
            (void)workflow_.selectTransitionNormalized(normalized);
        }
        return;
    }
    if (sequencer_.ccLaneUi.mode != seq::SequencerCcLaneUiMode::LANE_GRID) return;
    (void)workflow_.editVisibleEvent(indexInWindow, normalized, now());
}

FLASHMEM void SequencerCcLaneHandler::onMacroRelease(uint8_t indexInWindow) {
    if (indexInWindow >= Config::MACRO_COUNT) return;
    if (sequencer_.ccLaneUi.mode != seq::SequencerCcLaneUiMode::LANE_GRID) return;
    (void)workflow_.toggleVisibleEvent(indexInWindow, now());
}

FLASHMEM void SequencerCcLaneHandler::onMacroLongPress(uint8_t indexInWindow) {
    if (indexInWindow >= Config::MACRO_COUNT) return;
    if (workflow_.openTransitionPicker(indexInWindow, now())) {
        overlays_.show(core::ui::OverlayType::SEQ_CC_LANE, false);
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
    const auto mode = sequencer_.ccLaneUi.mode;
    if (mode == seq::SequencerCcLaneUiMode::LANE_SETTINGS ||
        mode == seq::SequencerCcLaneUiMode::TRANSITION_PICKER ||
        mode == seq::SequencerCcLaneUiMode::LANE_SELECTOR) {
        overlays_.show(core::ui::OverlayType::SEQ_CC_LANE, false);
    } else if (ccOverlayOwnsInput()) {
        overlays_.hide();
    }
}

FLASHMEM void SequencerCcLaneHandler::back() {
    workflow_.closeOneLevel(now());
    if (sequencer_.ccLaneUi.mode == seq::SequencerCcLaneUiMode::LANE_GRID ||
        !sequencer_.ccLaneUi.visible()) {
        overlays_.hide();
    }
}

FLASHMEM void SequencerCcLaneHandler::openPropertyGrammar() {
    (void)workflow_.commitEventEdit(now());
    property_selector_.openCcLaneShortcut();
}

}  // namespace core::handler

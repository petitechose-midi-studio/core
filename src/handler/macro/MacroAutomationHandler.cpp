#include "handler/macro/MacroAutomationHandler.hpp"

#include <algorithm>
#include <cmath>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>
#include <config/Timing.hpp>

#include "handler/common/ModalSelectionUtils.hpp"
#include "handler/common/NavigationUtils.hpp"

namespace core::handler {

namespace {

constexpr uint8_t ROW_STATE = 0;
constexpr uint8_t ROW_LENGTH = 1;
constexpr uint8_t ROW_OFFSET = 2;
constexpr uint8_t ROW_COUNT = 3;
constexpr uint8_t kMinDurationBeats = 1;
constexpr uint8_t kMaxDurationBeats = 64;
constexpr uint8_t kCoarseBeatStep = 4;

uint8_t beatStepFromTicks(uint16_t ticks, uint8_t minBeat, uint8_t maxBeat) {
    const float beats = core::state::macro::macroAutomationBeatsFromTicks(ticks);
    return static_cast<uint8_t>(std::clamp(
        static_cast<int>(std::lround(beats)),
        static_cast<int>(minBeat),
        static_cast<int>(maxBeat)
    ));
}

uint8_t steppedBeatCount(uint8_t minBeat, uint8_t maxBeat, uint8_t beatStep) {
    const uint8_t step = std::max<uint8_t>(1, beatStep);
    if (maxBeat <= minBeat) return 1;
    return static_cast<uint8_t>(((maxBeat - minBeat) / step) + 1U);
}

uint8_t sourceOffsetBeatCount(const core::state::macro::MacroAutomationSlotState* slot) {
    if (slot == nullptr || !slot->automation.active) return 1;
    const uint16_t sourceTicks = std::max<uint16_t>(
        slot->automation.sourceDurationTicks,
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
    );
    const uint16_t sourceBeats = static_cast<uint16_t>(
        (sourceTicks + core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT - 1U) /
        core::state::macro::MACRO_AUTOMATION_TICKS_PER_BEAT
    );
    return static_cast<uint8_t>(std::clamp<uint16_t>(
        sourceBeats,
        1,
        kMaxDurationBeats
    ));
}

uint8_t offsetStepCount(const core::state::macro::MacroAutomationSlotState* slot,
                        uint8_t beatStep) {
    const uint8_t sourceBeats = sourceOffsetBeatCount(slot);
    const uint8_t step = std::max<uint8_t>(1, beatStep);
    return static_cast<uint8_t>(((sourceBeats - 1U) / step) + 1U);
}

float normalizedStepToBeat(float normalized,
                           uint8_t minBeat,
                           uint8_t stepCount,
                           uint8_t beatStep) {
    if (stepCount <= 1) return static_cast<float>(minBeat);
    const uint8_t stepSize = std::max<uint8_t>(1, beatStep);
    const int step = static_cast<int>(
        std::clamp(normalized, 0.0f, 1.0f) * static_cast<float>(stepCount - 1U) + 0.5f
    );
    return static_cast<float>(
        minBeat + (std::clamp(step, 0, static_cast<int>(stepCount - 1U)) * stepSize)
    );
}

float beatToNormalizedStep(uint8_t beat,
                           uint8_t minBeat,
                           uint8_t stepCount,
                           uint8_t beatStep) {
    if (stepCount <= 1) return 0.0f;
    const uint8_t stepSize = std::max<uint8_t>(1, beatStep);
    const int rawStep = static_cast<int>(std::lround(
        static_cast<float>(static_cast<int>(beat) - static_cast<int>(minBeat)) /
        static_cast<float>(stepSize)
    ));
    const int clampedStep = std::clamp(rawStep, 0, static_cast<int>(stepCount - 1U));
    return static_cast<float>(clampedStep) / static_cast<float>(stepCount - 1U);
}

}  // namespace

FLASHMEM MacroAutomationHandler::MacroAutomationHandler(
    StateRefs state,
    MacroEditDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID automationScope
)
    : macro_edit_(state.macroEdit)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , automation_scope_(automationScope) {
    setupBindings();
}

FLASHMEM void MacroAutomationHandler::setupBindings() {
    using ButtonID = Config::ButtonID;
    using EncoderID = Config::EncoderID;

    encoders_.encoder(EncoderID::NAV)
        .turn()
        .scope(automation_scope_)
        .then([this](float delta) { moveFocus(delta); });

    encoders_.encoder(EncoderID::OPT)
        .turn()
        .scope(automation_scope_)
        .then([this](float normalized) { editFocusedValue(normalized); });

    buttons_.button(ButtonID::LEFT_TOP)
        .release()
        .scope(automation_scope_)
        .then([this]() { backToMacroEdit(); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .press()
        .scope(automation_scope_)
        .then([this]() { setCoarseEditActive(true); });

    buttons_.button(ButtonID::LEFT_CENTER)
        .release()
        .scope(automation_scope_)
        .then([this]() { setCoarseEditActive(false); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .release()
        .scope(automation_scope_)
        .then([this]() { clearAutomation(); });

    buttons_.button(ButtonID::BOTTOM_LEFT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(automation_scope_)
        .then([this]() { removeAutomation(); });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .release()
        .scope(automation_scope_)
        .then([this]() { copyAutomation(); });

    buttons_.button(ButtonID::BOTTOM_RIGHT)
        .longPress(Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS)
        .scope(automation_scope_)
        .then([this]() { pasteAutomation(); });
}

bool MacroAutomationHandler::active() const {
    return macro_edit_.visible.get() &&
           macro_edit_.flowPhase.get() == core::state::MacroEditFlowPhase::AUTOMATION;
}

uint8_t MacroAutomationHandler::macroIndex() const {
    return macro_edit_.editingIndex.get();
}

FLASHMEM void MacroAutomationHandler::moveFocus(float delta) {
    if (!active() || !nav::hasTurnDelta(delta)) return;
    const int current = static_cast<int>(macro_edit_.automationFocusedRow.get());
    const int next = nav::nextWrappedIndex(delta, current, ROW_COUNT);
    macro_edit_.automationFocusedRow.set(static_cast<uint8_t>(next));
    configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::editFocusedValue(float normalized) {
    if (!active()) return;
    const uint8_t index = macroIndex();
    if (!services_.automationActiveFor(index)) return;
    const float clamped = std::clamp(normalized, 0.0f, 1.0f);
    const uint8_t row = macro_edit_.automationFocusedRow.get();
    if (row == ROW_STATE) {
        services_.setAutomationManualOverride(index, clamped < 0.5f);
        return;
    }
    if (row == ROW_LENGTH) {
        const uint8_t beatStep = coarse_edit_active_ ? kCoarseBeatStep : 1;
        services_.setAutomationDurationBeats(
            index,
            normalizedStepToBeat(
                clamped,
                coarse_edit_active_ ? kCoarseBeatStep : kMinDurationBeats,
                steppedBeatCount(
                    coarse_edit_active_ ? kCoarseBeatStep : kMinDurationBeats,
                    kMaxDurationBeats,
                    beatStep
                ),
                beatStep
            )
        );
        return;
    }
    if (row == ROW_OFFSET) {
        const auto* slot = services_.automationSlot(index);
        const uint8_t beatStep = coarse_edit_active_ ? kCoarseBeatStep : 1;
        services_.setAutomationWindowOffsetBeats(
            index,
            normalizedStepToBeat(
                clamped,
                0,
                offsetStepCount(slot, beatStep),
                beatStep
            )
        );
    }
}

FLASHMEM void MacroAutomationHandler::configureOptForFocusedRow() {
    uint8_t steps = 1;
    float position = 0.0f;
    const uint8_t row = macro_edit_.automationFocusedRow.get();
    if (row == ROW_STATE) {
        steps = 2;
        const uint8_t index = macroIndex();
        position = services_.automationManualOverrideActiveFor(index) ? 0.0f : 1.0f;
    } else if (row == ROW_LENGTH) {
        const uint8_t beatStep = coarse_edit_active_ ? kCoarseBeatStep : 1;
        const uint8_t minBeat = coarse_edit_active_ ? kCoarseBeatStep : kMinDurationBeats;
        steps = steppedBeatCount(minBeat, kMaxDurationBeats, beatStep);
        const auto* slot = services_.automationSlot(macroIndex());
        if (slot != nullptr && slot->automation.active) {
            const uint8_t beat = beatStepFromTicks(
                slot->automation.durationTicks,
                minBeat,
                kMaxDurationBeats
            );
            position = beatToNormalizedStep(beat, minBeat, steps, beatStep);
        }
    } else if (row == ROW_OFFSET) {
        const auto* slot = services_.automationSlot(macroIndex());
        const uint8_t beatStep = coarse_edit_active_ ? kCoarseBeatStep : 1;
        steps = offsetStepCount(slot, beatStep);
        if (slot != nullptr && slot->automation.active) {
            const uint8_t beat = beatStepFromTicks(
                slot->automation.windowOffsetTicks,
                0,
                static_cast<uint8_t>((steps - 1U) * beatStep)
            );
            position = beatToNormalizedStep(beat, 0, steps, beatStep);
        }
    }
    encoders_.setDiscreteSteps(Config::EncoderID::OPT, steps);
    encoders_.setPosition(Config::EncoderID::OPT, position);
}

FLASHMEM void MacroAutomationHandler::setCoarseEditActive(bool active) {
    if (!this->active() || coarse_edit_active_ == active) return;
    coarse_edit_active_ = active;
    configureOptForFocusedRow();
}

FLASHMEM void MacroAutomationHandler::backToMacroEdit() {
    if (!active()) return;
    modal::hideIfCurrent(overlays_, core::ui::OverlayType::MACRO_AUTOMATION);
    macro_edit_.closeAutomation();
}

FLASHMEM void MacroAutomationHandler::clearAutomation() {
    if (!active()) return;
    if (ignore_next_bottom_left_release_) {
        ignore_next_bottom_left_release_ = false;
        return;
    }
    services_.clearAutomation(macroIndex());
}

FLASHMEM void MacroAutomationHandler::removeAutomation() {
    if (!active()) return;
    ignore_next_bottom_left_release_ = true;
    services_.removeAutomation(macroIndex());
}

FLASHMEM void MacroAutomationHandler::copyAutomation() {
    if (!active()) return;
    if (ignore_next_bottom_right_release_) {
        ignore_next_bottom_right_release_ = false;
        return;
    }
    services_.copyAutomation(macroIndex());
}

FLASHMEM void MacroAutomationHandler::pasteAutomation() {
    if (!active()) return;
    ignore_next_bottom_right_release_ = true;
    services_.pasteAutomation(macroIndex());
}

}  // namespace core::handler

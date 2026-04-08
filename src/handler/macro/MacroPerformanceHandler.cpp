#include "handler/macro/MacroPerformanceHandler.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "config/InputIDs.hpp"
#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"

namespace core::handler {

namespace input_utils = core::handler::sequencer::input_utils;

namespace {

inline oc::type::IsActiveFn performanceAvailable(
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
) {
    return [&overlays]() { return !overlays.hasVisible(); };
}

inline oc::type::IsActiveFn quickControlsSelecting(
    core::state::macro::MacroUiState& macroUi,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
) {
    return [&macroUi, &overlays]() {
        return macroUi.quickControlsSelecting.get() && !overlays.hasVisible();
    };
}

inline oc::type::IsActiveFn pageSelecting(
    core::state::macro::MacroUiState& macroUi,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
) {
    return [&macroUi, &overlays]() {
        return macroUi.pageSelecting.get() && !overlays.hasVisible();
    };
}

inline oc::type::IsActiveFn clutchActive(
    core::state::macro::MacroUiState& macroUi,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
) {
    return [&macroUi, &overlays]() {
        return macroUi.clutchActive.get() &&
               !macroUi.quickControlsSelecting.get() &&
               !macroUi.pageSelecting.get() &&
               !overlays.hasVisible();
    };
}

inline oc::type::IsActiveFn clutchInactive(
    core::state::macro::MacroUiState& macroUi,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays
) {
    return [&macroUi, &overlays]() {
        return !macroUi.clutchActive.get() &&
               !macroUi.quickControlsSelecting.get() &&
               !macroUi.pageSelecting.get() &&
               !overlays.hasVisible();
    };
}

float normalizedForProperty(const core::handler::MacroDomainServices& services,
                            uint8_t index,
                            core::state::macro::MacroPerformanceProperty property) {
    if (property == core::state::macro::MacroPerformanceProperty::CC) {
        return input_utils::indexToNormalized(services.activeConfig(index).cc, 128);
    }

    if (property == core::state::macro::MacroPerformanceProperty::CHANNEL) {
        return input_utils::indexToNormalized(services.activeConfig(index).channel, 16);
    }

    return services.runtimeValue(index);
}

}  // namespace

FLASHMEM MacroPerformanceHandler::MacroPerformanceHandler(
    StateRefs state,
    MacroDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders,
    oc::api::ButtonAPI& buttons,
    oc::type::ScopeID scopeId)
    : macro_ui_(state.macroUi)
    , pages_(state.pages)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders)
    , buttons_(buttons)
    , scope_id_(scopeId) {
    configureMacroEncoders();
    setupBindings();
}

FLASHMEM void MacroPerformanceHandler::setupBindings() {
    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .press()
        .scope(scope_id_)
        .when([this]() {
            left_center_held_ = true;
            return performanceAvailable(overlays_)() &&
                   left_bottom_held_;
        })
        .then([this]() { openPageSelector(); });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .scope(scope_id_)
        .when([this]() {
            left_bottom_held_ = true;
            return performanceAvailable(overlays_)() &&
                   left_center_held_;
        })
        .then([this]() { openPageSelector(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .press()
        .latch()
        .scope(scope_id_)
        .when([this]() {
            left_center_held_ = true;
            return performanceAvailable(overlays_)() &&
                   !left_bottom_held_ &&
                   !macro_ui_.pageSelecting.get();
        })
        .then([this]() { openQuickControls(); });

    buttons_.button(Config::ButtonID::LEFT_CENTER)
        .release()
        .scope(scope_id_)
        .then([this]() {
            left_center_held_ = false;
            if (macro_ui_.pageSelecting.get()) {
                closePageSelectorApplyIfReleased();
                return;
            }
            if (macro_ui_.quickControlsSelecting.get()) {
                closeQuickControlsApply();
            }
        });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .press()
        .latch()
        .scope(scope_id_)
        .when([this]() {
            left_bottom_held_ = true;
            return performanceAvailable(overlays_)() &&
                   !left_center_held_ &&
                   !macro_ui_.quickControlsSelecting.get() &&
                   !macro_ui_.pageSelecting.get();
        })
        .then([this]() { activateClutch(); });

    buttons_.button(Config::ButtonID::LEFT_BOTTOM)
        .release()
        .scope(scope_id_)
        .then([this]() {
            left_bottom_held_ = false;
            if (macro_ui_.pageSelecting.get()) {
                closePageSelectorApplyIfReleased();
                return;
            }
            if (macro_ui_.clutchActive.get() &&
                !macro_ui_.quickControlsSelecting.get()) {
                deactivateClutch();
            }
        });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(pageSelecting(macro_ui_, overlays_))
        .then([this](float delta) { navigatePageSelector(delta); });

    buttons_.button(Config::ButtonID::NAV)
        .release()
        .scope(scope_id_)
        .when(pageSelecting(macro_ui_, overlays_))
        .then([this]() { toggleSelectedPageEnabled(); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(quickControlsSelecting(macro_ui_, overlays_))
        .then([this](float delta) { navigateQuickControls(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(clutchActive(macro_ui_, overlays_))
        .then([this](float delta) { navigateProperty(delta); });

    encoders_.encoder(Config::EncoderID::NAV)
        .turn()
        .scope(scope_id_)
        .when(clutchInactive(macro_ui_, overlays_))
        .then([this](float delta) { movePage(delta); });

    encoders_.encoder(Config::EncoderID::OPT)
        .turn()
        .scope(scope_id_)
        .when(quickControlsSelecting(macro_ui_, overlays_))
        .then([this](float normalized) { setFocusedQuickControlValue(normalized); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(quickControlsSelecting(macro_ui_, overlays_))
        .then([this]() { closeQuickControlsCancel(); });

    buttons_.button(Config::ButtonID::LEFT_TOP)
        .release()
        .scope(scope_id_)
        .when(pageSelecting(macro_ui_, overlays_))
        .then([this]() { closePageSelectorCancel(); });
}

FLASHMEM void MacroPerformanceHandler::activateClutch() {
    if (overlays_.hasVisible()) return;
    if (macro_ui_.quickControlsSelecting.get() || macro_ui_.pageSelecting.get()) return;
    macro_ui_.clutchActive.set(true);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::deactivateClutch() {
    if (!macro_ui_.clutchActive.get()) return;
    macro_ui_.clutchActive.set(false);
    macro_ui_.activeProperty.set(core::state::macro::MacroPerformanceProperty::VALUE);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::openQuickControls() {
    if (overlays_.hasVisible() || macro_ui_.pageSelecting.get()) return;

    macro_ui_.clutchActive.set(false);
    macro_ui_.quickControlsSelecting.set(true);
    macro_ui_.focusedQuickControl.set(core::state::macro::MacroQuickControlItem::GLOBAL_CHANNEL);
    macro_ui_.ccOffset.set(0);
    quick_snapshot_page_ = pages_.activePage;
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        quick_snapshot_configs_[i] = services_.activeConfig(i);
    }
    configureQuickControlEncoder();
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::closeQuickControlsApply() {
    if (!macro_ui_.quickControlsSelecting.get()) return;
    if (macro_ui_.pageSelecting.get()) return;
    macro_ui_.quickControlsSelecting.set(false);
    resetQuickControlsState();
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::closeQuickControlsCancel() {
    if (!macro_ui_.quickControlsSelecting.get()) return;

    const uint8_t originalPage = quick_snapshot_page_;
    if (pages_.activePage != originalPage) {
        services_.switchToPage(originalPage);
    }
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        services_.setConfig(i, quick_snapshot_configs_[i].channel, quick_snapshot_configs_[i].cc);
    }
    macro_ui_.quickControlsSelecting.set(false);
    resetQuickControlsState();
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::navigateQuickControls(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const int current = core::state::macro::quickControlIndex(macro_ui_.focusedQuickControl.get());
    const int next = nav::nextWrappedIndex(delta, current, 2);
    macro_ui_.focusedQuickControl.set(core::state::macro::quickControlAtIndex(next));
    configureQuickControlEncoder();
}

FLASHMEM void MacroPerformanceHandler::setFocusedQuickControlValue(float normalized) {
    const auto item = macro_ui_.focusedQuickControl.get();
    if (item == core::state::macro::MacroQuickControlItem::CC_OFFSET) {
        const int offset = normalizedToOffset(normalized);
        if (macro_ui_.ccOffset.get() == offset) return;
        macro_ui_.ccOffset.set(static_cast<int8_t>(offset));
        applyCcOffsetFromSnapshot(offset);
        return;
    }

    const uint8_t channel =
        static_cast<uint8_t>(input_utils::normalizedToIndex(std::clamp(normalized, 0.0f, 1.0f), 16));
    if (services_.activeConfig(0).channel == channel) return;
    applyGlobalChannel(channel);
}

FLASHMEM void MacroPerformanceHandler::openPageSelector() {
    if (overlays_.hasVisible()) return;

    if (macro_ui_.quickControlsSelecting.get()) {
        closeQuickControlsApply();
    }
    if (macro_ui_.clutchActive.get()) {
        macro_ui_.clutchActive.set(false);
        macro_ui_.activeProperty.set(core::state::macro::MacroPerformanceProperty::VALUE);
    }

    page_selector_snapshot_page_ = pages_.activePage;
    page_selector_snapshot_enabled_mask_ = services_.pageEnabledMask();
    macro_ui_.pageSelecting.set(true);
    macro_ui_.selectedPage.set(pages_.activePage);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::navigatePageSelector(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const int current = macro_ui_.selectedPage.get();
    const int next = nav::nextWrappedIndex(delta, current, core::state::macro::PAGE_COUNT);
    macro_ui_.selectedPage.set(static_cast<uint8_t>(next));
}

FLASHMEM void MacroPerformanceHandler::closePageSelectorApplyIfReleased() {
    if (!macro_ui_.pageSelecting.get()) return;
    if (left_center_held_ || left_bottom_held_) {
        return;
    }

    const uint8_t target = macro_ui_.selectedPage.get();
    services_.switchToPage(target);
    macro_ui_.pageSelecting.set(false);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::toggleSelectedPageEnabled() {
    if (!macro_ui_.pageSelecting.get()) return;
    services_.togglePageEnabled(macro_ui_.selectedPage.get());
}

FLASHMEM void MacroPerformanceHandler::closePageSelectorCancel() {
    if (!macro_ui_.pageSelecting.get()) return;
    services_.setPageEnabledMask(page_selector_snapshot_enabled_mask_);
    macro_ui_.selectedPage.set(page_selector_snapshot_page_);
    macro_ui_.pageSelecting.set(false);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::navigateProperty(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = core::state::macro::performancePropertyIndex(
        macro_ui_.activeProperty.get()
    );
    const int next = nav::nextWrappedIndex(delta, current, 3);
    macro_ui_.activeProperty.set(core::state::macro::performancePropertyAtIndex(next));
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::movePage(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const int current = pages_.activePage;
    const int next = nav::nextWrappedIndex(delta, current, core::state::macro::PAGE_COUNT);
    if (next == current) return;

    services_.switchToPage(static_cast<uint8_t>(next));
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceHandler::configureMacroEncoders() {
    if (macro_ui_.quickControlsSelecting.get() || macro_ui_.pageSelecting.get()) {
        configureValueEncoders();
        return;
    }

    const auto property = macro_ui_.clutchActive.get()
        ? macro_ui_.activeProperty.get()
        : core::state::macro::MacroPerformanceProperty::VALUE;

    switch (property) {
        case core::state::macro::MacroPerformanceProperty::CC:
            configureDiscreteEncoders(128);
            break;
        case core::state::macro::MacroPerformanceProperty::CHANNEL:
            configureDiscreteEncoders(16);
            break;
        case core::state::macro::MacroPerformanceProperty::VALUE:
        default:
            configureValueEncoders();
            break;
    }

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        encoders_.setPosition(Config::MACRO_ENCODERS[i], normalizedForProperty(services_, i, property));
    }
}

FLASHMEM void MacroPerformanceHandler::configureQuickControlEncoder() {
    using Item = core::state::macro::MacroQuickControlItem;

    if (macro_ui_.focusedQuickControl.get() == Item::CC_OFFSET) {
        const int itemCount = (currentCcOffsetMax() - currentCcOffsetMin()) + 1;
        configureDiscreteEncoder(Config::EncoderID::OPT, static_cast<uint8_t>(std::max(itemCount, 1)));
        encoders_.setPosition(
            Config::EncoderID::OPT,
            offsetToNormalized(macro_ui_.ccOffset.get())
        );
        return;
    }

    configureDiscreteEncoder(Config::EncoderID::OPT, 16);
    encoders_.setPosition(
        Config::EncoderID::OPT,
        input_utils::indexToNormalized(services_.activeConfig(0).channel, 16)
    );
}

FLASHMEM void MacroPerformanceHandler::resetQuickControlsState() {
    macro_ui_.focusedQuickControl.set(core::state::macro::MacroQuickControlItem::GLOBAL_CHANNEL);
    macro_ui_.ccOffset.set(0);
}

FLASHMEM int MacroPerformanceHandler::currentCcOffsetMin() const {
    int minOffset = -127;
    for (const auto& config : quick_snapshot_configs_) {
        minOffset = std::max(minOffset, -static_cast<int>(config.cc));
    }
    return minOffset;
}

FLASHMEM int MacroPerformanceHandler::currentCcOffsetMax() const {
    int maxOffset = 127;
    for (const auto& config : quick_snapshot_configs_) {
        maxOffset = std::min(maxOffset, 127 - static_cast<int>(config.cc));
    }
    return maxOffset;
}

FLASHMEM float MacroPerformanceHandler::offsetToNormalized(int offset) const {
    const int minOffset = currentCcOffsetMin();
    const int maxOffset = currentCcOffsetMax();
    if (maxOffset <= minOffset) return 0.5f;
    const int clamped = std::clamp(offset, minOffset, maxOffset);
    return static_cast<float>(clamped - minOffset) /
           static_cast<float>(maxOffset - minOffset);
}

FLASHMEM int MacroPerformanceHandler::normalizedToOffset(float normalized) const {
    const int minOffset = currentCcOffsetMin();
    const int maxOffset = currentCcOffsetMax();
    if (maxOffset <= minOffset) return 0;
    const int itemCount = (maxOffset - minOffset) + 1;
    const int index = input_utils::normalizedToIndex(std::clamp(normalized, 0.0f, 1.0f), itemCount);
    return minOffset + index;
}

FLASHMEM void MacroPerformanceHandler::applyCcOffsetFromSnapshot(int offset) const {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const auto& snapshot = quick_snapshot_configs_[i];
        const int nextCc = std::clamp(static_cast<int>(snapshot.cc) + offset, 0, 127);
        services_.setConfig(i, snapshot.channel, static_cast<uint8_t>(nextCc));
    }
}

FLASHMEM void MacroPerformanceHandler::applyGlobalChannel(uint8_t channel) const {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        services_.setConfig(i, channel, services_.activeConfig(i).cc);
    }
}

FLASHMEM void MacroPerformanceHandler::configureValueEncoders() {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        configureNormalizedEncoder(Config::MACRO_ENCODERS[i]);
        encoders_.setContinuous(Config::MACRO_ENCODERS[i]);
    }
}

FLASHMEM void MacroPerformanceHandler::configureDiscreteEncoders(uint8_t discreteSteps) {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        configureDiscreteEncoder(Config::MACRO_ENCODERS[i], discreteSteps);
    }
}

FLASHMEM void MacroPerformanceHandler::configureNormalizedEncoder(Config::EncoderID id) {
    encoders_.setDiscreteTicksPerStep(id, input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP);
    encoders_.setNormalizedTurns(id, input_utils::DEFAULT_NORMALIZED_TURNS);
}

FLASHMEM void MacroPerformanceHandler::configureDiscreteEncoder(
    Config::EncoderID id,
    uint8_t discreteSteps
) {
    configureNormalizedEncoder(id);
    encoders_.setDiscreteSteps(id, discreteSteps);
}

}  // namespace core::handler

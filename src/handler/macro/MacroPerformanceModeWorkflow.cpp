#include "handler/macro/MacroPerformanceModeWorkflow.hpp"

#include <algorithm>

#include <config/InputIDs.hpp>
#include <config/PlatformCompat.hpp>

#include "handler/common/NavigationUtils.hpp"
#include "handler/sequencer/SequencerInputUtils.hpp"

namespace core::handler {

namespace input_utils = core::handler::sequencer::input_utils;

namespace {

float normalizedForProperty(const core::handler::MacroPerformanceDomainServices& services,
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

FLASHMEM MacroPerformanceModeWorkflow::MacroPerformanceModeWorkflow(
    StateRefs state,
    MacroPerformanceDomainServices services,
    oc::context::OverlayManager<core::ui::OverlayType>& overlays,
    oc::api::EncoderAPI& encoders)
    : macro_ui_(state.macroUi)
    , pages_(state.pages)
    , track_ui_(state.trackNavigation)
    , services_(services)
    , overlays_(overlays)
    , encoders_(encoders) {
    refreshEncoders();
}

bool MacroPerformanceModeWorkflow::performanceAvailable() const {
    return !overlays_.hasVisible() &&
           !macro_ui_.pageSelection.active.get() &&
           !track_ui_.selection.active.get();
}

bool MacroPerformanceModeWorkflow::quickControlsSelecting() const {
    return macro_ui_.quickControlsSelecting.get() && !overlays_.hasVisible();
}

bool MacroPerformanceModeWorkflow::clutchActive() const {
    return macro_ui_.clutchActive.get() &&
           !macro_ui_.quickControlsSelecting.get() &&
           !macro_ui_.pageSelection.active.get() &&
           !track_ui_.selection.active.get() &&
           !overlays_.hasVisible();
}

bool MacroPerformanceModeWorkflow::clutchInactive() const {
    return !macro_ui_.clutchActive.get() &&
           !macro_ui_.quickControlsSelecting.get() &&
           !macro_ui_.pageSelection.active.get() &&
           !track_ui_.selection.active.get() &&
           !overlays_.hasVisible();
}

FLASHMEM void MacroPerformanceModeWorkflow::activateClutch() {
    if (overlays_.hasVisible()) return;
    if (macro_ui_.quickControlsSelecting.get()) return;
    track_ui_.previewAddSlot.set(false);
    macro_ui_.previewAddPageSlot.set(false);
    initializeClutchChannelPreview();
    macro_ui_.clutchActive.set(true);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::deactivateClutch() {
    if (!macro_ui_.clutchActive.get()) return;
    commitClutchChannelPreview();
    macro_ui_.clutchActive.set(false);
    macro_ui_.activeProperty.set(core::state::macro::MacroPerformanceProperty::VALUE);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::openQuickControls() {
    if (overlays_.hasVisible()) return;

    track_ui_.previewAddSlot.set(false);
    macro_ui_.previewAddPageSlot.set(false);
    macro_ui_.clutchActive.set(false);
    macro_ui_.quickControlsSelecting.set(true);
    macro_ui_.focusedQuickControl.set(core::state::macro::MacroQuickControlItem::GLOBAL_CHANNEL);
    macro_ui_.quickControlGlobalChannel.set(services_.activeConfig(0).channel);
    macro_ui_.ccOffset.set(0);
    quick_snapshot_page_ = pages_.currentActivePage();
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        quick_snapshot_configs_[i] = services_.activeConfig(i);
    }
    configureQuickControlEncoder();
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::closeQuickControlsApply() {
    if (!macro_ui_.quickControlsSelecting.get()) return;

    const uint8_t originalPage = quick_snapshot_page_;
    if (pages_.currentActivePage() != originalPage) {
        services_.switchToPage(originalPage);
        macro_ui_.syncPreviewPage(pages_.currentActivePage());
    }

    const uint8_t channel = macro_ui_.quickControlGlobalChannel.get();
    const int offset = macro_ui_.ccOffset.get();
    std::array<core::state::macro::MacroConfig, Config::MACRO_COUNT> configs{};
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const auto& snapshot = quick_snapshot_configs_[i];
        const int nextCc = std::clamp(static_cast<int>(snapshot.cc) + offset, 0, 127);
        configs[i] = {
            .cc = static_cast<uint8_t>(nextCc),
            .channel = channel,
        };
    }
    services_.setTrackConfigs(configs);

    macro_ui_.quickControlsSelecting.set(false);
    resetQuickControlsState();
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::closeQuickControlsCancel() {
    if (!macro_ui_.quickControlsSelecting.get()) return;

    const uint8_t originalPage = quick_snapshot_page_;
    if (pages_.currentActivePage() != originalPage) {
        services_.switchToPage(originalPage);
        macro_ui_.syncPreviewPage(pages_.currentActivePage());
    }
    macro_ui_.quickControlsSelecting.set(false);
    resetQuickControlsState();
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::navigateQuickControls(float delta) {
    if (!nav::hasTurnDelta(delta)) return;
    const int current = core::state::macro::quickControlIndex(macro_ui_.focusedQuickControl.get());
    const int next = nav::nextWrappedIndex(delta, current, 2);
    macro_ui_.focusedQuickControl.set(core::state::macro::quickControlAtIndex(next));
    configureQuickControlEncoder();
}

FLASHMEM void MacroPerformanceModeWorkflow::setFocusedQuickControlValue(float normalized) {
    const auto item = macro_ui_.focusedQuickControl.get();
    if (item == core::state::macro::MacroQuickControlItem::CC_OFFSET) {
        const int offset = normalizedToOffset(normalized);
        if (macro_ui_.ccOffset.get() == offset) return;
        macro_ui_.ccOffset.set(static_cast<int8_t>(offset));
        return;
    }

    const uint8_t channel =
        static_cast<uint8_t>(input_utils::normalizedToIndex(std::clamp(normalized, 0.0f, 1.0f), 16));
    if (macro_ui_.quickControlGlobalChannel.get() == channel) return;
    macro_ui_.quickControlGlobalChannel.set(channel);
}

FLASHMEM void MacroPerformanceModeWorkflow::navigateProperty(float delta) {
    if (!nav::hasTurnDelta(delta)) return;

    const auto currentProperty = macro_ui_.activeProperty.get();
    const int current = core::state::macro::performancePropertyIndex(currentProperty);
    const int next = nav::nextWrappedIndex(delta, current, 3);
    const auto nextProperty = core::state::macro::performancePropertyAtIndex(next);

    if (currentProperty == core::state::macro::MacroPerformanceProperty::CHANNEL &&
        nextProperty != core::state::macro::MacroPerformanceProperty::CHANNEL) {
        commitClutchChannelPreview();
    }

    if (nextProperty == core::state::macro::MacroPerformanceProperty::CHANNEL) {
        initializeClutchChannelPreview();
    }

    macro_ui_.activeProperty.set(nextProperty);
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::refreshEncoders() {
    configureMacroEncoders();
}

FLASHMEM void MacroPerformanceModeWorkflow::configureMacroEncoders() {
    if (macro_ui_.quickControlsSelecting.get()) {
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
        if (property == core::state::macro::MacroPerformanceProperty::CHANNEL &&
            macro_ui_.clutchActive.get()) {
            encoders_.setPosition(
                Config::MACRO_ENCODERS[i],
                input_utils::indexToNormalized(macro_ui_.clutchPreviewTrackChannel.get(), 16)
            );
            continue;
        }
        encoders_.setPosition(Config::MACRO_ENCODERS[i], normalizedForProperty(services_, i, property));
    }
}

FLASHMEM void MacroPerformanceModeWorkflow::configureValueEncoders() {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        configureNormalizedEncoder(Config::MACRO_ENCODERS[i]);
        encoders_.setContinuous(Config::MACRO_ENCODERS[i]);
    }
}

FLASHMEM void MacroPerformanceModeWorkflow::configureDiscreteEncoders(uint8_t discreteSteps) {
    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        configureDiscreteEncoder(Config::MACRO_ENCODERS[i], discreteSteps);
    }
}

FLASHMEM void MacroPerformanceModeWorkflow::configureQuickControlEncoder() {
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
        input_utils::indexToNormalized(macro_ui_.quickControlGlobalChannel.get(), 16)
    );
}

FLASHMEM void MacroPerformanceModeWorkflow::resetQuickControlsState() {
    macro_ui_.focusedQuickControl.set(core::state::macro::MacroQuickControlItem::GLOBAL_CHANNEL);
    macro_ui_.quickControlGlobalChannel.set(0);
    macro_ui_.ccOffset.set(0);
}

FLASHMEM void MacroPerformanceModeWorkflow::configureNormalizedEncoder(Config::EncoderID id) {
    encoders_.setDiscreteTicksPerStep(id, input_utils::DEFAULT_DISCRETE_TICKS_PER_STEP);
    encoders_.setNormalizedTurns(id, input_utils::DEFAULT_NORMALIZED_TURNS);
}

FLASHMEM void MacroPerformanceModeWorkflow::configureDiscreteEncoder(
    Config::EncoderID id,
    uint8_t discreteSteps
) {
    configureNormalizedEncoder(id);
    encoders_.setDiscreteSteps(id, discreteSteps);
}

FLASHMEM int MacroPerformanceModeWorkflow::currentCcOffsetMin() const {
    int minOffset = -127;
    for (const auto& config : quick_snapshot_configs_) {
        minOffset = std::max(minOffset, -static_cast<int>(config.cc));
    }
    return minOffset;
}

FLASHMEM int MacroPerformanceModeWorkflow::currentCcOffsetMax() const {
    int maxOffset = 127;
    for (const auto& config : quick_snapshot_configs_) {
        maxOffset = std::min(maxOffset, 127 - static_cast<int>(config.cc));
    }
    return maxOffset;
}

FLASHMEM float MacroPerformanceModeWorkflow::offsetToNormalized(int offset) const {
    const int minOffset = currentCcOffsetMin();
    const int maxOffset = currentCcOffsetMax();
    if (maxOffset <= minOffset) return 0.5f;
    const int clamped = std::clamp(offset, minOffset, maxOffset);
    return static_cast<float>(clamped - minOffset) /
           static_cast<float>(maxOffset - minOffset);
}

FLASHMEM int MacroPerformanceModeWorkflow::normalizedToOffset(float normalized) const {
    const int minOffset = currentCcOffsetMin();
    const int maxOffset = currentCcOffsetMax();
    if (maxOffset <= minOffset) return 0;
    const int itemCount = (maxOffset - minOffset) + 1;
    const int index = input_utils::normalizedToIndex(std::clamp(normalized, 0.0f, 1.0f), itemCount);
    return minOffset + index;
}

FLASHMEM void MacroPerformanceModeWorkflow::initializeClutchChannelPreview() {
    macro_ui_.clutchPreviewTrackChannel.set(services_.activeTrackChannel());
}

FLASHMEM void MacroPerformanceModeWorkflow::commitClutchChannelPreview() {
    if (!macro_ui_.clutchActive.get()) return;
    if (macro_ui_.activeProperty.get() != core::state::macro::MacroPerformanceProperty::CHANNEL) return;
    services_.setTrackChannel(macro_ui_.clutchPreviewTrackChannel.get());
}

}  // namespace core::handler

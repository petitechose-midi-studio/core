#include "handler/macro/MacroMidiCcRuntimeAdapter.hpp"

#include "midi/MidiUtils.hpp"

namespace core::handler {

namespace {

constexpr uint16_t macroBit(uint8_t index) {
    return static_cast<uint16_t>(1U << index);
}

}  // namespace

MacroMidiCcRuntimeAdapter::MacroMidiCcRuntimeAdapter(
    StateRefs state,
    MacroPerformanceDomainServices services,
    MidiCcRuntimeAggregator& aggregator
)
    : pages_(state.pages)
    , macro_ui_(state.macroUi)
    , services_(services)
    , aggregator_(aggregator) {}

void MacroMidiCcRuntimeAdapter::beginComputedFrame() {
    computed_valid_mask_ = 0;
}

bool MacroMidiCcRuntimeAdapter::setComputedValue(uint8_t macroIndex, uint8_t value) {
    if (macroIndex >= computed_values_.size() || value > 127U) return false;
    computed_values_[macroIndex] = value;
    computed_valid_mask_ = static_cast<uint16_t>(
        computed_valid_mask_ | macroBit(macroIndex)
    );
    return true;
}

void MacroMidiCcRuntimeAdapter::clearComputedValues() {
    computed_valid_mask_ = 0;
}

MidiCcRuntimePublishResult MacroMidiCcRuntimeAdapter::publishComputedFrame() {
    return publishFrame_(
        core::state::shared::MidiCcResolutionMode::LIVE,
        NO_TRANSIENT_LIVE_MACRO,
        0
    );
}

MidiCcRuntimePublishResult MacroMidiCcRuntimeAdapter::publishLiveManual(
    uint8_t macroIndex,
    uint8_t value
) {
    if (macroIndex >= core::state::macro::MACRO_COUNT || value > 127U) {
        return MidiCcRuntimePublishResult{};
    }
    return publishFrame_(core::state::shared::MidiCcResolutionMode::LIVE, macroIndex, value);
}

MidiCcRuntimePublishResult MacroMidiCcRuntimeAdapter::publishPreview() {
    return publishFrame_(
        core::state::shared::MidiCcResolutionMode::PREVIEW,
        NO_TRANSIENT_LIVE_MACRO,
        0
    );
}

MidiCcRuntimePublishResult MacroMidiCcRuntimeAdapter::publishFrame_(
    core::state::shared::MidiCcResolutionMode mode,
    uint8_t transientLiveMacro,
    uint8_t transientLiveValue
) {
    aggregator_.beginFrame(mode);

    if (services_.isActivePageEnabled()) {
        const uint8_t track = pages_.currentActiveTrack();
        const uint8_t page = pages_.currentActivePage();
        for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
            if (!services_.isMacroSlotActive(i)) continue;

            const auto& config = services_.activeConfig(i);
            const auto destination = core::state::shared::MidiCcDestination{
                .identity = core::state::shared::MidiCcDestinationIdentity{
                    .port = aggregator_.outputPort(),
                    .channel = config.channel,
                    .controller = config.cc,
                },
                .routeValidity = core::state::shared::MidiCcRouteValidity::VALID,
            };
            const uint16_t address = stableAddress(track, page, i);
            const bool transientLive = i == transientLiveMacro;
            const bool recording = services_.automationRecordingActiveFor(i);
            const bool manualOverride = services_.automationManualOverrideActiveFor(i);
            const bool manual = transientLive || recording || manualOverride;
            const bool automation = services_.automationActiveFor(i);
            const bool computedValid =
                (computed_valid_mask_ & macroBit(i)) != 0;

            if (manual) {
                const uint8_t manualValue = transientLive
                    ? transientLiveValue
                    : core::midi::toCC(services_.runtimeValue(i));
                (void)aggregator_.addLiveManual(destination, address, manualValue);

                // A recording replaces the old lane entirely. A Manual
                // override keeps the computed local contribution visible as a
                // deterministic loser for conflict/detail telemetry.
                if (manualOverride && !recording && automation && computedValid) {
                    (void)aggregator_.addMacroComputed(
                        destination,
                        address,
                        computed_values_[i]
                    );
                }
                continue;
            }

            if (automation) {
                if (computedValid) {
                    (void)aggregator_.addMacroComputed(
                        destination,
                        address,
                        computed_values_[i]
                    );
                }
                continue;
            }

            (void)aggregator_.addMacroStatic(
                destination,
                address,
                core::midi::toCC(pages_.activePageData().values[i])
            );
        }
    }

    auto result = aggregator_.publish();
    if (result.ok() && result.sentCount > 0) {
        services_.pulseCcOut();
    }
    return result;
}

}  // namespace core::handler

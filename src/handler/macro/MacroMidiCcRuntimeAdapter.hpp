#pragma once

#include <array>
#include <cstdint>

#include "handler/common/MidiCcRuntimeAggregator.hpp"
#include "handler/macro/MacroPerformanceDomainServices.hpp"
#include "state/macro/MacroConstants.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"

namespace core::handler {

/**
 * Macro-domain adapter for the shared Gate 6 destination aggregator.
 *
 * It rebuilds one complete active-page Macro frame for every publication so
 * duplicate destinations are resolved together. Computed values are staged by
 * MacroAutomationPlaybackService; Manual keeps both its live value and the
 * staged computed contribution visible when an automation lane is overridden.
 */
class MacroMidiCcRuntimeAdapter final {
public:
    struct StateRefs {
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
    };

    MacroMidiCcRuntimeAdapter(
        StateRefs state,
        MacroPerformanceDomainServices services,
        MidiCcRuntimeAggregator& aggregator
    );

    MacroMidiCcRuntimeAdapter(const MacroMidiCcRuntimeAdapter&) = delete;
    MacroMidiCcRuntimeAdapter& operator=(const MacroMidiCcRuntimeAdapter&) = delete;

    /** Starts a bounded automation evaluation frame. */
    void beginComputedFrame();
    bool setComputedValue(uint8_t macroIndex, uint8_t value);
    void clearComputedValues();

    /** Publishes the staged automation frame as Live runtime output. */
    MidiCcRuntimePublishResult publishComputedFrame();

    /** Publishes one immediate encoder/manual movement with highest priority. */
    MidiCcRuntimePublishResult publishLiveManual(uint8_t macroIndex, uint8_t value);

    /** Publishes identical source/winner facts without MIDI side effects. */
    MidiCcRuntimePublishResult publishPreview();

    [[nodiscard]] const core::state::shared::MidiCcResolutionTelemetry& telemetry() const {
        return aggregator_.telemetry();
    }

    static constexpr uint16_t stableAddress(
        uint8_t track,
        uint8_t page,
        uint8_t macroIndex
    ) {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(track) * core::state::macro::PAGE_COUNT + page) *
                core::state::macro::MACRO_COUNT +
            macroIndex
        );
    }

private:
    static constexpr uint8_t NO_TRANSIENT_LIVE_MACRO = 0xFF;

    MidiCcRuntimePublishResult publishFrame_(
        core::state::shared::MidiCcResolutionMode mode,
        uint8_t transientLiveMacro,
        uint8_t transientLiveValue
    );

    core::state::macro::MacroPagesState& pages_;
    core::state::macro::MacroUiState& macro_ui_;
    MacroPerformanceDomainServices services_;
    MidiCcRuntimeAggregator& aggregator_;
    std::array<uint8_t, core::state::macro::MACRO_COUNT> computed_values_{};
    uint16_t computed_valid_mask_ = 0;
};

static_assert(
    MacroMidiCcRuntimeAdapter::stableAddress(
        core::state::macro::TRACK_COUNT - 1U,
        core::state::macro::PAGE_COUNT - 1U,
        core::state::macro::MACRO_COUNT - 1U
    ) < 0xFFFFU
);

}  // namespace core::handler

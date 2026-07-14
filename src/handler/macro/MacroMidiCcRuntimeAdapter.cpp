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
    MidiCcGlobalFrameCoordinator& coordinator
)
    : pages_(state.pages)
    , macro_ui_(state.macroUi)
    , services_(services)
    , coordinator_(coordinator) {}

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

MidiCcGlobalFrameResult MacroMidiCcRuntimeAdapter::publishComputedFrame() {
    return publishFrame_(
        NO_TRANSIENT_LIVE_MACRO,
        0
    );
}

MidiCcGlobalFrameResult MacroMidiCcRuntimeAdapter::publishLiveManual(
    uint8_t macroIndex,
    uint8_t value
) {
    if (macroIndex >= core::state::macro::MACRO_COUNT || value > 127U) {
        return MidiCcGlobalFrameResult{};
    }
    return publishFrame_(macroIndex, value);
}

MidiCcGlobalFrameResult MacroMidiCcRuntimeAdapter::publishFrame_(
    uint8_t transientLiveMacro,
    uint8_t transientLiveValue
) {
    uint16_t candidateCount = 0;
    const auto appendCandidate = [this, &candidateCount](
        core::state::shared::MidiCcCandidateClass candidateClass,
        const core::state::shared::MidiCcDestination& destination,
        uint16_t address,
        uint8_t value
    ) {
        if (candidateCount >= candidates_.size()) return false;
        candidates_[candidateCount++] = core::state::shared::MidiCcCandidate{
            .destination = destination,
            .author = core::state::shared::MidiCcAuthor{
                .candidateClass = candidateClass,
                .stableAddress = address,
            },
            .localValue = value,
        };
        return true;
    };

    if (services_.isActivePageEnabled()) {
        const uint8_t track = pages_.currentActiveTrack();
        const uint8_t page = pages_.currentActivePage();
        for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
            if (!services_.isMacroSlotActive(i)) continue;

            const auto& config = services_.activeConfig(i);
            const auto destination = core::state::shared::MidiCcDestination{
                .identity = core::state::shared::MidiCcDestinationIdentity{
                    .port = MidiCcGlobalFrameCoordinator::OUTPUT_PORT,
                    .channel = config.channel,
                    .controller = config.cc,
                },
                .routeValidity = core::state::shared::MidiCcRouteValidity::VALID,
            };
            const uint16_t address = stableAddress(track, page, i);
            const bool transientLive = i == transientLiveMacro;
            const bool recording = services_.automationRecordingActiveFor(i);
            const bool manualOverride = services_.manualOverrideActiveFor(i);
            const bool manual = transientLive || recording || manualOverride;
            const bool computedSource = services_.computedSourcePlaybackActiveFor(i);
            const bool computedValid =
                (computed_valid_mask_ & macroBit(i)) != 0;

            if (manual) {
                float stableManualValue = services_.runtimeValue(i);
                if (manualOverride && !recording) {
                    (void)services_.manualOverrideValueFor(i, stableManualValue);
                }
                const uint8_t manualValue = transientLive
                    ? transientLiveValue
                    : core::midi::toCC(stableManualValue);
                if (!appendCandidate(
                        core::state::shared::MidiCcCandidateClass::LIVE_MANUAL,
                        destination,
                        address,
                        manualValue
                    )) return MidiCcGlobalFrameResult{};

                // A recording replaces the old lane entirely. A Manual
                // override keeps the computed local contribution visible as a
                // deterministic loser for conflict/detail telemetry.
                if (manualOverride && !recording && computedSource && computedValid) {
                    if (!appendCandidate(
                        core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED,
                        destination,
                        address,
                        computed_values_[i]
                    )) return MidiCcGlobalFrameResult{};
                }
                continue;
            }

            if (computedSource) {
                if (computedValid) {
                    if (!appendCandidate(
                        core::state::shared::MidiCcCandidateClass::MACRO_COMPUTED,
                        destination,
                        address,
                        computed_values_[i]
                    )) return MidiCcGlobalFrameResult{};
                }
                continue;
            }

            if (!appendCandidate(
                core::state::shared::MidiCcCandidateClass::MACRO_STATIC,
                destination,
                address,
                core::midi::toCC(pages_.activePageData().values[i])
            )) return MidiCcGlobalFrameResult{};
        }
    }

    if (!coordinator_.publishPersistentAuthors(candidates_.data(), candidateCount)) {
        return MidiCcGlobalFrameResult{};
    }
    return {
        .status = MidiCcGlobalFrameStatus::OK,
        .resolveStatus = core::state::shared::MidiCcResolveStatus::OK,
        .candidateCount = candidateCount,
    };
}

}  // namespace core::handler

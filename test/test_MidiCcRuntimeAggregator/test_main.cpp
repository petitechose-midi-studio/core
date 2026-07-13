#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IMidi.hpp>
#include <oc/type/Result.hpp>

#include "handler/common/MidiCcRuntimeAggregator.hpp"

namespace {

using core::handler::MidiCcRuntimeAggregator;
using core::state::shared::MidiCcCandidateClass;
using core::state::shared::MidiCcDestination;
using core::state::shared::MidiCcDestinationIdentity;
using core::state::shared::MidiCcResolutionMode;
using core::state::shared::MidiCcResolutionTelemetry;
using core::state::shared::MidiCcResolveStatus;
using core::state::shared::MidiCcRouteValidity;

class MockMidiTransport final : public oc::interface::IMidi {
public:
    struct CcMessage {
        uint8_t channel = 0;
        uint8_t controller = 0;
        uint8_t value = 0;
    };

    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update() override {}
    void sendCC(uint8_t channel, uint8_t cc, uint8_t value) override {
        assert(messageCount < messages.size());
        messages[messageCount++] = CcMessage{channel, cc, value};
    }
    void sendNoteOn(uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t, uint8_t) override {}
    void sendSysEx(const uint8_t*, std::size_t) override {}
    void sendProgramChange(uint8_t, uint8_t) override {}
    void sendPitchBend(uint8_t, int16_t) override {}
    void sendChannelPressure(uint8_t, uint8_t) override {}
    void sendClock() override {}
    void sendStart() override {}
    void sendStop() override {}
    void sendContinue() override {}
    void setOnCC(CCCallback) override {}
    void setOnNoteOn(NoteCallback) override {}
    void setOnNoteOff(NoteCallback) override {}
    void setOnSysEx(SysExCallback) override {}
    void setOnClock(ClockCallback) override {}
    void setOnStart(RealtimeCallback) override {}
    void setOnStop(RealtimeCallback) override {}
    void setOnContinue(RealtimeCallback) override {}

    std::array<CcMessage, 32> messages{};
    uint16_t messageCount = 0;
};

MidiCcDestination destination(
    uint8_t channel = 4,
    uint8_t controller = 74,
    MidiCcRouteValidity routeValidity = MidiCcRouteValidity::VALID,
    uint8_t port = MidiCcRuntimeAggregator::DEFAULT_OUTPUT_PORT
) {
    return MidiCcDestination{
        .identity = MidiCcDestinationIdentity{
            .port = port,
            .channel = channel,
            .controller = controller,
        },
        .routeValidity = routeValidity,
    };
}

void test_concurrent_macro_sources_emit_one_deterministic_winner() {
    MockMidiTransport transport;
    oc::api::MidiAPI midi(transport);
    MidiCcRuntimeAggregator aggregator(midi);

    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addMacroStatic(destination(), 7, 97) == MidiCcResolveStatus::OK);
    assert(aggregator.addMacroStatic(destination(), 2, 22) == MidiCcResolveStatus::OK);
    const auto result = aggregator.publish();

    assert(result.ok());
    assert(result.candidateCount == 2);
    assert(result.destinationCount == 1);
    assert(result.conflictCount == 1);
    assert(result.eligibleEmissionCount == 1);
    assert(result.sentCount == 1);
    assert(transport.messageCount == 1);
    assert(transport.messages[0].channel == 4);
    assert(transport.messages[0].controller == 74);
    assert(transport.messages[0].value == 22);

    const auto& telemetry = aggregator.telemetry();
    assert(telemetry.destinations[0].winner.author.candidateClass ==
           MidiCcCandidateClass::MACRO_STATIC);
    assert(telemetry.destinations[0].winner.author.stableAddress == 2);
    assert(telemetry.destinations[0].finalValue == 22);
    assert(telemetry.destinations[0].loserCount == 1);
    assert(telemetry.losers[0].author.stableAddress == 7);
    assert(telemetry.losers[0].localValue == 97);

    std::cout << "[PASS] test_concurrent_macro_sources_emit_one_deterministic_winner\n";
}

void test_live_manual_priority_beats_reserved_lane_and_macro_sources() {
    MockMidiTransport transport;
    oc::api::MidiAPI midi(transport);
    MidiCcRuntimeAggregator aggregator(midi);

    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addMacroStatic(destination(), 1, 10) == MidiCcResolveStatus::OK);
    assert(aggregator.addMacroComputed(destination(), 2, 20) == MidiCcResolveStatus::OK);
    assert(aggregator.addSequencerCcLane(destination(), 3, 30) == MidiCcResolveStatus::OK);
    assert(aggregator.addLiveManual(destination(), 4, 40) == MidiCcResolveStatus::OK);
    const auto result = aggregator.publish();

    assert(result.ok());
    assert(result.sentCount == 1);
    assert(transport.messageCount == 1);
    assert(transport.messages[0].value == 40);

    const auto& telemetry = aggregator.telemetry();
    const auto& resolved = telemetry.destinations[0];
    assert(resolved.winner.author.candidateClass == MidiCcCandidateClass::LIVE_MANUAL);
    assert(resolved.finalValue == 40);
    assert(resolved.loserCount == 3);
    assert(telemetry.losers[0].author.candidateClass ==
           MidiCcCandidateClass::SEQUENCER_CC_LANE);
    assert(telemetry.losers[1].author.candidateClass ==
           MidiCcCandidateClass::MACRO_COMPUTED);
    assert(telemetry.losers[2].author.candidateClass ==
           MidiCcCandidateClass::MACRO_STATIC);

    std::cout << "[PASS] test_live_manual_priority_beats_reserved_lane_and_macro_sources\n";
}

void test_no_route_winner_is_visible_and_never_falls_through() {
    MockMidiTransport transport;
    oc::api::MidiAPI midi(transport);
    MidiCcRuntimeAggregator aggregator(midi);

    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addLiveManual(
        destination(4, 74, MidiCcRouteValidity::NO_ROUTE),
        8,
        101
    ) == MidiCcResolveStatus::OK);
    assert(aggregator.addMacroComputed(destination(), 1, 64) == MidiCcResolveStatus::OK);
    const auto result = aggregator.publish();

    assert(result.ok());
    assert(result.sentCount == 0);
    assert(result.eligibleEmissionCount == 0);
    assert(result.noRouteCount == 1);
    assert(transport.messageCount == 0);

    const auto& telemetry = aggregator.telemetry();
    const auto& resolved = telemetry.destinations[0];
    assert(resolved.winner.author.candidateClass == MidiCcCandidateClass::LIVE_MANUAL);
    assert(resolved.winner.routeValidity == MidiCcRouteValidity::NO_ROUTE);
    assert(resolved.finalValue == 101);
    assert(!resolved.shouldEmit);
    assert(resolved.conflict);
    assert(telemetry.losers[0].author.candidateClass ==
           MidiCcCandidateClass::MACRO_COMPUTED);

    // Returning to a valid route must reclaim the destination even if its
    // value happens to match a value sent before No route.
    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addMacroComputed(destination(), 1, 64) == MidiCcResolveStatus::OK);
    const auto restored = aggregator.publish();
    assert(restored.ok());
    assert(restored.sentCount == 1);
    assert(transport.messageCount == 1);
    assert(transport.messages[0].value == 64);

    std::cout << "[PASS] test_no_route_winner_is_visible_and_never_falls_through\n";
}

void test_preview_publishes_same_facts_without_midi_or_live_cache_mutation() {
    MockMidiTransport transport;
    oc::api::MidiAPI midi(transport);
    MidiCcRuntimeAggregator aggregator(midi);

    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addMacroStatic(destination(), 2, 20) == MidiCcResolveStatus::OK);
    assert(aggregator.publish().sentCount == 1);

    aggregator.beginFrame(MidiCcResolutionMode::PREVIEW);
    assert(aggregator.addMacroComputed(destination(), 2, 80) == MidiCcResolveStatus::OK);
    const auto preview = aggregator.publish();
    assert(preview.ok());
    assert(preview.sentCount == 0);
    assert(preview.eligibleEmissionCount == 0);
    assert(transport.messageCount == 1);
    assert(aggregator.telemetry().mode == MidiCcResolutionMode::PREVIEW);
    assert(aggregator.telemetry().destinations[0].finalValue == 80);
    assert(!aggregator.telemetry().destinations[0].shouldEmit);

    // Preview did not pretend to send 80, so the next Live frame must emit it.
    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addMacroComputed(destination(), 2, 80) == MidiCcResolveStatus::OK);
    const auto live = aggregator.publish();
    assert(live.sentCount == 1);
    assert(transport.messageCount == 2);
    assert(transport.messages[1].value == 80);

    std::cout << "[PASS] test_preview_publishes_same_facts_without_midi_or_live_cache_mutation\n";
}

void test_midi_non_regression_suppresses_only_unchanged_live_destinations() {
    MockMidiTransport transport;
    oc::api::MidiAPI midi(transport);
    MidiCcRuntimeAggregator aggregator(midi);

    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addMacroStatic(destination(3, 10), 1, 64) == MidiCcResolveStatus::OK);
    auto result = aggregator.publish();
    assert(result.sentCount == 1);
    assert(transport.messages[0].channel == 3);
    assert(transport.messages[0].controller == 10);
    assert(transport.messages[0].value == 64);

    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addMacroStatic(destination(3, 10), 1, 64) == MidiCcResolveStatus::OK);
    result = aggregator.publish();
    assert(result.eligibleEmissionCount == 1);
    assert(result.sentCount == 0);
    assert(transport.messageCount == 1);

    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addMacroStatic(destination(3, 10), 1, 65) == MidiCcResolveStatus::OK);
    assert(aggregator.addMacroStatic(destination(9, 71), 2, 99) == MidiCcResolveStatus::OK);
    result = aggregator.publish();
    assert(result.sentCount == 2);
    assert(transport.messageCount == 3);
    assert(transport.messages[1].channel == 3);
    assert(transport.messages[1].controller == 10);
    assert(transport.messages[1].value == 65);
    assert(transport.messages[2].channel == 9);
    assert(transport.messages[2].controller == 71);
    assert(transport.messages[2].value == 99);

    // A disappeared author emits no reset. Its later return is a fresh claim.
    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    result = aggregator.publish();
    assert(result.sentCount == 0);
    assert(transport.messageCount == 3);

    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addMacroStatic(destination(3, 10), 1, 65) == MidiCcResolveStatus::OK);
    result = aggregator.publish();
    assert(result.sentCount == 1);
    assert(transport.messageCount == 4);

    std::cout << "[PASS] test_midi_non_regression_suppresses_only_unchanged_live_destinations\n";
}

void test_failed_frame_keeps_previous_telemetry_and_emits_nothing() {
    MockMidiTransport transport;
    oc::api::MidiAPI midi(transport);
    MidiCcRuntimeAggregator aggregator(midi);

    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addMacroStatic(destination(), 1, 11) == MidiCcResolveStatus::OK);
    assert(aggregator.publish().sentCount == 1);

    std::array<unsigned char, sizeof(MidiCcResolutionTelemetry)> before{};
    std::memcpy(before.data(), &aggregator.telemetry(), sizeof(aggregator.telemetry()));
    const uint16_t messageCountBefore = transport.messageCount;

    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    for (uint16_t i = 0; i < MidiCcResolutionTelemetry::MAX_CANDIDATES; ++i) {
        assert(aggregator.addMacroStatic(
            destination(),
            i,
            static_cast<uint8_t>(i % 128U)
        ) == MidiCcResolveStatus::OK);
    }
    assert(aggregator.addMacroStatic(destination(), 400, 1) ==
           MidiCcResolveStatus::CAPACITY_EXCEEDED);
    const auto overflow = aggregator.publish();
    assert(overflow.status == MidiCcResolveStatus::CAPACITY_EXCEEDED);
    assert(transport.messageCount == messageCountBefore);
    assert(std::memcmp(
        before.data(),
        &aggregator.telemetry(),
        sizeof(aggregator.telemetry())
    ) == 0);

    // A bound single-port aggregator refuses another valid physical port as
    // one failed batch, rather than publishing telemetry it cannot emit.
    aggregator.beginFrame(MidiCcResolutionMode::LIVE);
    assert(aggregator.addMacroStatic(destination(4, 74), 1, 12) ==
           MidiCcResolveStatus::OK);
    assert(aggregator.addMacroStatic(destination(4, 74, MidiCcRouteValidity::VALID, 1), 2, 13) ==
           MidiCcResolveStatus::INVALID_INPUT);
    const auto wrongPort = aggregator.publish();
    assert(wrongPort.status == MidiCcResolveStatus::INVALID_INPUT);
    assert(transport.messageCount == messageCountBefore);
    assert(std::memcmp(
        before.data(),
        &aggregator.telemetry(),
        sizeof(aggregator.telemetry())
    ) == 0);

    std::cout << "[PASS] test_failed_frame_keeps_previous_telemetry_and_emits_nothing\n";
}

}  // namespace

int main() {
    test_concurrent_macro_sources_emit_one_deterministic_winner();
    test_live_manual_priority_beats_reserved_lane_and_macro_sources();
    test_no_route_winner_is_visible_and_never_falls_through();
    test_preview_publishes_same_facts_without_midi_or_live_cache_mutation();
    test_midi_non_regression_suppresses_only_unchanged_live_destinations();
    test_failed_frame_keeps_previous_telemetry_and_emits_nothing();

    std::cout << "\nAll MidiCcRuntimeAggregator tests passed.\n";
    return 0;
}

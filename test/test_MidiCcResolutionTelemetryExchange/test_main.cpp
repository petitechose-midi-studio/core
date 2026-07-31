#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <iostream>
#include <utility>

#include "sequencer/MidiCcResolutionTelemetryExchange.hpp"

namespace {

using core::sequencer::MidiCcResolutionTelemetryExchange;

void publishCandidateCount(
    MidiCcResolutionTelemetryExchange& exchange,
    uint16_t candidateCount
) {
    const auto lease = exchange.beginWrite();
    assert(lease);
    *lease.telemetry = {};
    lease.telemetry->candidateCount = candidateCount;
    exchange.publish(lease);
}

void test_reader_is_stable_while_writer_publishes_another_frame() {
    MidiCcResolutionTelemetryExchange exchange{};
    publishCandidateCount(exchange, 7U);
    auto first = exchange.read();
    assert(first && first->candidateCount == 7U);
    assert(!exchange.read());

    publishCandidateCount(exchange, 11U);
    assert(first->candidateCount == 7U);

    first = {};
    auto second = exchange.read();
    assert(second && second->candidateCount == 11U);
}

void test_move_transfers_the_exact_reader_lease() {
    MidiCcResolutionTelemetryExchange exchange{};
    publishCandidateCount(exchange, 3U);
    auto source = exchange.read();
    auto destination = std::move(source);
    assert(!source);
    assert(destination && destination->candidateCount == 3U);
    assert(!exchange.read());
    destination = {};
    assert(exchange.read());
}

void test_reset_preserves_a_held_view_and_publishes_zero_after_release() {
    MidiCcResolutionTelemetryExchange exchange{};
    publishCandidateCount(exchange, 19U);
    auto held = exchange.read();
    exchange.reset();
    assert(held && held->candidateCount == 19U);
    assert(!exchange.read());

    held = {};
    auto reset = exchange.read();
    assert(reset);
    assert(reset->candidateCount == 0U);
}

}  // namespace

int main() {
    test_reader_is_stable_while_writer_publishes_another_frame();
    test_move_transfers_the_exact_reader_lease();
    test_reset_preserves_a_held_view_and_publishes_zero_after_release();
    std::cout << "MidiCcResolutionTelemetryExchange tests passed\n";
    return 0;
}

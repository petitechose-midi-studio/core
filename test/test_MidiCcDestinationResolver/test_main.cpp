#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "state/shared/MidiCcDestinationResolver.hpp"

namespace {

using core::state::shared::MidiCcAuthor;
using core::state::shared::MidiCcCandidate;
using core::state::shared::MidiCcCandidateClass;
using core::state::shared::MidiCcContributionTelemetry;
using core::state::shared::MidiCcDestination;
using core::state::shared::MidiCcDestinationIdentity;
using core::state::shared::MidiCcResolutionMode;
using core::state::shared::MidiCcResolutionTelemetry;
using core::state::shared::MidiCcResolveStatus;
using core::state::shared::MidiCcResolvedDestinationTelemetry;
using core::state::shared::MidiCcRouteValidity;
using core::state::shared::midiCcCandidatePriority;
using core::state::shared::resolveMidiCcDestinations;

MidiCcCandidate candidate(
    MidiCcCandidateClass candidateClass,
    uint16_t stableAddress,
    uint8_t localValue,
    uint8_t port = 1,
    uint8_t channel = 4,
    uint8_t controller = 74,
    MidiCcRouteValidity routeValidity = MidiCcRouteValidity::VALID
) {
    return MidiCcCandidate{
        .destination = MidiCcDestination{
            .identity = MidiCcDestinationIdentity{
                .port = port,
                .channel = channel,
                .controller = controller,
            },
            .routeValidity = routeValidity,
        },
        .author = MidiCcAuthor{
            .candidateClass = candidateClass,
            .stableAddress = stableAddress,
        },
        .localValue = localValue,
    };
}

bool sameIdentity(
    const MidiCcDestinationIdentity& lhs,
    const MidiCcDestinationIdentity& rhs
) {
    return lhs.port == rhs.port &&
           lhs.channel == rhs.channel &&
           lhs.controller == rhs.controller;
}

bool sameAuthor(const MidiCcAuthor& lhs, const MidiCcAuthor& rhs) {
    return lhs.candidateClass == rhs.candidateClass &&
           lhs.stableAddress == rhs.stableAddress;
}

bool sameContribution(
    const MidiCcContributionTelemetry& lhs,
    const MidiCcContributionTelemetry& rhs
) {
    return sameAuthor(lhs.author, rhs.author) &&
           lhs.localValue == rhs.localValue &&
           lhs.routeValidity == rhs.routeValidity;
}

bool sameResolved(
    const MidiCcResolvedDestinationTelemetry& lhs,
    const MidiCcResolvedDestinationTelemetry& rhs
) {
    return sameIdentity(lhs.destination.identity, rhs.destination.identity) &&
           lhs.destination.routeValidity == rhs.destination.routeValidity &&
           sameContribution(lhs.winner, rhs.winner) &&
           lhs.firstLoser == rhs.firstLoser &&
           lhs.loserCount == rhs.loserCount &&
           lhs.finalValue == rhs.finalValue &&
           lhs.conflict == rhs.conflict &&
           lhs.shouldEmit == rhs.shouldEmit;
}

bool sameTelemetry(
    const MidiCcResolutionTelemetry& lhs,
    const MidiCcResolutionTelemetry& rhs
) {
    if (lhs.mode != rhs.mode ||
        lhs.candidateCount != rhs.candidateCount ||
        lhs.destinationCount != rhs.destinationCount ||
        lhs.loserCount != rhs.loserCount ||
        lhs.conflictCount != rhs.conflictCount ||
        lhs.emissionCount != rhs.emissionCount ||
        lhs.noRouteCount != rhs.noRouteCount) {
        return false;
    }
    for (uint16_t i = 0; i < lhs.destinationCount; ++i) {
        if (!sameResolved(lhs.destinations[i], rhs.destinations[i])) return false;
    }
    for (uint16_t i = 0; i < lhs.loserCount; ++i) {
        if (!sameContribution(lhs.losers[i], rhs.losers[i])) return false;
    }
    return true;
}

void test_fixed_v1_priority_is_exhaustive() {
    constexpr std::array<MidiCcCandidateClass, 4> classes{
        MidiCcCandidateClass::LIVE_MANUAL,
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        MidiCcCandidateClass::MACRO_COMPUTED,
        MidiCcCandidateClass::MACRO_STATIC,
    };

    for (uint8_t i = 0; i < classes.size(); ++i) {
        assert(midiCcCandidatePriority(classes[i]) == i);
    }

    for (uint8_t firstClass = 0; firstClass < classes.size(); ++firstClass) {
        std::array<MidiCcCandidate, 4> candidates{};
        const uint8_t count = static_cast<uint8_t>(classes.size() - firstClass);
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t classIndex = static_cast<uint8_t>(firstClass + i);
            candidates[i] = candidate(
                classes[classIndex],
                static_cast<uint16_t>(40U - classIndex),
                static_cast<uint8_t>(20U + classIndex)
            );
        }

        MidiCcResolutionTelemetry telemetry;
        assert(resolveMidiCcDestinations(
            candidates.data(),
            count,
            MidiCcResolutionMode::LIVE,
            telemetry
        ) == MidiCcResolveStatus::OK);
        assert(telemetry.destinationCount == 1);
        assert(telemetry.destinations[0].winner.author.candidateClass == classes[firstClass]);
        assert(telemetry.destinations[0].finalValue ==
               static_cast<uint8_t>(20U + firstClass));
        assert(telemetry.destinations[0].loserCount == count - 1U);
        assert(telemetry.destinations[0].conflict == (count > 1U));
        assert(telemetry.destinations[0].shouldEmit);
        for (uint8_t i = 1; i < count; ++i) {
            assert(telemetry.losers[i - 1U].author.candidateClass ==
                   classes[firstClass + i]);
        }
    }

    std::cout << "[PASS] test_fixed_v1_priority_is_exhaustive\n";
}

void test_duplicate_intra_class_uses_lowest_stable_address() {
    constexpr std::array<MidiCcCandidateClass, 4> classes{
        MidiCcCandidateClass::LIVE_MANUAL,
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        MidiCcCandidateClass::MACRO_COMPUTED,
        MidiCcCandidateClass::MACRO_STATIC,
    };

    for (const auto candidateClass : classes) {
        const std::array<MidiCcCandidate, 3> candidates{
            candidate(candidateClass, 9, 90),
            candidate(candidateClass, 2, 20),
            candidate(candidateClass, 5, 50),
        };
        MidiCcResolutionTelemetry telemetry;
        assert(resolveMidiCcDestinations(
            candidates.data(),
            candidates.size(),
            MidiCcResolutionMode::LIVE,
            telemetry
        ) == MidiCcResolveStatus::OK);
        assert(telemetry.destinationCount == 1);
        assert(telemetry.destinations[0].winner.author.stableAddress == 2);
        assert(telemetry.destinations[0].finalValue == 20);
        assert(telemetry.destinations[0].loserCount == 2);
        assert(telemetry.losers[0].author.stableAddress == 5);
        assert(telemetry.losers[1].author.stableAddress == 9);
    }

    std::cout << "[PASS] test_duplicate_intra_class_uses_lowest_stable_address\n";
}

void test_port_channel_and_cc_are_distinct_destination_identity() {
    const std::array<MidiCcCandidate, 4> candidates{
        candidate(MidiCcCandidateClass::MACRO_STATIC, 4, 44, 2, 2, 74),
        candidate(MidiCcCandidateClass::MACRO_STATIC, 3, 33, 1, 3, 74),
        candidate(MidiCcCandidateClass::MACRO_STATIC, 2, 22, 1, 2, 74),
        candidate(MidiCcCandidateClass::MACRO_STATIC, 1, 11, 1, 2, 10),
    };

    MidiCcResolutionTelemetry telemetry;
    assert(resolveMidiCcDestinations(
        candidates.data(),
        candidates.size(),
        MidiCcResolutionMode::LIVE,
        telemetry
    ) == MidiCcResolveStatus::OK);
    assert(telemetry.destinationCount == 4);
    assert(telemetry.emissionCount == 4);
    assert(telemetry.conflictCount == 0);

    assert(sameIdentity(
        telemetry.destinations[0].destination.identity,
        MidiCcDestinationIdentity{1, 2, 10}
    ));
    assert(sameIdentity(
        telemetry.destinations[1].destination.identity,
        MidiCcDestinationIdentity{1, 2, 74}
    ));
    assert(sameIdentity(
        telemetry.destinations[2].destination.identity,
        MidiCcDestinationIdentity{1, 3, 74}
    ));
    assert(sameIdentity(
        telemetry.destinations[3].destination.identity,
        MidiCcDestinationIdentity{2, 2, 74}
    ));

    std::cout << "[PASS] test_port_channel_and_cc_are_distinct_destination_identity\n";
}

void test_no_route_preserves_author_and_never_falls_through() {
    const std::array<MidiCcCandidate, 2> conflict{
        candidate(
            MidiCcCandidateClass::LIVE_MANUAL,
            6,
            101,
            7,
            4,
            74,
            MidiCcRouteValidity::NO_ROUTE
        ),
        candidate(MidiCcCandidateClass::SEQUENCER_CC_LANE, 1, 96, 7, 4, 74),
    };

    MidiCcResolutionTelemetry live;
    assert(resolveMidiCcDestinations(
        conflict.data(),
        conflict.size(),
        MidiCcResolutionMode::LIVE,
        live
    ) == MidiCcResolveStatus::OK);
    assert(live.destinationCount == 1);
    assert(live.destinations[0].winner.author.candidateClass ==
           MidiCcCandidateClass::LIVE_MANUAL);
    assert(live.destinations[0].winner.author.stableAddress == 6);
    assert(live.destinations[0].finalValue == 101);
    assert(!live.destinations[0].shouldEmit);
    assert(live.emissionCount == 0);
    assert(live.noRouteCount == 1);
    assert(live.destinations[0].loserCount == 1);
    assert(live.losers[0].author.candidateClass ==
           MidiCcCandidateClass::SEQUENCER_CC_LANE);

    const MidiCcCandidate unresolved = candidate(
        MidiCcCandidateClass::MACRO_COMPUTED,
        42,
        87,
        MidiCcDestinationIdentity::INVALID_PORT,
        MidiCcDestinationIdentity::INVALID_CHANNEL,
        71,
        MidiCcRouteValidity::NO_ROUTE
    );
    MidiCcResolutionTelemetry noRoute;
    assert(resolveMidiCcDestinations(
        &unresolved,
        1,
        MidiCcResolutionMode::LIVE,
        noRoute
    ) == MidiCcResolveStatus::OK);
    assert(noRoute.destinations[0].destination.identity.port ==
           MidiCcDestinationIdentity::INVALID_PORT);
    assert(noRoute.destinations[0].destination.identity.channel ==
           MidiCcDestinationIdentity::INVALID_CHANNEL);
    assert(noRoute.destinations[0].winner.author.stableAddress == 42);
    assert(noRoute.destinations[0].finalValue == 87);
    assert(!noRoute.destinations[0].shouldEmit);

    const MidiCcCandidate valid = candidate(
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        3,
        64
    );
    MidiCcResolutionTelemetry preview;
    assert(resolveMidiCcDestinations(
        &valid,
        1,
        MidiCcResolutionMode::PREVIEW,
        preview
    ) == MidiCcResolveStatus::OK);
    assert(preview.mode == MidiCcResolutionMode::PREVIEW);
    assert(preview.destinations[0].finalValue == 64);
    assert(!preview.destinations[0].shouldEmit);
    assert(preview.emissionCount == 0);
    assert(preview.noRouteCount == 0);

    std::cout << "[PASS] test_no_route_preserves_author_and_never_falls_through\n";
}

void test_capacity_is_exact_and_failures_publish_no_partial_frame() {
    static std::array<MidiCcCandidate, MidiCcResolutionTelemetry::MAX_CANDIDATES> candidates;
    for (uint16_t i = 0; i < candidates.size(); ++i) {
        candidates[i] = candidate(
            MidiCcCandidateClass::MACRO_STATIC,
            static_cast<uint16_t>(candidates.size() - 1U - i),
            static_cast<uint8_t>(i % 128U)
        );
    }

    MidiCcResolutionTelemetry telemetry;
    assert(resolveMidiCcDestinations(
        candidates.data(),
        candidates.size(),
        MidiCcResolutionMode::LIVE,
        telemetry
    ) == MidiCcResolveStatus::OK);
    assert(telemetry.candidateCount == MidiCcResolutionTelemetry::MAX_CANDIDATES);
    assert(telemetry.destinationCount == 1);
    assert(telemetry.loserCount == MidiCcResolutionTelemetry::MAX_LOSERS);
    assert(telemetry.destinations[0].winner.author.stableAddress == 0);
    for (uint16_t i = 0; i < telemetry.loserCount; ++i) {
        assert(telemetry.losers[i].author.stableAddress == i + 1U);
    }

    for (uint16_t i = 0; i < candidates.size(); ++i) {
        candidates[i] = candidate(
            MidiCcCandidateClass::MACRO_STATIC,
            i,
            static_cast<uint8_t>(i % 128U),
            static_cast<uint8_t>(i / 16U),
            static_cast<uint8_t>(i % 16U),
            74
        );
    }
    assert(resolveMidiCcDestinations(
        candidates.data(),
        candidates.size(),
        MidiCcResolutionMode::LIVE,
        telemetry
    ) == MidiCcResolveStatus::OK);
    assert(telemetry.destinationCount == MidiCcResolutionTelemetry::MAX_DESTINATIONS);
    assert(telemetry.loserCount == 0);
    assert(telemetry.emissionCount == MidiCcResolutionTelemetry::MAX_DESTINATIONS);

    std::array<unsigned char, sizeof(MidiCcResolutionTelemetry)> before{};
    std::memcpy(before.data(), &telemetry, sizeof(telemetry));
    assert(resolveMidiCcDestinations(
        candidates.data(),
        static_cast<std::size_t>(MidiCcResolutionTelemetry::MAX_CANDIDATES) + 1U,
        MidiCcResolutionMode::LIVE,
        telemetry
    ) == MidiCcResolveStatus::CAPACITY_EXCEEDED);
    assert(std::memcmp(before.data(), &telemetry, sizeof(telemetry)) == 0);

    MidiCcCandidate invalid = candidates[0];
    invalid.destination.routeValidity = MidiCcRouteValidity::VALID;
    invalid.destination.identity.channel = MidiCcDestinationIdentity::INVALID_CHANNEL;
    assert(resolveMidiCcDestinations(
        &invalid,
        1,
        MidiCcResolutionMode::LIVE,
        telemetry
    ) == MidiCcResolveStatus::INVALID_INPUT);
    assert(std::memcmp(before.data(), &telemetry, sizeof(telemetry)) == 0);

    invalid = candidates[0];
    invalid.destination.routeValidity = MidiCcRouteValidity::VALID;
    invalid.destination.identity.port = MidiCcDestinationIdentity::INVALID_PORT;
    assert(resolveMidiCcDestinations(
        &invalid,
        1,
        MidiCcResolutionMode::LIVE,
        telemetry
    ) == MidiCcResolveStatus::INVALID_INPUT);
    assert(std::memcmp(before.data(), &telemetry, sizeof(telemetry)) == 0);

    std::cout << "[PASS] test_capacity_is_exact_and_failures_publish_no_partial_frame\n";
}

void test_result_is_independent_of_candidate_insertion_order() {
    const std::array<MidiCcCandidate, 11> forward{
        candidate(MidiCcCandidateClass::MACRO_STATIC, 8, 8, 2, 1, 10),
        candidate(MidiCcCandidateClass::SEQUENCER_CC_LANE, 7, 70, 1, 4, 74),
        candidate(MidiCcCandidateClass::MACRO_COMPUTED, 9, 90, 1, 4, 74),
        candidate(MidiCcCandidateClass::LIVE_MANUAL, 4, 44, 1, 4, 74),
        candidate(MidiCcCandidateClass::LIVE_MANUAL, 2, 22, 1, 4, 74),
        candidate(MidiCcCandidateClass::MACRO_STATIC, 1, 11, 1, 2, 10),
        candidate(MidiCcCandidateClass::MACRO_COMPUTED, 3, 33, 1, 2, 10),
        candidate(MidiCcCandidateClass::SEQUENCER_CC_LANE, 5, 55, 2, 1, 10),
        candidate(MidiCcCandidateClass::MACRO_STATIC, 6, 66, 1, 3, 7),
        candidate(MidiCcCandidateClass::MACRO_STATIC, 12, 99, 3, 5, 71),
        candidate(MidiCcCandidateClass::MACRO_STATIC, 12, 12, 3, 5, 71),
    };
    std::array<MidiCcCandidate, forward.size()> reverse{};
    for (std::size_t i = 0; i < forward.size(); ++i) {
        reverse[i] = forward[forward.size() - 1U - i];
    }

    MidiCcResolutionTelemetry lhs;
    MidiCcResolutionTelemetry rhs;
    assert(resolveMidiCcDestinations(
        forward.data(),
        forward.size(),
        MidiCcResolutionMode::LIVE,
        lhs
    ) == MidiCcResolveStatus::OK);
    assert(resolveMidiCcDestinations(
        reverse.data(),
        reverse.size(),
        MidiCcResolutionMode::LIVE,
        rhs
    ) == MidiCcResolveStatus::OK);
    assert(sameTelemetry(lhs, rhs));
    assert(lhs.destinationCount == 5);
    assert(lhs.destinations[0].winner.author.candidateClass ==
           MidiCcCandidateClass::MACRO_COMPUTED);
    assert(lhs.destinations[2].winner.author.candidateClass ==
           MidiCcCandidateClass::LIVE_MANUAL);
    assert(lhs.destinations[2].winner.author.stableAddress == 2);
    assert(lhs.destinations[4].winner.author.stableAddress == 12);
    assert(lhs.destinations[4].finalValue == 12);

    std::cout << "[PASS] test_result_is_independent_of_candidate_insertion_order\n";
}

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);

    test_fixed_v1_priority_is_exhaustive();
    test_duplicate_intra_class_uses_lowest_stable_address();
    test_port_channel_and_cc_are_distinct_destination_identity();
    test_no_route_preserves_author_and_never_falls_through();
    test_capacity_is_exact_and_failures_publish_no_partial_frame();
    test_result_is_independent_of_candidate_insertion_order();

    std::cout << "All MidiCcDestinationResolver tests passed\n";
    return 0;
}

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace core::state::shared {

/**
 * Stable classic-MIDI destination identity.
 *
 * Port remains part of the domain even when a single-port product UI hides
 * it. Channel is zero-based (0..15) and controller is classic CC (0..127).
 */
struct MidiCcDestinationIdentity {
    static constexpr uint8_t INVALID_PORT = 0xFF;
    static constexpr uint8_t INVALID_CHANNEL = 0xFF;

    uint8_t port = 0;
    uint8_t channel = 0;
    uint8_t controller = 0;
};

enum class MidiCcRouteValidity : uint8_t {
    VALID = 0,
    NO_ROUTE,
};

struct MidiCcDestination {
    MidiCcDestinationIdentity identity{};
    MidiCcRouteValidity routeValidity = MidiCcRouteValidity::NO_ROUTE;
};

/** Fixed ADR-0037 V1 priority, from highest to lowest. */
enum class MidiCcCandidateClass : uint8_t {
    LIVE_MANUAL = 0,
    SEQUENCER_CC_LANE,
    MACRO_COMPUTED,
    MACRO_STATIC,
};

/**
 * Stable author identity inside one candidate class.
 *
 * Adapters encode their durable Track/Lane or Track/Page/Slot address here.
 * Lower addresses win duplicate candidates inside the same class.
 */
struct MidiCcAuthor {
    MidiCcCandidateClass candidateClass = MidiCcCandidateClass::MACRO_STATIC;
    uint16_t stableAddress = 0;
};

struct MidiCcCandidate {
    MidiCcDestination destination{};
    MidiCcAuthor author{};
    uint8_t localValue = 0;
};

enum class MidiCcResolutionMode : uint8_t {
    PREVIEW = 0,
    LIVE,
};

enum class MidiCcResolveStatus : uint8_t {
    OK = 0,
    INVALID_INPUT,
    CAPACITY_EXCEEDED,
};

/** Display/runtime facts for one winning or losing author. */
struct MidiCcContributionTelemetry {
    MidiCcAuthor author{};
    uint8_t localValue = 0;
    MidiCcRouteValidity routeValidity = MidiCcRouteValidity::NO_ROUTE;
};

/**
 * Exactly one final value for one destination identity.
 *
 * Losers occupy telemetry.losers[firstLoser..firstLoser+loserCount). The
 * winner's route controls emission: NO_ROUTE never falls through to a lower
 * priority author, so authorship and conflict policy remain truthful.
 */
struct MidiCcResolvedDestinationTelemetry {
    MidiCcDestination destination{};
    MidiCcContributionTelemetry winner{};
    uint16_t firstLoser = 0;
    uint16_t loserCount = 0;
    uint8_t finalValue = 0;
    bool conflict = false;
    bool shouldEmit = false;
};

/**
 * Bounded POD frame shared by Preview, Live runtime, UI, and UX telemetry.
 *
 * Capacity covers the full V1 product envelope:
 * 16 Tracks * (8 Macro sources + 8 simultaneous Live/Manual overrides
 * + 4 Sequencer CC lanes). Computed and static Macro candidates are mutually
 * exclusive for one Slot; an override may coexist for conflict telemetry.
 */
struct MidiCcResolutionTelemetry {
    static constexpr uint16_t MAX_CANDIDATES = 320;
    static constexpr uint16_t MAX_DESTINATIONS = MAX_CANDIDATES;
    static constexpr uint16_t MAX_LOSERS = MAX_CANDIDATES - 1U;

    MidiCcResolutionMode mode = MidiCcResolutionMode::PREVIEW;
    uint16_t candidateCount = 0;
    uint16_t destinationCount = 0;
    uint16_t loserCount = 0;
    uint16_t conflictCount = 0;
    uint16_t emissionCount = 0;
    uint16_t noRouteCount = 0;
    std::array<MidiCcResolvedDestinationTelemetry, MAX_DESTINATIONS> destinations{};
    std::array<MidiCcContributionTelemetry, MAX_LOSERS> losers{};
};

static_assert(std::is_standard_layout_v<MidiCcResolutionTelemetry>);
static_assert(std::is_trivially_copyable_v<MidiCcResolutionTelemetry>);
static_assert(std::is_trivially_copyable_v<MidiCcCandidate>);
static_assert(std::is_trivially_copyable_v<MidiCcResolvedDestinationTelemetry>);
static_assert(sizeof(MidiCcResolutionTelemetry) <= 8U * 1024U);

bool sameMidiCcDestinationIdentity(
    const MidiCcDestinationIdentity& lhs,
    const MidiCcDestinationIdentity& rhs
);

/** Returns the fixed V1 priority rank (zero is highest). */
uint8_t midiCcCandidatePriority(MidiCcCandidateClass candidateClass);

/**
 * Resolves active candidates without allocation.
 *
 * Output destinations and loser ranges are sorted deterministically by
 * destination, class priority, then stable address. On non-OK status, out is
 * left byte-for-byte untouched; capacity failures therefore never publish a
 * partial frame. PREVIEW computes identical winners but suppresses emission.
 */
MidiCcResolveStatus resolveMidiCcDestinations(
    const MidiCcCandidate* candidates,
    std::size_t candidateCount,
    MidiCcResolutionMode mode,
    MidiCcResolutionTelemetry& out
);

}  // namespace core::state::shared

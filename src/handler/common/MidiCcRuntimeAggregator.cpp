#include "handler/common/MidiCcRuntimeAggregator.hpp"

namespace core::handler {

namespace {

using core::state::shared::MidiCcCandidate;
using core::state::shared::MidiCcCandidateClass;
using core::state::shared::MidiCcDestinationIdentity;
using core::state::shared::MidiCcResolutionMode;
using core::state::shared::MidiCcResolveStatus;
using core::state::shared::MidiCcRouteValidity;

int compareIdentity(
    const MidiCcDestinationIdentity& lhs,
    const MidiCcDestinationIdentity& rhs
) {
    if (lhs.port != rhs.port) return lhs.port < rhs.port ? -1 : 1;
    if (lhs.channel != rhs.channel) return lhs.channel < rhs.channel ? -1 : 1;
    if (lhs.controller != rhs.controller) {
        return lhs.controller < rhs.controller ? -1 : 1;
    }
    return 0;
}

}  // namespace

MidiCcRuntimeAggregator::MidiCcRuntimeAggregator(
    oc::api::MidiAPI& midi,
    uint8_t outputPort
)
    : midi_(midi)
    , output_port_(outputPort) {
    reset();
}

void MidiCcRuntimeAggregator::reset() {
    telemetry_frames_[0] = core::state::shared::MidiCcResolutionTelemetry{};
    telemetry_frames_[1] = core::state::shared::MidiCcResolutionTelemetry{};
    sent_cache_counts_.fill(0);
    candidate_count_ = 0;
    frame_mode_ = MidiCcResolutionMode::PREVIEW;
    frame_status_ = MidiCcResolveStatus::OK;
    published_telemetry_index_ = 0;
    active_sent_cache_index_ = 0;
    frame_open_ = false;
}

void MidiCcRuntimeAggregator::beginFrame(MidiCcResolutionMode mode) {
    candidate_count_ = 0;
    frame_mode_ = mode;
    frame_status_ = MidiCcResolveStatus::OK;
    frame_open_ = true;
}

MidiCcResolveStatus MidiCcRuntimeAggregator::addLiveManual(
    const core::state::shared::MidiCcDestination& destination,
    uint16_t stableAddress,
    uint8_t localValue
) {
    return addCandidate_(
        MidiCcCandidateClass::LIVE_MANUAL,
        destination,
        stableAddress,
        localValue
    );
}

MidiCcResolveStatus MidiCcRuntimeAggregator::addSequencerCcLane(
    const core::state::shared::MidiCcDestination& destination,
    uint16_t stableAddress,
    uint8_t localValue
) {
    return addCandidate_(
        MidiCcCandidateClass::SEQUENCER_CC_LANE,
        destination,
        stableAddress,
        localValue
    );
}

MidiCcResolveStatus MidiCcRuntimeAggregator::addMacroComputed(
    const core::state::shared::MidiCcDestination& destination,
    uint16_t stableAddress,
    uint8_t localValue
) {
    return addCandidate_(
        MidiCcCandidateClass::MACRO_COMPUTED,
        destination,
        stableAddress,
        localValue
    );
}

MidiCcResolveStatus MidiCcRuntimeAggregator::addMacroStatic(
    const core::state::shared::MidiCcDestination& destination,
    uint16_t stableAddress,
    uint8_t localValue
) {
    return addCandidate_(
        MidiCcCandidateClass::MACRO_STATIC,
        destination,
        stableAddress,
        localValue
    );
}

MidiCcResolveStatus MidiCcRuntimeAggregator::addCandidate_(
    MidiCcCandidateClass candidateClass,
    const core::state::shared::MidiCcDestination& destination,
    uint16_t stableAddress,
    uint8_t localValue
) {
    if (!frame_open_) return MidiCcResolveStatus::INVALID_INPUT;
    if (frame_status_ != MidiCcResolveStatus::OK) return frame_status_;
    if (candidate_count_ >= candidates_.size()) {
        frame_status_ = MidiCcResolveStatus::CAPACITY_EXCEEDED;
        return frame_status_;
    }

    const MidiCcCandidate candidate{
        .destination = destination,
        .author = core::state::shared::MidiCcAuthor{
            .candidateClass = candidateClass,
            .stableAddress = stableAddress,
        },
        .localValue = localValue,
    };
    if (!validForBoundOutput_(candidate)) {
        frame_status_ = MidiCcResolveStatus::INVALID_INPUT;
        return frame_status_;
    }

    candidates_[candidate_count_++] = candidate;
    return MidiCcResolveStatus::OK;
}

bool MidiCcRuntimeAggregator::validForBoundOutput_(const MidiCcCandidate& candidate) const {
    if (candidate.localValue > 127U ||
        candidate.destination.identity.controller > 127U) {
        return false;
    }

    const auto routeValidity = candidate.destination.routeValidity;
    const auto& identity = candidate.destination.identity;
    if (routeValidity == MidiCcRouteValidity::VALID) {
        return identity.port == output_port_ &&
               identity.port != MidiCcDestinationIdentity::INVALID_PORT &&
               identity.channel <= 15U;
    }
    if (routeValidity != MidiCcRouteValidity::NO_ROUTE) return false;
    return identity.channel <= 15U ||
           identity.channel == MidiCcDestinationIdentity::INVALID_CHANNEL;
}

MidiCcRuntimePublishResult MidiCcRuntimeAggregator::publish() {
    MidiCcRuntimePublishResult result{};
    result.candidateCount = candidate_count_;
    if (!frame_open_) return result;
    frame_open_ = false;

    if (frame_status_ != MidiCcResolveStatus::OK) {
        result.status = frame_status_;
        return result;
    }

    const uint8_t pendingTelemetryIndex =
        static_cast<uint8_t>(1U - published_telemetry_index_);
    auto& pendingTelemetry = telemetry_frames_[pendingTelemetryIndex];
    result.status = core::state::shared::resolveMidiCcDestinations(
        candidates_.data(),
        candidate_count_,
        frame_mode_,
        pendingTelemetry
    );
    if (result.status != MidiCcResolveStatus::OK) return result;

    // Validate the complete physical-output batch before the first side effect.
    for (uint16_t i = 0; i < pendingTelemetry.destinationCount; ++i) {
        const auto& resolved = pendingTelemetry.destinations[i];
        if (resolved.shouldEmit &&
            resolved.destination.identity.port != output_port_) {
            result.status = MidiCcResolveStatus::INVALID_INPUT;
            return result;
        }
    }

    if (frame_mode_ == MidiCcResolutionMode::LIVE) {
        const uint8_t nextCacheIndex =
            static_cast<uint8_t>(1U - active_sent_cache_index_);
        auto& nextCache = sent_caches_[nextCacheIndex];
        const auto& previousCache = sent_caches_[active_sent_cache_index_];
        const uint16_t previousCount = sent_cache_counts_[active_sent_cache_index_];
        uint16_t previousCursor = 0;
        uint16_t nextCount = 0;

        for (uint16_t i = 0; i < pendingTelemetry.destinationCount; ++i) {
            const auto& resolved = pendingTelemetry.destinations[i];
            if (!resolved.shouldEmit) continue;

            const auto& identity = resolved.destination.identity;
            while (previousCursor < previousCount &&
                   compareIdentity(previousCache[previousCursor].identity, identity) < 0) {
                ++previousCursor;
            }
            const bool unchanged = previousCursor < previousCount &&
                compareIdentity(previousCache[previousCursor].identity, identity) == 0 &&
                previousCache[previousCursor].value == resolved.finalValue;

            nextCache[nextCount++] = SentDestinationValue{
                .identity = identity,
                .value = resolved.finalValue,
            };
            if (unchanged) continue;

            midi_.sendCC(identity.channel, identity.controller, resolved.finalValue);
            ++result.sentCount;
        }

        sent_cache_counts_[nextCacheIndex] = nextCount;
        active_sent_cache_index_ = nextCacheIndex;
    }

    published_telemetry_index_ = pendingTelemetryIndex;
    result.destinationCount = pendingTelemetry.destinationCount;
    result.conflictCount = pendingTelemetry.conflictCount;
    result.noRouteCount = pendingTelemetry.noRouteCount;
    result.eligibleEmissionCount = pendingTelemetry.emissionCount;
    return result;
}

const core::state::shared::MidiCcResolutionTelemetry&
MidiCcRuntimeAggregator::telemetry() const {
    return telemetry_frames_[published_telemetry_index_];
}

}  // namespace core::handler

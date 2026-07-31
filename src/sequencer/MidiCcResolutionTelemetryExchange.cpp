#include "sequencer/MidiCcResolutionTelemetryExchange.hpp"

#include <oc/realtime/InterruptGuard.hpp>

namespace core::sequencer {

MidiCcResolutionTelemetryExchange::ReadView::ReadView(
    const MidiCcResolutionTelemetryExchange& owner,
    const Telemetry& telemetry,
    uint8_t index
)
    : owner_(&owner)
    , telemetry_(&telemetry)
    , index_(index) {}

MidiCcResolutionTelemetryExchange::ReadView::~ReadView() {
    release_();
}

MidiCcResolutionTelemetryExchange::ReadView::ReadView(
    ReadView&& other
) noexcept
    : owner_(other.owner_)
    , telemetry_(other.telemetry_)
    , index_(other.index_) {
    other.owner_ = nullptr;
    other.telemetry_ = nullptr;
    other.index_ = NO_READER;
}

MidiCcResolutionTelemetryExchange::ReadView&
MidiCcResolutionTelemetryExchange::ReadView::operator=(
    ReadView&& other
) noexcept {
    if (this == &other) return *this;
    release_();
    owner_ = other.owner_;
    telemetry_ = other.telemetry_;
    index_ = other.index_;
    other.owner_ = nullptr;
    other.telemetry_ = nullptr;
    other.index_ = NO_READER;
    return *this;
}

void MidiCcResolutionTelemetryExchange::ReadView::release_() {
    if (owner_ != nullptr) owner_->releaseReader_(index_);
    owner_ = nullptr;
    telemetry_ = nullptr;
    index_ = NO_READER;
}

MidiCcResolutionTelemetryExchange::WriteLease
MidiCcResolutionTelemetryExchange::beginWrite() {
    uint8_t publishedIndex = 0U;
    uint8_t readingIndex = NO_READER;
    {
        oc::realtime::InterruptGuard lock;
        publishedIndex = published_index_;
        readingIndex = reading_index_;
    }
    for (uint8_t index = 0U; index < frames_.size(); ++index) {
        if (index != publishedIndex && index != readingIndex) {
            return {
                .telemetry = &frames_[index],
                .index = index,
            };
        }
    }
    return {};
}

void MidiCcResolutionTelemetryExchange::publish(const WriteLease& lease) {
    if (!lease || lease.index >= frames_.size() ||
        lease.telemetry != &frames_[lease.index]) {
        return;
    }
    oc::realtime::InterruptGuard lock;
    published_index_ = lease.index;
}

const MidiCcResolutionTelemetryExchange::Telemetry&
MidiCcResolutionTelemetryExchange::published() const {
    uint8_t index = 0U;
    {
        oc::realtime::InterruptGuard lock;
        index = published_index_;
    }
    return frames_[index];
}

MidiCcResolutionTelemetryExchange::ReadView
MidiCcResolutionTelemetryExchange::read() const {
    oc::realtime::InterruptGuard lock;
    if (reading_index_ != NO_READER ||
        published_index_ >= frames_.size()) {
        return {};
    }
    const uint8_t index = published_index_;
    reading_index_ = index;
    return ReadView(*this, frames_[index], index);
}

void MidiCcResolutionTelemetryExchange::reset() {
    uint8_t heldIndex = NO_READER;
    {
        oc::realtime::InterruptGuard lock;
        heldIndex = reading_index_;
    }

    uint8_t zeroIndex = NO_READER;
    for (uint8_t index = 0U; index < frames_.size(); ++index) {
        if (index == heldIndex) continue;
        frames_[index] = {};
        if (zeroIndex == NO_READER) zeroIndex = index;
    }
    {
        oc::realtime::InterruptGuard lock;
        // Three frames and at most one reader guarantee one zero frame.
        published_index_ = zeroIndex;
    }
}

void MidiCcResolutionTelemetryExchange::releaseReader_(
    uint8_t index
) const {
    oc::realtime::InterruptGuard lock;
    if (reading_index_ == index) reading_index_ = NO_READER;
}

}  // namespace core::sequencer

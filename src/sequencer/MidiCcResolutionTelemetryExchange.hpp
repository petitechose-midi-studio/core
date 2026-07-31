#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/shared/MidiCcDestinationResolver.hpp"

namespace core::sequencer {

/**
 * Triple-buffered single-writer telemetry publication with one stable reader
 * lease.
 *
 * A held reader never blocks publication because two other frames remain
 * writable. Nested reads fail closed instead of replacing the active lease.
 */
class MidiCcResolutionTelemetryExchange final {
public:
    using Telemetry = core::state::shared::MidiCcResolutionTelemetry;

    struct WriteLease {
        Telemetry* telemetry = nullptr;
        uint8_t index = 0xFFU;

        [[nodiscard]] explicit operator bool() const {
            return telemetry != nullptr;
        }
    };

    class ReadView final {
    public:
        ReadView() = default;
        ~ReadView();

        ReadView(const ReadView&) = delete;
        ReadView& operator=(const ReadView&) = delete;
        ReadView(ReadView&& other) noexcept;
        ReadView& operator=(ReadView&& other) noexcept;

        [[nodiscard]] explicit operator bool() const {
            return telemetry_ != nullptr;
        }
        [[nodiscard]] const Telemetry* get() const { return telemetry_; }
        [[nodiscard]] const Telemetry& operator*() const {
            return *telemetry_;
        }
        [[nodiscard]] const Telemetry* operator->() const {
            return telemetry_;
        }

    private:
        friend class MidiCcResolutionTelemetryExchange;

        ReadView(
            const MidiCcResolutionTelemetryExchange& owner,
            const Telemetry& telemetry,
            uint8_t index
        );
        void release_();

        const MidiCcResolutionTelemetryExchange* owner_ = nullptr;
        const Telemetry* telemetry_ = nullptr;
        uint8_t index_ = 0xFFU;
    };

    [[nodiscard]] WriteLease beginWrite();
    void publish(const WriteLease& lease);
    [[nodiscard]] const Telemetry& published() const;
    [[nodiscard]] ReadView read() const;
    void reset();

private:
    static constexpr uint8_t FRAME_COUNT = 3U;
    static constexpr uint8_t NO_READER = 0xFFU;

    void releaseReader_(uint8_t index) const;

    std::array<Telemetry, FRAME_COUNT> frames_{};
    volatile uint8_t published_index_ = 0U;
    mutable volatile uint8_t reading_index_ = NO_READER;
};

static_assert(!std::is_copy_constructible_v<
              MidiCcResolutionTelemetryExchange::ReadView>);
static_assert(std::is_nothrow_move_constructible_v<
              MidiCcResolutionTelemetryExchange::ReadView>);

}  // namespace core::sequencer

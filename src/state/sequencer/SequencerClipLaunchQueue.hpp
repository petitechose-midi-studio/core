#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/sequencer/SequencerClipGridState.hpp"

namespace core::state::sequencer {

enum class SequencerClipLaunchQuantization : uint8_t {
    IMMEDIATE = 0,
    BEAT,
    BAR,
};

enum class SequencerClipLaunchStatus : uint8_t {
    IDLE = 0,
    QUEUED,
    APPLIED,
    CANCELLED,
};

struct SequencerClipRuntimeSource {
    SequencerClipAddress address{};
    uint32_t generation = 0U;
    const SequencerClipDocument* document = nullptr;
    bool valid = false;

    [[nodiscard]] bool resident() const noexcept {
        return valid && document == nullptr;
    }
};

using SequencerClipRuntimeSources = std::array<
    SequencerClipRuntimeSource,
    SequencerTrackBankState::TRACK_COUNT>;

struct SequencerClipLaunchRuntimePublication {
    uint16_t queuedMask = 0U;
    std::array<uint8_t, SequencerTrackBankState::TRACK_COUNT> slots{};
    std::array<uint32_t, SequencerTrackBankState::TRACK_COUNT> generations{};
    std::array<uint32_t, SequencerTrackBankState::TRACK_COUNT> sourceGenerations{};
    std::array<
        SequencerClipLaunchQuantization,
        SequencerTrackBankState::TRACK_COUNT> quantizations{};

    [[nodiscard]] bool empty() const noexcept { return queuedMask == 0U; }
};

struct SequencerClipLaunchRealtimeView {
    enum class Disposition : uint8_t {
        NORMAL = 0,
        FROZEN,
        STAGED,
    };

    Disposition disposition = Disposition::NORMAL;
    SequencerClipLaunchQuantization quantization =
        SequencerClipLaunchQuantization::IMMEDIATE;
    uint8_t slot = SequencerClipGridState::INVALID_SLOT;
    uint8_t previousSnapshotIndex = 0U;
    uint32_t generation = 0U;
};

struct SequencerClipLaunchTelemetry {
    SequencerClipLaunchStatus status = SequencerClipLaunchStatus::IDLE;
    SequencerClipLaunchQuantization quantization =
        SequencerClipLaunchQuantization::IMMEDIATE;
    uint8_t activeSlot = SequencerClipGridState::INVALID_SLOT;
    uint8_t queuedSlot = SequencerClipGridState::INVALID_SLOT;
    uint32_t generation = 0U;
};

/**
 * Scalar authoring-to-realtime hand-off for quantized Clip launches.
 *
 * Musical payloads remain in the existing immutable runtime banks. While a
 * launch is staged, those banks pin the previous generation; the timer lane
 * only reads and writes the fixed fields below and never allocates or emits a
 * Signal.
 */
class SequencerClipLaunchQueue {
public:
    static constexpr uint8_t TRACK_COUNT = SequencerTrackBankState::TRACK_COUNT;

    void reset(const SequencerClipGridState& clips, uint16_t enabledTrackMask);

    [[nodiscard]] bool request(
        SequencerClipAddress target,
        const SequencerClipGridState& clips,
        bool transportPlaying,
        SequencerClipLaunchQuantization quantization =
            SequencerClipLaunchQuantization::BAR
    );

    [[nodiscard]] SequencerClipLaunchRuntimePublication captureRuntimePublication(
        const SequencerClipGridState& clips,
        bool transportPlaying
    );
    [[nodiscard]] bool captureRuntimeSources(
        const SequencerClipGridState& clips,
        SequencerClipRuntimeSources& out
    ) const noexcept;

    /** Called inside the graph/snapshot publication interrupt guard. */
    void applyRuntimePublication(
        const SequencerClipLaunchRuntimePublication& publication,
        uint8_t previousSnapshotIndex,
        uint8_t targetSnapshotIndex
    ) noexcept;

    [[nodiscard]] SequencerClipLaunchRealtimeView realtimeView(
        uint8_t track
    ) const noexcept;
    [[nodiscard]] bool markAppliedFromRealtime(
        uint8_t track,
        uint32_t generation
    ) noexcept;

    /** Main-loop bridge; returns Tracks whose retired graph can be released. */
    [[nodiscard]] uint16_t publishRealtimeTelemetry();

    [[nodiscard]] uint16_t pendingTrackMask() const noexcept;
    [[nodiscard]] uint16_t stagedTrackMask() const noexcept;
    [[nodiscard]] bool references(SequencerClipAddress address) const noexcept;
    [[nodiscard]] uint8_t activeSlot(uint8_t track) const noexcept;
    [[nodiscard]] SequencerClipLaunchTelemetry telemetry(uint8_t track) const noexcept;

    oc::state::Signal<uint32_t, 4>& telemetryRevision() noexcept {
        return telemetry_revision_;
    }
    const oc::state::Signal<uint32_t, 4>& telemetryRevision() const noexcept {
        return telemetry_revision_;
    }

private:
    enum class Phase : uint8_t {
        IDLE = 0,
        QUEUED,
        STAGED,
        APPLIED_PENDING_TELEMETRY,
        APPLIED,
        CANCELLED,
    };

    struct Entry {
        volatile Phase phase = Phase::IDLE;
        volatile SequencerClipLaunchQuantization quantization =
            SequencerClipLaunchQuantization::IMMEDIATE;
        volatile uint8_t activeSlot = SequencerClipGridState::INVALID_SLOT;
        volatile uint8_t targetSlot = SequencerClipGridState::INVALID_SLOT;
        volatile uint8_t previousSnapshotIndex = 0U;
        volatile uint8_t targetSnapshotIndex = 0U;
        volatile uint32_t sourceGeneration = 0U;
        volatile uint32_t generation = 0U;
    };

    static constexpr uint16_t ALL_TRACKS_MASK =
        static_cast<uint16_t>((1U << TRACK_COUNT) - 1U);

    static uint32_t nextNonZeroGeneration_(uint32_t current) noexcept;
    static bool pending_(Phase phase) noexcept;
    static SequencerClipLaunchStatus status_(Phase phase) noexcept;
    void bumpTelemetryRevision_();
    bool queueFallback_(
        uint8_t track,
        const SequencerClipGridState& clips,
        bool transportPlaying
    );

    std::array<Entry, TRACK_COUNT> entries_{};
    uint32_t next_generation_ = 0U;
    oc::state::Signal<uint32_t, 4> telemetry_revision_{0U};
};

}  // namespace core::state::sequencer

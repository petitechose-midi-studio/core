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

enum class SequencerClipLaunchAction : uint8_t {
    NONE = 0,
    CLIP,
    STOP,
};

enum class SequencerClipLaunchOrigin : uint8_t {
    CLIP_FOLLOW = 0,
    SCENE_FOLLOW,
    MANUAL_CLIP,
    MANUAL_SCENE,
    DIRECT_STOP,
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
    std::array<SequencerClipLaunchAction,
        SequencerTrackBankState::TRACK_COUNT> actions{};
    std::array<uint8_t, SequencerTrackBankState::TRACK_COUNT> slots{};
    std::array<uint32_t, SequencerTrackBankState::TRACK_COUNT> generations{};
    std::array<uint32_t, SequencerTrackBankState::TRACK_COUNT>
        sourceGenerations{};
    std::array<uint32_t, SequencerTrackBankState::TRACK_COUNT> dueTicks{};
    std::array<SequencerClipLaunchQuantization,
        SequencerTrackBankState::TRACK_COUNT> quantizations{};

    [[nodiscard]] bool empty() const noexcept { return queuedMask == 0U; }
};

struct SequencerClipLaunchRollbackPublication {
    uint16_t trackMask = 0U;
    uint8_t previousSnapshotIndex = 0U;
    uint32_t generation = 0U;

    [[nodiscard]] bool empty() const noexcept { return trackMask == 0U; }
};

struct SequencerClipLaunchRealtimeView {
    enum class Disposition : uint8_t {
        NORMAL = 0,
        FROZEN,
        STAGED,
    };

    Disposition disposition = Disposition::NORMAL;
    SequencerClipLaunchAction action = SequencerClipLaunchAction::NONE;
    SequencerClipLaunchQuantization quantization =
        SequencerClipLaunchQuantization::IMMEDIATE;
    uint8_t slot = SequencerClipGridState::INVALID_SLOT;
    uint8_t previousSnapshotIndex = 0U;
    uint32_t dueTick = 0U;
    uint32_t generation = 0U;
};

struct SequencerClipLaunchTelemetry {
    SequencerClipLaunchStatus status = SequencerClipLaunchStatus::IDLE;
    SequencerClipLaunchAction action = SequencerClipLaunchAction::NONE;
    SequencerClipLaunchOrigin origin = SequencerClipLaunchOrigin::MANUAL_CLIP;
    SequencerClipLaunchQuantization quantization =
        SequencerClipLaunchQuantization::IMMEDIATE;
    uint8_t activeSlot = SequencerClipGridState::INVALID_SLOT;
    uint8_t queuedSlot = SequencerClipGridState::INVALID_SLOT;
    uint8_t beatsRemaining = 0U;
    uint8_t activePhaseQ8 = 0U;
    uint32_t generation = 0U;
    bool stopped = false;
};

struct SequencerSceneLaunchTelemetry {
    SequencerClipLaunchStatus status = SequencerClipLaunchStatus::IDLE;
    uint8_t activeScene = SequencerClipGridState::INVALID_SLOT;
    uint8_t queuedScene = SequencerClipGridState::INVALID_SLOT;
    uint8_t beatsRemaining = 0U;
    uint32_t generation = 0U;
    bool replaced = false;
};

/** Fixed Clip/Stop/Scene hand-off; musical payloads stay in runtime banks. */
class SequencerClipLaunchQueue {
public:
    static constexpr uint8_t TRACK_COUNT = SequencerTrackBankState::TRACK_COUNT;

    void reset(const SequencerClipGridState& clips, uint16_t enabledTrackMask);
    void synchronizeEnabledTracks(
        const SequencerClipGridState& clips,
        uint16_t enabledTrackMask
    );
    void updateTransportPosition(uint32_t tick, bool playing);

    [[nodiscard]] bool request(
        SequencerClipAddress target,
        const SequencerClipGridState& clips,
        bool transportPlaying,
        SequencerClipLaunchQuantization quantization =
            SequencerClipLaunchQuantization::BAR,
        uint32_t loopTicks = 0U
    );
    [[nodiscard]] bool requestStop(
        uint8_t track,
        bool transportPlaying,
        SequencerClipLaunchQuantization quantization,
        SequencerClipLaunchOrigin origin =
            SequencerClipLaunchOrigin::DIRECT_STOP,
        uint8_t sourceSlot = SequencerClipGridState::INVALID_SLOT
    );
    [[nodiscard]] bool requestScene(
        uint8_t slot,
        const SequencerClipGridState& clips,
        uint16_t enabledTrackMask,
        bool transportPlaying,
        SequencerClipLaunchQuantization quantization =
            SequencerClipLaunchQuantization::BAR,
        const std::array<uint32_t, TRACK_COUNT>* loopTicks = nullptr
    );
    void processFollowActions(
        const SequencerClipGridState& clips,
        uint16_t enabledTrackMask,
        bool transportPlaying,
        const std::array<uint32_t, TRACK_COUNT>* loopTicks = nullptr
    );

    [[nodiscard]] SequencerClipLaunchRollbackPublication
        captureRollbackPublication() const noexcept;
    void applyRollbackPublication(
        const SequencerClipLaunchRollbackPublication& publication
    ) noexcept;

    [[nodiscard]] SequencerClipLaunchRuntimePublication captureRuntimePublication(
        const SequencerClipGridState& clips,
        bool transportPlaying
    );
    [[nodiscard]] bool captureRuntimeSources(
        const SequencerClipGridState& clips,
        SequencerClipRuntimeSources& out
    ) const noexcept;
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
        uint32_t generation,
        uint32_t tick = 0U
    ) noexcept;
    [[nodiscard]] uint16_t publishRealtimeTelemetry();

    [[nodiscard]] uint16_t pendingTrackMask() const noexcept;
    [[nodiscard]] uint16_t stagedTrackMask() const noexcept;
    [[nodiscard]] bool references(SequencerClipAddress address) const noexcept;
    void refreshBehavior(
        SequencerClipAddress address,
        SequencerLauncherBehavior behavior
    ) noexcept;
    [[nodiscard]] uint8_t activeSlot(uint8_t track) const noexcept;
    [[nodiscard]] bool stopped(uint8_t track) const noexcept;
    [[nodiscard]] SequencerClipLaunchTelemetry telemetry(
        uint8_t track
    ) const noexcept;
    [[nodiscard]] SequencerSceneLaunchTelemetry sceneTelemetry() const noexcept;

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
        ROLLBACK_PENDING,
        APPLIED_PENDING_TELEMETRY,
        APPLIED,
        CANCELLED,
    };

    struct RequestSpec {
        SequencerClipLaunchAction action = SequencerClipLaunchAction::NONE;
        SequencerClipLaunchOrigin origin = SequencerClipLaunchOrigin::MANUAL_CLIP;
        SequencerClipLaunchQuantization quantization =
            SequencerClipLaunchQuantization::IMMEDIATE;
        uint8_t targetSlot = SequencerClipGridState::INVALID_SLOT;
        uint32_t sourceGeneration = 0U;
        uint32_t dueTick = 0U;
        uint32_t generation = 0U;
        uint32_t groupGeneration = 0U;
        uint32_t loopTicks = 0U;
        SequencerLauncherBehavior behavior{};

        [[nodiscard]] bool queued() const noexcept {
            return action != SequencerClipLaunchAction::NONE;
        }
    };

    struct DesiredPlan {
        std::array<RequestSpec, TRACK_COUNT> requests{};
        uint16_t queuedMask = 0U;
        uint8_t sceneSlot = SequencerClipGridState::INVALID_SLOT;
        uint16_t sceneExpectedMask = 0U;
        uint32_t sceneGeneration = 0U;
        SequencerLauncherBehavior sceneBehavior{};
    };

    struct Entry {
        volatile Phase phase = Phase::IDLE;
        volatile SequencerClipLaunchAction action =
            SequencerClipLaunchAction::NONE;
        volatile SequencerClipLaunchOrigin origin =
            SequencerClipLaunchOrigin::MANUAL_CLIP;
        volatile SequencerClipLaunchQuantization quantization =
            SequencerClipLaunchQuantization::IMMEDIATE;
        volatile uint8_t activeSlot = SequencerClipGridState::INVALID_SLOT;
        volatile uint8_t targetSlot = SequencerClipGridState::INVALID_SLOT;
        volatile uint8_t previousSnapshotIndex = 0U;
        volatile uint8_t targetSnapshotIndex = 0U;
        volatile bool stopped = false;
        volatile uint32_t sourceGeneration = 0U;
        volatile uint32_t dueTick = 0U;
        volatile uint32_t generation = 0U;
        volatile uint32_t groupGeneration = 0U;
        volatile uint32_t pendingLoopTicks = 0U;
        volatile uint32_t activeStartedTick = 0U;
        volatile uint32_t activeLoopTicks = 0U;
        volatile bool activeFollowScheduled = false;
        SequencerLauncherBehavior pendingBehavior{};
        SequencerLauncherBehavior activeBehavior{};
    };

    static constexpr uint16_t ALL_TRACKS_MASK =
        static_cast<uint16_t>((1U << TRACK_COUNT) - 1U);

    static uint32_t nextNonZeroGeneration_(uint32_t current) noexcept;
    static bool pending_(Phase phase) noexcept;
    static uint8_t priority_(SequencerClipLaunchOrigin origin) noexcept;
    static SequencerClipLaunchStatus status_(Phase phase) noexcept;
    static SequencerClipLaunchQuantization followQuantization_(
        SequencerLauncherFollowQuantization quantization
    ) noexcept;
    static bool due_(uint32_t now, uint32_t deadline) noexcept;
    uint32_t nextBoundaryTick_(
        SequencerClipLaunchQuantization quantization,
        bool transportPlaying
    ) const noexcept;
    uint8_t beatsRemaining_(uint32_t dueTick) const noexcept;
    void resetEntry_(
        Entry& entry,
        uint8_t track,
        const SequencerClipGridState& clips,
        bool enabled
    ) noexcept;
    void bumpTelemetryRevision_();
    void captureDesiredPlan_(DesiredPlan& out) const noexcept;
    void applyDesiredPlan_(const DesiredPlan& plan) noexcept;
    void commitDesiredPlan_(const DesiredPlan& plan);
    [[nodiscard]] bool requestWithOrigin_(
        SequencerClipAddress target,
        const SequencerClipGridState& clips,
        bool transportPlaying,
        SequencerClipLaunchQuantization quantization,
        uint32_t loopTicks,
        SequencerClipLaunchOrigin origin
    );
    [[nodiscard]] bool requestSceneWithOrigin_(
        uint8_t slot,
        const SequencerClipGridState& clips,
        uint16_t enabledTrackMask,
        bool transportPlaying,
        SequencerClipLaunchQuantization quantization,
        const std::array<uint32_t, TRACK_COUNT>* loopTicks,
        SequencerClipLaunchOrigin origin
    );
    bool queueFallback_(
        uint8_t track,
        const SequencerClipGridState& clips,
        bool transportPlaying
    );

    std::array<Entry, TRACK_COUNT> entries_{};
    DesiredPlan rollback_plan_{};
    uint16_t rollback_track_mask_ = 0U;
    uint8_t rollback_previous_snapshot_index_ = 0U;
    uint32_t rollback_generation_ = 0U;
    uint32_t next_generation_ = 0U;
    volatile uint32_t transport_tick_ = 0U;
    volatile bool transport_playing_ = false;
    uint16_t enabled_track_mask_ = 0U;
    uint32_t published_beat_ = 0U;
    volatile uint8_t active_scene_ = SequencerClipGridState::INVALID_SLOT;
    volatile uint8_t queued_scene_ = SequencerClipGridState::INVALID_SLOT;
    volatile uint16_t scene_expected_mask_ = 0U;
    volatile uint16_t scene_applied_mask_ = 0U;
    volatile uint32_t active_scene_started_tick_ = 0U;
    volatile uint32_t active_scene_generation_ = 0U;
    volatile uint32_t queued_scene_generation_ = 0U;
    volatile bool active_scene_follow_scheduled_ = false;
    volatile bool scene_replaced_ = false;
    volatile SequencerClipLaunchStatus scene_status_ =
        SequencerClipLaunchStatus::IDLE;
    SequencerLauncherBehavior active_scene_behavior_{};
    SequencerLauncherBehavior queued_scene_behavior_{};
    oc::state::Signal<uint32_t, 4> telemetry_revision_{0U};
};

}  // namespace core::state::sequencer

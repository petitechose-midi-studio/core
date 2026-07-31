#include "handler/settings/DeviceSettingsDomainServices.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>

#include <config/PlatformCompat.hpp>

#include "state/MidiSyncSettingsPolicy.hpp"

namespace core::handler {

namespace {

namespace policy = core::state::midi_sync_policy;

template <typename T, size_t N>
int findChoiceIndex(
    const T& value,
    const std::array<T, N>& choices,
    int fallback = 0
) {
    for (size_t i = 0; i < N; ++i) {
        if (choices[i] == value) return static_cast<int>(i);
    }
    return std::clamp(fallback, 0, static_cast<int>(N) - 1);
}

}  // namespace

FLASHMEM DeviceSettingsDomainServices::DeviceSettingsDomainServices(StateRefs state)
    : midi_sync_(&state.midiSync)
    , store_(&state.store) {}

FLASHMEM int DeviceSettingsDomainServices::currentChoiceIndex(uint8_t row) const {
    switch (row) {
        case 0:
            return findChoiceIndex(
                midi_sync_->mode.get(),
                policy::MODES,
                findChoiceIndex(
                    core::state::MidiSyncMode::AUTO,
                    policy::MODES,
                    0
                )
            );
        case 1:
            return findChoiceIndex(
                midi_sync_->followTransport.get(),
                policy::FOLLOW_TRANSPORT,
                1
            );
        case 2:
            return findChoiceIndex(
                midi_sync_->autoFallbackMs.get(),
                policy::AUTO_FALLBACK_MS,
                findChoiceIndex(
                    static_cast<uint16_t>(500),
                    policy::AUTO_FALLBACK_MS,
                    0
                )
            );
        case 3:
            return findChoiceIndex(
                midi_sync_->autoLockClockCount.get(),
                policy::AUTO_LOCK_CLOCKS,
                findChoiceIndex(
                    static_cast<uint8_t>(6),
                    policy::AUTO_LOCK_CLOCKS,
                    0
                )
            );
        default:
            return 0;
    }
}

FLASHMEM int DeviceSettingsDomainServices::choiceCount(uint8_t row) const {
    switch (row) {
        case 0: return static_cast<int>(policy::MODES.size());
        case 1: return static_cast<int>(policy::FOLLOW_TRANSPORT.size());
        case 2: return static_cast<int>(policy::AUTO_FALLBACK_MS.size());
        case 3: return static_cast<int>(policy::AUTO_LOCK_CLOCKS.size());
        default: return 0;
    }
}

FLASHMEM void DeviceSettingsDomainServices::applyChoice(uint8_t row, int choiceIndex) const {
    auto status = core::persistence::PersistenceWriteStatus::OK;
    int appliedIndex = 0;

    switch (row) {
        case 0: {
            appliedIndex = std::clamp(
                choiceIndex,
                0,
                static_cast<int>(policy::MODES.size()) - 1
            );
            status = store_->saveMidiSyncModeStatus(
                policy::MODES[appliedIndex]
            );
            break;
        }
        case 1: {
            appliedIndex = std::clamp(
                choiceIndex,
                0,
                static_cast<int>(policy::FOLLOW_TRANSPORT.size()) - 1
            );
            status = store_->saveMidiFollowTransportStatus(
                policy::FOLLOW_TRANSPORT[appliedIndex]
            );
            break;
        }
        case 2: {
            appliedIndex = std::clamp(
                choiceIndex,
                0,
                static_cast<int>(policy::AUTO_FALLBACK_MS.size()) - 1
            );
            status = store_->saveMidiAutoFallbackMsStatus(
                policy::AUTO_FALLBACK_MS[appliedIndex]
            );
            break;
        }
        case 3: {
            appliedIndex = std::clamp(
                choiceIndex,
                0,
                static_cast<int>(policy::AUTO_LOCK_CLOCKS.size()) - 1
            );
            status = store_->saveMidiAutoLockClockCountStatus(
                policy::AUTO_LOCK_CLOCKS[appliedIndex]
            );
            break;
        }
        default:
            return;
    }

    if (status != core::persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[DeviceSettings] Failed to stage settings row {}: {}",
                    row,
                    core::persistence::persistenceWriteStatusLabel(status));
        return;
    }

    const auto commitStatus = store_->commitStatus();
    if (commitStatus != core::persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[DeviceSettings] Failed to commit settings row {}: {}",
                    row,
                    core::persistence::persistenceWriteStatusLabel(commitStatus));
        return;
    }

    switch (row) {
        case 0:
            midi_sync_->mode.set(policy::MODES[appliedIndex]);
            break;
        case 1:
            midi_sync_->followTransport.set(
                policy::FOLLOW_TRANSPORT[appliedIndex]
            );
            break;
        case 2:
            midi_sync_->autoFallbackMs.set(
                policy::AUTO_FALLBACK_MS[appliedIndex]
            );
            break;
        case 3:
            midi_sync_->autoLockClockCount.set(
                policy::AUTO_LOCK_CLOCKS[appliedIndex]
            );
            break;
        default:
            break;
    }
}

}  // namespace core::handler

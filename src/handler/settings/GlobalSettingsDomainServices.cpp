#include "handler/settings/GlobalSettingsDomainServices.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>

#include <config/PlatformCompat.hpp>

namespace core::handler {

namespace {

constexpr int MODE_COUNT = 3;
constexpr int FOLLOW_COUNT = 2;
constexpr int FALLBACK_COUNT = 7;
constexpr int LOCK_COUNT = 8;

constexpr core::state::MidiSyncMode MODE_VALUES[MODE_COUNT] = {
    core::state::MidiSyncMode::MASTER,
    core::state::MidiSyncMode::SLAVE,
    core::state::MidiSyncMode::AUTO,
};

constexpr bool FOLLOW_VALUES[FOLLOW_COUNT] = {false, true};
constexpr uint16_t FALLBACK_VALUES[FALLBACK_COUNT] = {150, 250, 500, 750, 1000, 1500, 2000};
constexpr uint8_t LOCK_VALUES[LOCK_COUNT] = {1, 2, 3, 4, 6, 8, 12, 24};

template <typename T, int N>
int findChoiceIndex(const T& value, const T (&choices)[N], int fallback = 0) {
    for (int i = 0; i < N; ++i) {
        if (choices[i] == value) return i;
    }
    return std::clamp(fallback, 0, N - 1);
}

}  // namespace

FLASHMEM GlobalSettingsDomainServices::GlobalSettingsDomainServices(StateRefs state)
    : midi_sync_(&state.midiSync)
    , settings_(&state.settings) {}

FLASHMEM int GlobalSettingsDomainServices::currentChoiceIndex(uint8_t row) const {
    switch (row) {
        case 0:
            return findChoiceIndex(
                midi_sync_->mode.get(),
                MODE_VALUES,
                findChoiceIndex(core::state::MidiSyncMode::AUTO, MODE_VALUES, 0)
            );
        case 1:
            return findChoiceIndex(midi_sync_->followTransport.get(), FOLLOW_VALUES, 1);
        case 2:
            return findChoiceIndex(
                midi_sync_->autoFallbackMs.get(),
                FALLBACK_VALUES,
                findChoiceIndex(static_cast<uint16_t>(500), FALLBACK_VALUES, 0)
            );
        case 3:
            return findChoiceIndex(
                midi_sync_->autoLockClockCount.get(),
                LOCK_VALUES,
                findChoiceIndex(static_cast<uint8_t>(6), LOCK_VALUES, 0)
            );
        default:
            return 0;
    }
}

FLASHMEM int GlobalSettingsDomainServices::choiceCount(uint8_t row) const {
    switch (row) {
        case 0: return MODE_COUNT;
        case 1: return FOLLOW_COUNT;
        case 2: return FALLBACK_COUNT;
        case 3: return LOCK_COUNT;
        default: return 0;
    }
}

FLASHMEM void GlobalSettingsDomainServices::applyChoice(uint8_t row, int choiceIndex) const {
    auto status = core::persistence::PersistenceWriteStatus::OK;

    switch (row) {
        case 0: {
            const int idx = std::clamp(choiceIndex, 0, MODE_COUNT - 1);
            midi_sync_->mode.set(MODE_VALUES[idx]);
            status = settings_->saveMidiSyncModeStatus(midi_sync_->mode.get());
            break;
        }
        case 1: {
            const int idx = std::clamp(choiceIndex, 0, FOLLOW_COUNT - 1);
            midi_sync_->followTransport.set(FOLLOW_VALUES[idx]);
            status = settings_->saveMidiFollowTransportStatus(midi_sync_->followTransport.get());
            break;
        }
        case 2: {
            const int idx = std::clamp(choiceIndex, 0, FALLBACK_COUNT - 1);
            midi_sync_->autoFallbackMs.set(FALLBACK_VALUES[idx]);
            status = settings_->saveMidiAutoFallbackMsStatus(midi_sync_->autoFallbackMs.get());
            break;
        }
        case 3: {
            const int idx = std::clamp(choiceIndex, 0, LOCK_COUNT - 1);
            midi_sync_->autoLockClockCount.set(LOCK_VALUES[idx]);
            status = settings_->saveMidiAutoLockClockCountStatus(midi_sync_->autoLockClockCount.get());
            break;
        }
        default:
            return;
    }

    if (status != core::persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[GlobalSettings] Failed to stage settings row {}: {}",
                    row,
                    core::persistence::persistenceWriteStatusLabel(status));
        return;
    }

    const auto commitStatus = settings_->commitStatus();
    if (commitStatus != core::persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[GlobalSettings] Failed to commit settings row {}: {}",
                    row,
                    core::persistence::persistenceWriteStatusLabel(commitStatus));
    }
}

}  // namespace core::handler

#include "handler/settings/DeviceSettingsDomainServices.hpp"

#include <algorithm>

#include <oc/log/Log.hpp>

#include <config/PlatformCompat.hpp>

#include "state/MidiSyncSettingsPolicy.hpp"

namespace core::handler {

namespace {

namespace policy = core::state::midi_sync_policy;
using ApplyResult = DeviceSettingsDomainServices::ApplyResult;
using ApplyStatus = DeviceSettingsDomainServices::ApplyStatus;

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

FLASHMEM ApplyResult applyResult(
    ApplyStatus status,
    core::persistence::PersistenceWriteStatus persistenceStatus =
        core::persistence::PersistenceWriteStatus::OK
) {
    return ApplyResult{
        .status = status,
        .persistenceStatus = persistenceStatus,
    };
}

}  // namespace

FLASHMEM DeviceSettingsDomainServices::DeviceSettingsDomainServices(StateRefs state)
    : midi_sync_(&state.midiSync)
    , midi_note_display_(&state.midiNoteDisplay)
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
        case 4:
            return findChoiceIndex(
                midi_note_display_->octaveConvention.get(),
                core::midi::NOTE_OCTAVE_CONVENTIONS,
                static_cast<int>(
                    core::midi::DEFAULT_NOTE_OCTAVE_CONVENTION
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
        case 4:
            return static_cast<int>(
                core::midi::NOTE_OCTAVE_CONVENTIONS.size()
            );
        default: return 0;
    }
}

FLASHMEM DeviceSettingsDomainServices::ApplyResult
DeviceSettingsDomainServices::applyMidiSyncMode(
    core::state::MidiSyncMode mode
) const {
    if (!policy::validMode(mode)) {
        return applyResult(
            ApplyStatus::INVALID_SELECTION,
            core::persistence::PersistenceWriteStatus::INVALID_CONFIG
        );
    }
    return applyChoice(0U, findChoiceIndex(mode, policy::MODES, 0));
}

FLASHMEM DeviceSettingsDomainServices::ApplyResult
DeviceSettingsDomainServices::applyChoice(uint8_t row, int choiceIndex) const {
    if (choiceCount(row) <= 0) {
        return applyResult(
            ApplyStatus::INVALID_SELECTION,
            core::persistence::PersistenceWriteStatus::INVALID_CONFIG
        );
    }

    // A failed write/commit may leave backend staging dirty. Restore the
    // RAM-authoritative record before preparing another independent choice so
    // a later successful commit cannot publish stale bytes from that failure.
    const auto reconciliationStatus = store_->reconcileAllStatus(
        *midi_sync_,
        *midi_note_display_
    );
    if (reconciliationStatus !=
        core::persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN(
            "[DeviceSettings] Failed to reconcile settings before row {}: {}",
            row,
            core::persistence::persistenceWriteStatusLabel(
                reconciliationStatus
            )
        );
        return applyResult(
            ApplyStatus::PERSISTENCE_FAILED,
            reconciliationStatus
        );
    }

    auto status = core::persistence::PersistenceWriteStatus::OK;
    int appliedIndex = 0;

    switch (row) {
        case 0: {
            appliedIndex = std::clamp(
                choiceIndex,
                0,
                static_cast<int>(policy::MODES.size()) - 1
            );
            if (midi_sync_->mode.get() == policy::MODES[appliedIndex]) {
                return applyResult(ApplyStatus::NO_CHANGE);
            }
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
            if (midi_sync_->followTransport.get() ==
                policy::FOLLOW_TRANSPORT[appliedIndex]) {
                return applyResult(ApplyStatus::NO_CHANGE);
            }
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
            if (midi_sync_->autoFallbackMs.get() ==
                policy::AUTO_FALLBACK_MS[appliedIndex]) {
                return applyResult(ApplyStatus::NO_CHANGE);
            }
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
            if (midi_sync_->autoLockClockCount.get() ==
                policy::AUTO_LOCK_CLOCKS[appliedIndex]) {
                return applyResult(ApplyStatus::NO_CHANGE);
            }
            status = store_->saveMidiAutoLockClockCountStatus(
                policy::AUTO_LOCK_CLOCKS[appliedIndex]
            );
            break;
        }
        case 4: {
            appliedIndex = std::clamp(
                choiceIndex,
                0,
                static_cast<int>(
                    core::midi::NOTE_OCTAVE_CONVENTIONS.size()
                ) - 1
            );
            if (midi_note_display_->octaveConvention.get() ==
                core::midi::NOTE_OCTAVE_CONVENTIONS[appliedIndex]) {
                return applyResult(ApplyStatus::NO_CHANGE);
            }
            status = store_->saveNoteOctaveConventionStatus(
                core::midi::NOTE_OCTAVE_CONVENTIONS[appliedIndex]
            );
            break;
        }
        default: break;
    }

    if (status != core::persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[DeviceSettings] Failed to stage settings row {}: {}",
                    row,
                    core::persistence::persistenceWriteStatusLabel(status));
        return applyResult(ApplyStatus::PERSISTENCE_FAILED, status);
    }

    const auto commitStatus = store_->commitStatus();
    if (commitStatus != core::persistence::PersistenceWriteStatus::OK) {
        OC_LOG_WARN("[DeviceSettings] Failed to commit settings row {}: {}",
                    row,
                    core::persistence::persistenceWriteStatusLabel(commitStatus));
        return applyResult(ApplyStatus::PERSISTENCE_FAILED, commitStatus);
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
        case 4:
            midi_note_display_->setOctaveConvention(
                core::midi::NOTE_OCTAVE_CONVENTIONS[appliedIndex]
            );
            break;
        default:
            break;
    }
    return applyResult(ApplyStatus::APPLIED);
}

}  // namespace core::handler

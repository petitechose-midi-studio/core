#include "persistence/DeviceSettingsCodec.hpp"
#include "persistence/DeviceSettingsStorageLayout.hpp"

#include <config/PlatformCompat.hpp>

#include "state/MidiSyncSettingsPolicy.hpp"

namespace core::persistence::device_settings {

namespace {

bool readExact(
    oc::interface::IStorage& backend,
    uint32_t address,
    uint8_t* buffer,
    size_t size
) {
    if (!backend.available()) return false;
    return backend.read(address, buffer, size) == size;
}

bool writeExact(
    oc::interface::IStorage& backend,
    uint32_t address,
    const uint8_t* buffer,
    size_t size
) {
    if (!backend.available()) return false;
    return backend.write(address, buffer, size) == size;
}

PersistenceWriteStatus writeExactStatus(
    oc::interface::IStorage& backend,
    uint32_t address,
    const uint8_t* buffer,
    size_t size
) {
    if (!backend.available()) return PersistenceWriteStatus::STORAGE_UNAVAILABLE;
    return writeExact(backend, address, buffer, size)
               ? PersistenceWriteStatus::OK
               : PersistenceWriteStatus::IO_ERROR;
}

constexpr uint16_t decodeU16LittleEndian(const uint8_t (&bytes)[2]) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(bytes[0]) |
        (static_cast<uint16_t>(bytes[1]) << 8U)
    );
}

constexpr uint32_t decodeU32LittleEndian(const uint8_t (&bytes)[4]) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) |
           (static_cast<uint32_t>(bytes[3]) << 24U);
}

void encodeU16LittleEndian(uint16_t value, uint8_t (&bytes)[2]) {
    bytes[0] = static_cast<uint8_t>(value & 0xFFU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void encodeU32LittleEndian(uint32_t value, uint8_t (&bytes)[4]) {
    bytes[0] = static_cast<uint8_t>(value & 0xFFU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    bytes[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    bytes[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

}  // namespace

FLASHMEM bool readMagic(oc::interface::IStorage& backend, uint32_t& magic) {
    uint8_t encoded[4]{};
    if (!readExact(backend, layout::ADDR_MAGIC, encoded, sizeof(encoded))) {
        return false;
    }
    magic = decodeU32LittleEndian(encoded);
    return true;
}

FLASHMEM bool readVersion(
    oc::interface::IStorage& backend,
    uint8_t& version
) {
    return readExact(
        backend,
        layout::ADDR_VERSION,
        &version,
        sizeof(version)
    );
}

FLASHMEM bool validMidiSyncMode(state::MidiSyncMode mode) {
    return state::midi_sync_policy::validMode(mode);
}

FLASHMEM bool validMidiSyncAutoFallbackMs(uint16_t fallbackMs) {
    return state::midi_sync_policy::validAutoFallbackMs(fallbackMs);
}

FLASHMEM bool validMidiSyncAutoLockClockCount(uint8_t lockClocks) {
    return state::midi_sync_policy::validAutoLockClockCount(lockClocks);
}

FLASHMEM bool validMidiSyncState(const state::MidiSyncState& midiSync) {
    return validMidiSyncMode(midiSync.mode.get()) &&
           validMidiSyncAutoFallbackMs(midiSync.autoFallbackMs.get()) &&
           validMidiSyncAutoLockClockCount(midiSync.autoLockClockCount.get());
}

FLASHMEM PersistenceWriteStatus stageMidiSyncMode(
    oc::interface::IStorage& backend,
    state::MidiSyncMode mode
) {
    const uint8_t encoded = static_cast<uint8_t>(mode);
    return writeExactStatus(
        backend,
        layout::ADDR_SYNC_MODE,
        &encoded,
        sizeof(encoded)
    );
}

FLASHMEM PersistenceWriteStatus stageMidiFollowTransport(
    oc::interface::IStorage& backend,
    bool followTransport
) {
    const uint8_t encoded = followTransport ? 1U : 0U;
    return writeExactStatus(
        backend,
        layout::ADDR_SYNC_FOLLOW_TRANSPORT,
        &encoded,
        sizeof(encoded)
    );
}

FLASHMEM PersistenceWriteStatus stageMidiAutoFallbackMs(
    oc::interface::IStorage& backend,
    uint16_t fallbackMs
) {
    uint8_t encoded[2]{};
    encodeU16LittleEndian(fallbackMs, encoded);
    return writeExactStatus(
        backend,
        layout::ADDR_SYNC_AUTO_FALLBACK_MS,
        encoded,
        sizeof(encoded)
    );
}

FLASHMEM PersistenceWriteStatus stageMidiAutoLockClockCount(
    oc::interface::IStorage& backend,
    uint8_t lockCount
) {
    return writeExactStatus(
        backend,
        layout::ADDR_SYNC_AUTO_LOCK_CLOCKS,
        &lockCount,
        sizeof(lockCount)
    );
}

FLASHMEM PersistenceWriteStatus stageNoteOctaveConvention(
    oc::interface::IStorage& backend,
    core::midi::NoteOctaveConvention convention
) {
    if (!core::midi::validNoteOctaveConvention(convention)) {
        return PersistenceWriteStatus::INVALID_CONFIG;
    }
    const uint8_t encoded = static_cast<uint8_t>(convention);
    return writeExactStatus(
        backend,
        layout::ADDR_NOTE_OCTAVE_CONVENTION,
        &encoded,
        sizeof(encoded)
    );
}

FLASHMEM PersistenceWriteStatus saveAll(oc::interface::IStorage& backend,
                                        const state::MidiSyncState& midiSync,
                                        const state::MidiNoteDisplayState& noteDisplay) {
    if (!validMidiSyncState(midiSync) ||
        !core::midi::validNoteOctaveConvention(
            noteDisplay.octaveConvention.get()
        )) {
        return PersistenceWriteStatus::INVALID_CONFIG;
    }

    uint8_t encodedMagic[4]{};
    encodeU32LittleEndian(layout::MAGIC, encodedMagic);
    const uint8_t version = layout::VERSION;

    const auto headerStatus = writeExactStatus(
        backend,
        layout::ADDR_MAGIC,
        encodedMagic,
        sizeof(encodedMagic)
    );
    if (headerStatus != PersistenceWriteStatus::OK) return headerStatus;

    const auto versionStatus = writeExactStatus(backend, layout::ADDR_VERSION, &version, 1);
    if (versionStatus != PersistenceWriteStatus::OK) return versionStatus;

    const auto modeStatus =
        stageMidiSyncMode(backend, midiSync.mode.get());
    if (modeStatus != PersistenceWriteStatus::OK) return modeStatus;

    const auto followStatus =
        stageMidiFollowTransport(backend, midiSync.followTransport.get());
    if (followStatus != PersistenceWriteStatus::OK) return followStatus;

    const auto fallbackStatus =
        stageMidiAutoFallbackMs(backend, midiSync.autoFallbackMs.get());
    if (fallbackStatus != PersistenceWriteStatus::OK) return fallbackStatus;

    const auto lockStatus =
        stageMidiAutoLockClockCount(backend, midiSync.autoLockClockCount.get());
    if (lockStatus != PersistenceWriteStatus::OK) return lockStatus;

    const auto noteOctaveStatus = stageNoteOctaveConvention(
        backend,
        noteDisplay.octaveConvention.get()
    );
    if (noteOctaveStatus != PersistenceWriteStatus::OK) {
        return noteOctaveStatus;
    }

    return backend.commit() ? PersistenceWriteStatus::OK : PersistenceWriteStatus::COMMIT_FAILED;
}

FLASHMEM bool load(
    oc::interface::IStorage& backend,
    state::MidiSyncState& midiSync,
    state::MidiNoteDisplayState& noteDisplay
) {
    uint8_t rawMode = 0;
    if (!readExact(backend, layout::ADDR_SYNC_MODE, &rawMode, 1)) {
        return false;
    }

    uint8_t followTransport = 0;
    if (!readExact(backend, layout::ADDR_SYNC_FOLLOW_TRANSPORT, &followTransport, 1)) {
        return false;
    }

    uint8_t encodedFallbackMs[2]{};
    if (!readExact(
            backend,
            layout::ADDR_SYNC_AUTO_FALLBACK_MS,
            encodedFallbackMs,
            sizeof(encodedFallbackMs)
        )) {
        return false;
    }
    const uint16_t fallbackMs = decodeU16LittleEndian(encodedFallbackMs);

    uint8_t lockClocks = 0;
    if (!readExact(backend, layout::ADDR_SYNC_AUTO_LOCK_CLOCKS, &lockClocks, 1)) {
        return false;
    }

    uint8_t rawNoteOctaveConvention = 0;
    if (!readExact(
            backend,
            layout::ADDR_NOTE_OCTAVE_CONVENTION,
            &rawNoteOctaveConvention,
            1
        )) {
        return false;
    }

    const auto mode = static_cast<state::MidiSyncMode>(rawMode);
    const auto noteOctaveConvention =
        static_cast<core::midi::NoteOctaveConvention>(
            rawNoteOctaveConvention
        );
    if (!validMidiSyncMode(mode) ||
        followTransport > 1U ||
        !validMidiSyncAutoFallbackMs(fallbackMs) ||
        !validMidiSyncAutoLockClockCount(lockClocks) ||
        !core::midi::validNoteOctaveConvention(noteOctaveConvention)) {
        return false;
    }

    midiSync.mode.set(static_cast<state::MidiSyncMode>(rawMode));
    midiSync.followTransport.set(followTransport == 1U);
    midiSync.autoFallbackMs.set(fallbackMs);
    midiSync.autoLockClockCount.set(lockClocks);
    noteDisplay.octaveConvention.set(noteOctaveConvention);
    return true;
}

}  // namespace core::persistence::device_settings

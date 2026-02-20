#pragma once

/**
 * @file CoreSettings.hpp
 * @brief Incremental persistence for macro pages
 *
 * Storage layout in EEPROM (4KB available on Teensy 4.x):
 *
 * | Offset | Size | Content |
 * |--------|------|---------|
 * | 0x0000 | 4    | Magic number (0x4D435354 = "MCST") |
 * | 0x0004 | 1    | Version |
 * | 0x0005 | 1    | Active page index |
 * | 0x0006 | 1    | MIDI sync mode |
 * | 0x0007 | 1    | Follow transport flag |
 * | 0x0008 | 2    | Auto fallback timeout (ms) |
 * | 0x000A | 1    | Auto lock clock count |
 * | 0x000B | 5    | Reserved |
 * | 0x0010 | 512  | 8 pages × 64 bytes |
 *
 * Total: 528 bytes used.
 */

#include <cstdint>
#include <cstddef>
#include <cstring>

#include <oc/interface/IStorage.hpp>
#include <oc/log/Log.hpp>

#include "MidiSyncState.hpp"
#include "DataManagerState.hpp"
#include "macro/MacroPagesState.hpp"

namespace core::state {

/// Storage layout constants
namespace StorageLayout {
    constexpr uint32_t MAGIC = 0x4D435354;  ///< "MCST" in ASCII
    constexpr uint8_t VERSION = 3;

    constexpr uint32_t ADDR_MAGIC = 0x0000;
    constexpr uint32_t ADDR_VERSION = 0x0004;
    constexpr uint32_t ADDR_ACTIVE_PAGE = 0x0005;
    constexpr uint32_t ADDR_RESERVED = 0x0006;
    constexpr uint32_t ADDR_PAGES = 0x0010;

    constexpr uint32_t ADDR_SYNC_MODE = ADDR_RESERVED;
    constexpr uint32_t ADDR_SYNC_FOLLOW_TRANSPORT = ADDR_RESERVED + 1;
    constexpr uint32_t ADDR_SYNC_AUTO_FALLBACK_MS = ADDR_RESERVED + 2;   // uint16_t
    constexpr uint32_t ADDR_SYNC_AUTO_LOCK_CLOCKS = ADDR_RESERVED + 4;

    constexpr uint32_t ADDR_SHORTCUT_MACRO_LEFT = ADDR_RESERVED + 5;
    constexpr uint32_t ADDR_SHORTCUT_MACRO_RIGHT = ADDR_RESERVED + 6;
    constexpr uint32_t ADDR_SHORTCUT_SEQ_LEFT = ADDR_RESERVED + 7;
    constexpr uint32_t ADDR_SHORTCUT_SEQ_RIGHT = ADDR_RESERVED + 8;

    constexpr uint8_t DEFAULT_SHORTCUT_MACRO_LEFT =
        static_cast<uint8_t>(DataManagerCommand::MACRO_SAVE_SLOT);
    constexpr uint8_t DEFAULT_SHORTCUT_MACRO_RIGHT =
        static_cast<uint8_t>(DataManagerCommand::MACRO_LOAD_SLOT);
    constexpr uint8_t DEFAULT_SHORTCUT_SEQ_LEFT =
        static_cast<uint8_t>(DataManagerCommand::SEQ_SAVE_PATTERN_SLOT);
    constexpr uint8_t DEFAULT_SHORTCUT_SEQ_RIGHT =
        static_cast<uint8_t>(DataManagerCommand::SEQ_LOAD_PATTERN_SLOT);

    // Note: Named MACRO_PAGE_SIZE to avoid conflict with system PAGE_SIZE macro (Emscripten)
    constexpr size_t MACRO_PAGE_SIZE = sizeof(macro::MacroPageData);  // 64 bytes
    static_assert(MACRO_PAGE_SIZE == 64, "Page size must be 64 bytes");

    // Offsets within MacroPageData (avoid magic numbers that must match struct layout).
    static constexpr uint32_t OFF_NAME = static_cast<uint32_t>(offsetof(macro::MacroPageData, name));
    static constexpr uint32_t OFF_CC = static_cast<uint32_t>(offsetof(macro::MacroPageData, cc));
    static constexpr uint32_t OFF_CHANNEL = static_cast<uint32_t>(offsetof(macro::MacroPageData, channel));
    static constexpr uint32_t OFF_VALUES = static_cast<uint32_t>(offsetof(macro::MacroPageData, values));

    static_assert(OFF_NAME == 0, "Unexpected MacroPageData layout");
    static_assert(OFF_CC == 16, "Unexpected MacroPageData layout");
    static_assert(OFF_CHANNEL == 24, "Unexpected MacroPageData layout");
    static_assert(OFF_VALUES == 32, "Unexpected MacroPageData layout");

    /// Get offset for a specific page
    inline constexpr uint32_t pageOffset(uint8_t pageIndex) {
        return ADDR_PAGES + pageIndex * MACRO_PAGE_SIZE;
    }

    /// Get offset for a specific value within a page
    inline constexpr uint32_t valueOffset(uint8_t pageIndex, uint8_t macroIndex) {
        return pageOffset(pageIndex) + OFF_VALUES + macroIndex * sizeof(float);
    }

    /// Get offset for CC number
    inline constexpr uint32_t ccOffset(uint8_t pageIndex, uint8_t macroIndex) {
        return pageOffset(pageIndex) + OFF_CC + macroIndex;
    }

    /// Get offset for channel
    inline constexpr uint32_t channelOffset(uint8_t pageIndex, uint8_t macroIndex) {
        return pageOffset(pageIndex) + OFF_CHANNEL + macroIndex;
    }
}

/**
 * @brief Manages persistence of macro page settings to EEPROM
 *
 * Supports incremental saves:
 * - Single byte for CC/channel changes
 * - 4 bytes for value changes
 * - 64 bytes for full page saves
 *
     * Value changes use a dirty timer to batch writes (see VALUE_SAVE_DELAY_MS).
     */
class CoreSettings {
public:
    static constexpr uint32_t VALUE_SAVE_DELAY_MS = 300;  ///< Delay before saving values

    explicit CoreSettings(oc::interface::IStorage& backend)
        : backend_(backend) {}

    // Non-copyable
    CoreSettings(const CoreSettings&) = delete;
    CoreSettings& operator=(const CoreSettings&) = delete;

    /**
     * @brief Load all settings from storage
     * @param pages State to populate
     * @return true if valid data was loaded, false if defaults used
     */
    bool load(macro::MacroPagesState& pages, MidiSyncState& midiSync) {
        // Check magic number
        uint32_t magic = 0;
        backend_.read(StorageLayout::ADDR_MAGIC, reinterpret_cast<uint8_t*>(&magic), sizeof(magic));

        if (magic != StorageLayout::MAGIC) {
            OC_LOG_INFO("[CoreSettings] No valid data, using defaults");
            pages.initDefaults();
            midiSync.reset();
            saveAll(pages, midiSync);  // Initialize storage
            return false;
        }

        // Check version
        uint8_t version = 0;
        backend_.read(StorageLayout::ADDR_VERSION, &version, 1);
        if (version == StorageLayout::VERSION) {
            loadPages_(pages);
            loadMidiSync_(midiSync);

            OC_LOG_INFO("[CoreSettings] Loaded page {}", pages.activePage);
            return true;
        }

        if (version == 2) {
            loadPages_(pages);
            loadMidiSync_(midiSync);
            saveAll(pages, midiSync);
            OC_LOG_INFO("[CoreSettings] Migrated settings v2 -> v{}", StorageLayout::VERSION);
            return true;
        }

        if (version == 1) {
            loadPages_(pages);
            midiSync.reset();
            saveAll(pages, midiSync);
            OC_LOG_INFO("[CoreSettings] Migrated settings v1 -> v{}", StorageLayout::VERSION);
            return true;
        }

        OC_LOG_WARN("[CoreSettings] Version mismatch ({} vs {}), using defaults",
                    version, StorageLayout::VERSION);
        pages.initDefaults();
        midiSync.reset();
        saveAll(pages, midiSync);
        return false;
    }

    /**
     * @brief Save all settings to storage
     */
    void saveAll(const macro::MacroPagesState& pages, const MidiSyncState& midiSync) {
        // Write header
        uint32_t magic = StorageLayout::MAGIC;
        uint8_t version = StorageLayout::VERSION;
        backend_.write(StorageLayout::ADDR_MAGIC, reinterpret_cast<uint8_t*>(&magic), sizeof(magic));
        backend_.write(StorageLayout::ADDR_VERSION, &version, 1);
        backend_.write(StorageLayout::ADDR_ACTIVE_PAGE, &pages.activePage, 1);

        const uint8_t mode = static_cast<uint8_t>(midiSync.mode.get());
        const uint8_t followTransport = midiSync.followTransport.get() ? 1 : 0;
        const uint16_t fallbackMs = midiSync.autoFallbackMs.get();
        const uint8_t lockClocks = midiSync.autoLockClockCount.get();

        backend_.write(StorageLayout::ADDR_SYNC_MODE, reinterpret_cast<const uint8_t*>(&mode), 1);
        backend_.write(StorageLayout::ADDR_SYNC_FOLLOW_TRANSPORT,
                       reinterpret_cast<const uint8_t*>(&followTransport),
                       1);
        backend_.write(StorageLayout::ADDR_SYNC_AUTO_FALLBACK_MS,
                       reinterpret_cast<const uint8_t*>(&fallbackMs),
                       sizeof(fallbackMs));
        backend_.write(StorageLayout::ADDR_SYNC_AUTO_LOCK_CLOCKS,
                       reinterpret_cast<const uint8_t*>(&lockClocks),
                       1);

        writeDefaultShortcuts_();

        // Write all pages
        for (uint8_t i = 0; i < macro::PAGE_COUNT; ++i) {
            backend_.write(
                StorageLayout::pageOffset(i),
                reinterpret_cast<const uint8_t*>(&pages.pages[i]),
                StorageLayout::MACRO_PAGE_SIZE
            );
        }

        backend_.commit();
        OC_LOG_DEBUG("[CoreSettings] Saved all");
    }

    void saveMidiSyncMode(MidiSyncMode mode) {
        const uint8_t value = static_cast<uint8_t>(mode);
        backend_.write(StorageLayout::ADDR_SYNC_MODE, reinterpret_cast<const uint8_t*>(&value), 1);
    }

    void saveMidiFollowTransport(bool followTransport) {
        const uint8_t value = followTransport ? 1 : 0;
        backend_.write(StorageLayout::ADDR_SYNC_FOLLOW_TRANSPORT,
                       reinterpret_cast<const uint8_t*>(&value),
                       1);
    }

    void saveMidiAutoFallbackMs(uint16_t fallbackMs) {
        backend_.write(StorageLayout::ADDR_SYNC_AUTO_FALLBACK_MS,
                       reinterpret_cast<const uint8_t*>(&fallbackMs),
                       sizeof(fallbackMs));
    }

    void saveMidiAutoLockClockCount(uint8_t lockCount) {
        backend_.write(StorageLayout::ADDR_SYNC_AUTO_LOCK_CLOCKS,
                       reinterpret_cast<const uint8_t*>(&lockCount),
                       1);
    }

    void saveDataManagerMacroShortcutLeft(uint8_t command) {
        backend_.write(StorageLayout::ADDR_SHORTCUT_MACRO_LEFT,
                       reinterpret_cast<const uint8_t*>(&command),
                       1);
    }

    void saveDataManagerMacroShortcutRight(uint8_t command) {
        backend_.write(StorageLayout::ADDR_SHORTCUT_MACRO_RIGHT,
                       reinterpret_cast<const uint8_t*>(&command),
                       1);
    }

    void saveDataManagerSeqShortcutLeft(uint8_t command) {
        backend_.write(StorageLayout::ADDR_SHORTCUT_SEQ_LEFT,
                       reinterpret_cast<const uint8_t*>(&command),
                       1);
    }

    void saveDataManagerSeqShortcutRight(uint8_t command) {
        backend_.write(StorageLayout::ADDR_SHORTCUT_SEQ_RIGHT,
                       reinterpret_cast<const uint8_t*>(&command),
                       1);
    }

    void loadDataManagerShortcuts(uint8_t& macroLeft,
                                  uint8_t& macroRight,
                                  uint8_t& seqLeft,
                                  uint8_t& seqRight) {
        macroLeft = StorageLayout::DEFAULT_SHORTCUT_MACRO_LEFT;
        macroRight = StorageLayout::DEFAULT_SHORTCUT_MACRO_RIGHT;
        seqLeft = StorageLayout::DEFAULT_SHORTCUT_SEQ_LEFT;
        seqRight = StorageLayout::DEFAULT_SHORTCUT_SEQ_RIGHT;

        uint8_t version = 0;
        backend_.read(StorageLayout::ADDR_VERSION, &version, 1);
        if (version < StorageLayout::VERSION) {
            return;
        }

        backend_.read(StorageLayout::ADDR_SHORTCUT_MACRO_LEFT, &macroLeft, 1);
        backend_.read(StorageLayout::ADDR_SHORTCUT_MACRO_RIGHT, &macroRight, 1);
        backend_.read(StorageLayout::ADDR_SHORTCUT_SEQ_LEFT, &seqLeft, 1);
        backend_.read(StorageLayout::ADDR_SHORTCUT_SEQ_RIGHT, &seqRight, 1);
    }

    /**
     * @brief Save active page index only
     */
    void saveActivePage(uint8_t pageIndex) {
        backend_.write(StorageLayout::ADDR_ACTIVE_PAGE, &pageIndex, 1);
        OC_LOG_DEBUG("[CoreSettings] Saved active page: {}", pageIndex);
    }

    /**
     * @brief Save a complete page
     */
    void savePage(uint8_t pageIndex, const macro::MacroPageData& page) {
        backend_.write(
            StorageLayout::pageOffset(pageIndex),
            reinterpret_cast<const uint8_t*>(&page),
            StorageLayout::MACRO_PAGE_SIZE
        );
        OC_LOG_DEBUG("[CoreSettings] Saved page {}", pageIndex);
    }

    /**
     * @brief Save a single macro value (deferred via dirty flag)
     */
    void saveValue(uint8_t pageIndex, uint8_t macroIndex, float value) {
        backend_.write(
            StorageLayout::valueOffset(pageIndex, macroIndex),
            reinterpret_cast<const uint8_t*>(&value),
            sizeof(float)
        );
    }

    /**
     * @brief Save CC number for a macro
     */
    void saveCC(uint8_t pageIndex, uint8_t macroIndex, uint8_t cc) {
        backend_.write(StorageLayout::ccOffset(pageIndex, macroIndex), &cc, 1);
        OC_LOG_DEBUG("[CoreSettings] Saved CC[{}][{}] = {}", pageIndex, macroIndex, cc);
    }

    /**
     * @brief Save channel for a macro
     */
    void saveChannel(uint8_t pageIndex, uint8_t macroIndex, uint8_t channel) {
        backend_.write(StorageLayout::channelOffset(pageIndex, macroIndex), &channel, 1);
        OC_LOG_DEBUG("[CoreSettings] Saved CH[{}][{}] = {}", pageIndex, macroIndex, channel);
    }

    /**
     * @brief Commit pending writes to storage
     */
    void commit() {
        backend_.commit();
    }

    /**
     * @brief Erase all settings (factory reset)
     */
    void factoryReset() {
        backend_.erase(0, StorageLayout::ADDR_PAGES + macro::PAGE_COUNT * StorageLayout::MACRO_PAGE_SIZE);
        OC_LOG_INFO("[CoreSettings] Factory reset");
    }

private:
    void writeDefaultShortcuts_() {
        const uint8_t macroLeft = StorageLayout::DEFAULT_SHORTCUT_MACRO_LEFT;
        const uint8_t macroRight = StorageLayout::DEFAULT_SHORTCUT_MACRO_RIGHT;
        const uint8_t seqLeft = StorageLayout::DEFAULT_SHORTCUT_SEQ_LEFT;
        const uint8_t seqRight = StorageLayout::DEFAULT_SHORTCUT_SEQ_RIGHT;

        backend_.write(StorageLayout::ADDR_SHORTCUT_MACRO_LEFT,
                       reinterpret_cast<const uint8_t*>(&macroLeft),
                       1);
        backend_.write(StorageLayout::ADDR_SHORTCUT_MACRO_RIGHT,
                       reinterpret_cast<const uint8_t*>(&macroRight),
                       1);
        backend_.write(StorageLayout::ADDR_SHORTCUT_SEQ_LEFT,
                       reinterpret_cast<const uint8_t*>(&seqLeft),
                       1);
        backend_.write(StorageLayout::ADDR_SHORTCUT_SEQ_RIGHT,
                       reinterpret_cast<const uint8_t*>(&seqRight),
                       1);
    }

    void loadPages_(macro::MacroPagesState& pages) {
        uint8_t activePage = 0;
        backend_.read(StorageLayout::ADDR_ACTIVE_PAGE, &activePage, 1);
        if (activePage >= macro::PAGE_COUNT) activePage = 0;

        for (uint8_t i = 0; i < macro::PAGE_COUNT; ++i) {
            backend_.read(
                StorageLayout::pageOffset(i),
                reinterpret_cast<uint8_t*>(&pages.pages[i]),
                StorageLayout::MACRO_PAGE_SIZE
            );
        }

        pages.activePage = activePage;
        pages.updateActiveConfigs();
    }

    void loadMidiSync_(MidiSyncState& midiSync) {
        uint8_t rawMode = static_cast<uint8_t>(MidiSyncMode::AUTO);
        backend_.read(StorageLayout::ADDR_SYNC_MODE, &rawMode, 1);
        if (rawMode > static_cast<uint8_t>(MidiSyncMode::AUTO)) {
            rawMode = static_cast<uint8_t>(MidiSyncMode::AUTO);
        }

        uint8_t followTransport = 1;
        backend_.read(StorageLayout::ADDR_SYNC_FOLLOW_TRANSPORT, &followTransport, 1);

        uint16_t fallbackMs = 500;
        backend_.read(StorageLayout::ADDR_SYNC_AUTO_FALLBACK_MS,
                      reinterpret_cast<uint8_t*>(&fallbackMs),
                      sizeof(fallbackMs));
        if (fallbackMs < 100) fallbackMs = 100;
        if (fallbackMs > 5000) fallbackMs = 5000;

        uint8_t lockClocks = 6;
        backend_.read(StorageLayout::ADDR_SYNC_AUTO_LOCK_CLOCKS, &lockClocks, 1);
        if (lockClocks < 1) lockClocks = 1;
        if (lockClocks > 96) lockClocks = 96;

        midiSync.reset();
        midiSync.mode.set(static_cast<MidiSyncMode>(rawMode));
        midiSync.followTransport.set(followTransport != 0);
        midiSync.autoFallbackMs.set(fallbackMs);
        midiSync.autoLockClockCount.set(lockClocks);
    }

    oc::interface::IStorage& backend_;
};

}  // namespace core::state

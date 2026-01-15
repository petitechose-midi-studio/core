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
 * | 0x0006 | 10   | Reserved |
 * | 0x0010 | 512  | 8 pages × 64 bytes |
 *
 * Total: 528 bytes used.
 */

#include <cstdint>
#include <cstring>

#include <oc/hal/IStorageBackend.hpp>
#include <oc/log/Log.hpp>

#include "macro/MacroPagesState.hpp"

namespace core::state {

/// Storage layout constants
namespace StorageLayout {
    constexpr uint32_t MAGIC = 0x4D435354;  ///< "MCST" in ASCII
    constexpr uint8_t VERSION = 1;

    constexpr uint32_t ADDR_MAGIC = 0x0000;
    constexpr uint32_t ADDR_VERSION = 0x0004;
    constexpr uint32_t ADDR_ACTIVE_PAGE = 0x0005;
    constexpr uint32_t ADDR_RESERVED = 0x0006;
    constexpr uint32_t ADDR_PAGES = 0x0010;

    // Note: Named MACRO_PAGE_SIZE to avoid conflict with system PAGE_SIZE macro (Emscripten)
    constexpr size_t MACRO_PAGE_SIZE = sizeof(macro::MacroPageData);  // 64 bytes
    static_assert(MACRO_PAGE_SIZE == 64, "Page size must be 64 bytes");

    /// Get offset for a specific page
    inline constexpr uint32_t pageOffset(uint8_t pageIndex) {
        return ADDR_PAGES + pageIndex * MACRO_PAGE_SIZE;
    }

    /// Get offset for a specific value within a page
    inline constexpr uint32_t valueOffset(uint8_t pageIndex, uint8_t macroIndex) {
        // values start at offset 32 within MacroPageData (after name[16] + cc[8] + channel[8])
        return pageOffset(pageIndex) + 32 + macroIndex * sizeof(float);
    }

    /// Get offset for CC number
    inline constexpr uint32_t ccOffset(uint8_t pageIndex, uint8_t macroIndex) {
        return pageOffset(pageIndex) + 16 + macroIndex;  // After name[16]
    }

    /// Get offset for channel
    inline constexpr uint32_t channelOffset(uint8_t pageIndex, uint8_t macroIndex) {
        return pageOffset(pageIndex) + 24 + macroIndex;  // After name[16] + cc[8]
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
 * Value changes use a dirty timer to batch writes (3s timeout).
 */
class CoreSettings {
public:
    static constexpr uint32_t VALUE_SAVE_DELAY_MS = 300;  ///< Delay before saving values

    explicit CoreSettings(oc::hal::IStorageBackend& backend)
        : backend_(backend) {}

    // Non-copyable
    CoreSettings(const CoreSettings&) = delete;
    CoreSettings& operator=(const CoreSettings&) = delete;

    /**
     * @brief Load all settings from storage
     * @param pages State to populate
     * @return true if valid data was loaded, false if defaults used
     */
    bool load(macro::MacroPagesState& pages) {
        // Check magic number
        uint32_t magic = 0;
        backend_.read(StorageLayout::ADDR_MAGIC, reinterpret_cast<uint8_t*>(&magic), sizeof(magic));

        if (magic != StorageLayout::MAGIC) {
            OC_LOG_INFO("[CoreSettings] No valid data, using defaults");
            pages.initDefaults();
            saveAll(pages);  // Initialize storage
            return false;
        }

        // Check version
        uint8_t version = 0;
        backend_.read(StorageLayout::ADDR_VERSION, &version, 1);
        if (version != StorageLayout::VERSION) {
            OC_LOG_WARN("[CoreSettings] Version mismatch ({} vs {}), using defaults",
                        version, StorageLayout::VERSION);
            pages.initDefaults();
            saveAll(pages);
            return false;
        }

        // Load active page
        uint8_t activePage = 0;
        backend_.read(StorageLayout::ADDR_ACTIVE_PAGE, &activePage, 1);
        if (activePage >= macro::PAGE_COUNT) activePage = 0;

        // Load all pages
        for (uint8_t i = 0; i < macro::PAGE_COUNT; ++i) {
            backend_.read(
                StorageLayout::pageOffset(i),
                reinterpret_cast<uint8_t*>(&pages.pages[i]),
                StorageLayout::MACRO_PAGE_SIZE
            );
        }

        pages.activePage = activePage;
        pages.updateActiveConfigs();

        OC_LOG_INFO("[CoreSettings] Loaded page {}", activePage);
        return true;
    }

    /**
     * @brief Save all settings to storage
     */
    void saveAll(const macro::MacroPagesState& pages) {
        // Write header
        uint32_t magic = StorageLayout::MAGIC;
        uint8_t version = StorageLayout::VERSION;
        backend_.write(StorageLayout::ADDR_MAGIC, reinterpret_cast<uint8_t*>(&magic), sizeof(magic));
        backend_.write(StorageLayout::ADDR_VERSION, &version, 1);
        backend_.write(StorageLayout::ADDR_ACTIVE_PAGE, &pages.activePage, 1);

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
    oc::hal::IStorageBackend& backend_;
};

}  // namespace core::state

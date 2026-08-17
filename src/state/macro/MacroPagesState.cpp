#include "state/macro/MacroPagesState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

FLASHMEM MacroPageData::MacroPageData() {
    std::memset(name, 0, PAGE_NAME_SIZE);
    std::strncpy(name, "Page 1", PAGE_NAME_SIZE - 1);
    cc.fill(0);
    values.fill(0.5f);
    activeMacroMask = DEFAULT_ACTIVE_MACRO_MASK;
}

FLASHMEM void MacroPageData::initDefault(uint8_t pageIndex) {
    std::memset(name, 0, PAGE_NAME_SIZE);
    size_t pos = oc::type::text::appendString(name, PAGE_NAME_SIZE, 0, "Page ");
    pos = oc::type::text::appendUnsigned(name, PAGE_NAME_SIZE, pos, pageIndex + 1);
    oc::type::text::terminate(name, PAGE_NAME_SIZE, pos);

    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        cc[i] = defaultMacroCc(pageIndex, i);
        values[i] = 0.5f;
    }
    activeMacroMask = DEFAULT_ACTIVE_MACRO_MASK;
}

FLASHMEM void MacroPageData::initEmpty(uint8_t pageIndex) {
    initDefault(pageIndex);
    activeMacroMask = 0U;
}

FLASHMEM MacroTrackData::MacroTrackData() {
    initDefaults(0);
}

FLASHMEM void MacroTrackData::initDefaults(uint8_t) {
    activePage = 0;
    enabledPageMask = 0x0001;
    for (uint8_t i = 0; i < PAGE_COUNT; ++i) {
        pages[i].initDefault(i);
    }
}

FLASHMEM bool MacroTrackData::compactPages(uint16_t retainedPageMask) {
    const uint16_t retained = static_cast<uint16_t>(
        enabledPageMask & retainedPageMask
    );
    if (retained == 0U || retained == enabledPageMask) return false;

    uint8_t retainedCount = 0U;
    uint8_t nextActive = 0U;
    bool activeResolved = false;
    for (uint8_t page = 0U; page < PAGE_COUNT; ++page) {
        if ((retained & static_cast<uint16_t>(1U << page)) == 0U) continue;
        if (!activeResolved && page >= activePage) {
            nextActive = retainedCount;
            activeResolved = true;
        }
        ++retainedCount;
    }
    if (!activeResolved) {
        nextActive = static_cast<uint8_t>(retainedCount - 1U);
    }

    uint8_t write = 0U;
    for (uint8_t read = 0U; read < PAGE_COUNT; ++read) {
        if ((retained & static_cast<uint16_t>(1U << read)) == 0U) continue;
        if (write != read) pages[write] = pages[read];
        ++write;
    }
    for (uint8_t page = write; page < PAGE_COUNT; ++page) {
        pages[page].initDefault(page);
    }
    enabledPageMask = write >= 16U
        ? 0xFFFFU
        : static_cast<uint16_t>((1U << write) - 1U);
    activePage = nextActive;
    return true;
}

FLASHMEM MacroPagesState::MacroPagesState() {
    initDefaults();
}

FLASHMEM void MacroPagesState::initDefaults() {
    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        tracks[i].initDefaults(i);
    }
    control.clear();
    active_track_ = 0;
    active_page_ = 0;
    track_enabled_mask_.set(DEFAULT_TRACK_ENABLED_MASK);
    syncActiveTrackCache();
    updateActiveConfigs();
}

FLASHMEM void MacroPagesState::syncSharedTrackState(uint16_t enabledTrackMask,
                                                    uint8_t trackIndex) {
    const uint16_t sanitizedMask = sanitizeTrackEnabledMask(enabledTrackMask);
    const uint8_t sanitizedTrack = sanitizeActiveTrack(sanitizedMask, trackIndex);

    if (track_enabled_mask_.get() != sanitizedMask) {
        track_enabled_mask_.set(sanitizedMask);
    }

    active_track_ = sanitizedTrack;
    syncActiveTrackCache();
    updateActiveConfigs();
}

FLASHMEM void MacroPagesState::captureSharedTrackState(
    uint16_t& enabledTrackMaskOut,
    uint8_t& activeTrackOut
) const {
    enabledTrackMaskOut = track_enabled_mask_.get();
    activeTrackOut = active_track_;
}

FLASHMEM void MacroPagesState::restoreTracksWithSharedState(
    const std::array<MacroTrackData, TRACK_COUNT>& persistedTracks,
    uint16_t enabledTrackMaskIn,
    uint8_t activeTrackIn
) {
    tracks = persistedTracks;
    syncSharedTrackState(enabledTrackMaskIn, activeTrackIn);
}

FLASHMEM void MacroPagesState::setActivePage(uint8_t index) {
    if (index >= PAGE_COUNT) return;
    tracks[active_track_].activePage = index;
    active_page_ = index;
    active_page_index_.set(active_page_);
    updateActiveConfigs();
}

FLASHMEM void MacroPagesState::updateActiveConfigs() {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        activeConfigs[i] = {activePageData().cc[i]};
    }
}

FLASHMEM void MacroPagesState::syncActiveTrackCache() {
    active_page_ = activeTrackData().activePage;
    active_page_index_.set(active_page_);
    enabled_mask_.set(activeTrackData().enabledPageMask);
}

}  // namespace core::state::macro

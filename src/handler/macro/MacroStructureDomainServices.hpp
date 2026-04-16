#pragma once

#include <cstdint>

#include "state/macro/MacroPagesState.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

class MacroStructureDomainServices {
public:
    explicit MacroStructureDomainServices(core::state::CoreState& state);
    static MacroStructureDomainServices fromCoreState(core::state::CoreState& state);

    void switchToPage(uint8_t pageIndex) const;
    void switchToTrack(uint8_t trackIndex) const;
    uint8_t activeTrack() const;
    uint16_t pageEnabledMask() const;
    uint16_t trackEnabledMask() const;
    bool deleteActivePage() const;
    bool deleteActiveTrack() const;
    bool deleteSelectedPages(uint16_t selectedMask) const;
    bool deleteSelectedTracks(uint16_t selectedMask) const;
    bool duplicateSelectedPages(uint16_t selectedMask) const;
    bool duplicateSelectedTracks(uint16_t selectedMask) const;
    bool erasePage(uint8_t pageIndex) const;
    bool eraseTrack(uint8_t trackIndex) const;
    bool pastePage(uint8_t pageIndex, const core::state::macro::MacroPageData& pageData) const;
    bool pasteTrack(uint8_t trackIndex, const core::state::macro::MacroTrackData& trackData) const;
    bool createNextPage() const;
    bool createTrack(uint8_t trackIndex) const;

private:
    core::state::CoreState* state_ = nullptr;
};

}  // namespace core::handler

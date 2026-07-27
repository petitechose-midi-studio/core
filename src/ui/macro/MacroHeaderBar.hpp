#pragma once

#include <array>
#include <cstdint>
#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "ui/common/TrackHeaderRow.hpp"

namespace core::ui {

struct MacroHeaderBarProps {
    static constexpr uint8_t ACTIVITY_COUNT = core::state::macro::PAGE_COUNT;

    uint8_t activeTrack = 0;
    uint8_t previewTrack = 0;
    uint8_t activePage = 0;
    uint8_t previewPage = 0;
    uint8_t addPageIndex = core::state::macro::PAGE_COUNT;
    uint8_t addTrackIndex = core::state::macro::TRACK_COUNT;
    uint16_t enabledMask = 0x0001;
    uint16_t trackEnabledMask = 0x0001;
    core::state::macro::MacroPerformanceOverlayMode performanceOverlayMode =
        core::state::macro::MacroPerformanceOverlayMode::NONE;
    core::state::macro::MacroAutomationTakePhase automationTakePhase =
        core::state::macro::MacroAutomationTakePhase::IDLE;
    core::state::macro::MacroAutomationTakeTiming automationTakeTiming =
        core::state::macro::MacroAutomationTakeTiming::HOLD;
    uint16_t automationTakeTouchedMask = 0;
    bool focusingPage = false;
    bool focusingTrack = false;
    bool slotSelectionActive = false;
    bool pageSelectionActive = false;
    bool previewPageAddSlot = false;
    bool previewTrackAddSlot = false;
    core::state::macro::MacroAutomationRecordingStatus automationRecordingStatus =
        core::state::macro::MacroAutomationRecordingStatus::IDLE;
    uint16_t pageSelectedMask = 0U;
    uint16_t pageDestinationMask = 0U;
    uint16_t pageOverwriteMask = 0U;
    uint16_t pageBlockedMask = 0U;
    std::array<uint8_t, ACTIVITY_COUNT> pageOutputActivity{};
};

class MacroHeaderBar : public oc::ui::lvgl::IWidget {
public:
    explicit MacroHeaderBar(lv_obj_t* parent);
    ~MacroHeaderBar() override;

    MacroHeaderBar(const MacroHeaderBar&) = delete;
    MacroHeaderBar& operator=(const MacroHeaderBar&) = delete;

    void render(const MacroHeaderBarProps& props);
    lv_obj_t* getElement() const override { return container_; }

private:
    void createUI(lv_obj_t* parent);

    lv_obj_t* container_ = nullptr;
    core::app::ExtmemUniquePtr<TrackHeaderRow> header_row_;
};

}  // namespace core::ui

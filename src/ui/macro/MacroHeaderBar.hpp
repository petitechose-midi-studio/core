#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/macro/MacroPagesState.hpp"
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
    uint16_t selectedPageMask = 0;
    bool clutchActive = false;
    bool focusingPage = false;
    bool focusingTrack = false;
    bool selectingPage = false;
    bool selectingTrack = false;
    bool previewPageAddSlot = false;
    bool previewTrackAddSlot = false;
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
    std::unique_ptr<TrackHeaderRow> header_row_;
};

}  // namespace core::ui

#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "ui/common/TrackHeaderRow.hpp"

namespace core::ui {

struct MacroHeaderBarProps {
    static constexpr uint8_t ACTIVITY_COUNT = 8;

    uint8_t activeTrack = 0;
    uint8_t activePage = 0;
    uint8_t previewPage = 0;
    uint8_t enabledMask = 0xFF;
    uint16_t trackEnabledMask = 0x01;
    bool clutchActive = false;
    bool selectingPage = false;
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

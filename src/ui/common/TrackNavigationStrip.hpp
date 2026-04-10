#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

#include "state/StatusBarState.hpp"

namespace core::ui {

struct TrackNavigationStripProps {
    static constexpr uint8_t TRACK_COUNT = core::state::StatusBarState::TRACK_COUNT;

    uint8_t activeTrack = 0;
    uint8_t previewTrack = 0;
    uint8_t addTrackIndex = TRACK_COUNT;
    uint16_t enabledMask = 0x0001;
    uint16_t selectedMask = 0;
    bool focusingTrack = false;
    bool selectingTrack = false;
    std::array<uint8_t, TRACK_COUNT> activity{};
};

class TrackNavigationStrip : public oc::ui::lvgl::IWidget {
public:
    explicit TrackNavigationStrip(lv_obj_t* parent);
    ~TrackNavigationStrip() override;

    TrackNavigationStrip(const TrackNavigationStrip&) = delete;
    TrackNavigationStrip& operator=(const TrackNavigationStrip&) = delete;

    void render(const TrackNavigationStripProps& props);
    lv_obj_t* getElement() const override { return container_; }

private:
    void createUI(lv_obj_t* parent);

    lv_obj_t* container_ = nullptr;
    lv_obj_t* items_row_ = nullptr;
    lv_obj_t* selection_cursor_ = nullptr;
    std::array<lv_obj_t*, TrackNavigationStripProps::TRACK_COUNT> items_{};
    std::array<lv_obj_t*, TrackNavigationStripProps::TRACK_COUNT> item_add_labels_{};
};

}  // namespace core::ui

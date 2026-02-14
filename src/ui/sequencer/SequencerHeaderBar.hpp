#pragma once

/**
 * @file SequencerHeaderBar.hpp
 * @brief Sequencer header: title + division/length + playhead progress strip
 */

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

namespace core::ui {

struct SequencerHeaderBarProps {
    uint8_t length = 0;
    uint8_t viewedPage = 0;     // 0..7
    int16_t playheadStep = -1;  // -1 when stopped
    uint8_t stepsPerBeat = 0;
};

/**
 * @brief Stateless header widget rendered from Sequencer state
 *
 * Pattern: stateless + render(props), similar to plugin-bitwig DeviceStateBar.
 */
class SequencerHeaderBar : public oc::ui::lvgl::IWidget {
public:
    explicit SequencerHeaderBar(lv_obj_t* parent);
    ~SequencerHeaderBar() override;

    SequencerHeaderBar(const SequencerHeaderBar&) = delete;
    SequencerHeaderBar& operator=(const SequencerHeaderBar&) = delete;

    void render(const SequencerHeaderBarProps& props);

    lv_obj_t* getElement() const override { return container_; }

private:
    static constexpr uint8_t PAGE_COUNT = 8;
    static constexpr uint8_t STEPS_PER_PAGE = 8;
    static constexpr lv_coord_t STRIP_HEIGHT = 3;
    static constexpr lv_coord_t MARKER_WIDTH = 2;

    static uint16_t stepDivisionDenom(uint8_t stepsPerBeat);

    void createUI(lv_obj_t* parent);
    void renderTopRow(const SequencerHeaderBarProps& props);
    void renderStrip(const SequencerHeaderBarProps& props);

    struct Segment {
        lv_obj_t* container = nullptr;  // full width, disabled background
        lv_obj_t* valid = nullptr;      // valid range baseline
        lv_obj_t* progress = nullptr;   // progress fill (before playhead)
        lv_obj_t* marker = nullptr;     // playhead marker (hidden when stopped)
    };

    lv_obj_t* container_ = nullptr;
    lv_obj_t* top_row_ = nullptr;
    lv_obj_t* strip_row_ = nullptr;

    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* info_label_ = nullptr;

    std::array<Segment, PAGE_COUNT> segments_{};
};

}  // namespace core::ui

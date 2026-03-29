#pragma once

/**
 * @file SequencerHeaderBar.hpp
 * @brief Sequencer header: text row + playhead progress strip
 */

#include <array>
#include <cstdint>

#include <lvgl.h>

#include <oc/ui/lvgl/IWidget.hpp>

namespace core::ui {

struct SequencerHeaderBarProps {
    uint8_t length = 0;
    uint8_t viewedPage = 0;     // 0..7, may point to a future paste target page
    int16_t playheadStep = -1;  // -1 when stopped
    const char* leftText = "";
    const char* centerText = "";
    const char* rightText = "";
    bool dimmed = false;
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
    static constexpr lv_coord_t TOP_ROW_HEIGHT = 14;
    static constexpr lv_coord_t STRIP_HEIGHT = 3;
    static constexpr lv_coord_t ROW_GAP = 2;
    static constexpr lv_coord_t MARKER_WIDTH = 2;

    void createUI(lv_obj_t* parent);
    void renderTopRow(const SequencerHeaderBarProps& props);
    void renderStrip(const SequencerHeaderBarProps& props);

    struct Segment {
        lv_obj_t* container = nullptr;  // full width, disabled background
        lv_obj_t* valid = nullptr;      // valid range baseline
        lv_obj_t* progress = nullptr;   // progress fill (before playhead)
        lv_obj_t* marker = nullptr;     // playhead marker (hidden when stopped)
    };

    struct SegmentRenderCache {
        bool initialized = false;
        lv_coord_t width = -1;
        lv_coord_t validWidth = -1;
        lv_coord_t progressWidth = -1;
        bool markerVisible = false;
        lv_coord_t markerX = -1;
        uint32_t validColorHex = 0;
        uint32_t progressColorHex = 0;
    };

    lv_obj_t* container_ = nullptr;
    lv_obj_t* top_row_ = nullptr;
    lv_obj_t* strip_row_ = nullptr;

    lv_obj_t* left_label_ = nullptr;
    lv_obj_t* center_label_ = nullptr;
    lv_obj_t* right_label_ = nullptr;

    std::array<Segment, PAGE_COUNT> segments_{};
    std::array<SegmentRenderCache, PAGE_COUNT> segment_cache_{};

    bool top_row_cache_initialized_ = false;
    bool top_row_dimmed_ = false;
    std::array<char, 32> left_text_cache_{};
    std::array<char, 32> center_text_cache_{};
    std::array<char, 32> right_text_cache_{};

    bool strip_cache_initialized_ = false;
    uint8_t strip_cached_length_ = 0;
    uint8_t strip_cached_viewed_page_ = 0;
    int16_t strip_cached_playhead_ = -2;
    lv_coord_t strip_cached_width_ = -1;
};

}  // namespace core::ui

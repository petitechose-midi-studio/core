#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <lvgl.h>

namespace core::ui {

struct SequencerChordVoiceRailItem {
    const char* label = "";
    const char* value = "";
    bool add = false;
    bool enabled = true;
};

struct SequencerChordVoiceRailProps {
    static constexpr std::size_t MAX_ITEMS = 8;

    bool visible = false;
    uint8_t itemCount = 0;
    uint8_t focusedItem = 0;
    uint32_t color = 0;
    std::array<SequencerChordVoiceRailItem, MAX_ITEMS> items{};
};

/**
 * Allocation-free, custom-drawn Formula rail.
 *
 * One LVGL object renders Root, V2..V8 and the optional trailing Add item.
 * Values are retained in fixed buffers so presenter-owned strings never leak
 * into asynchronous LVGL drawing.
 */
class SequencerChordVoiceRail final {
public:
    void create(lv_obj_t* parent);
    void render(const SequencerChordVoiceRailProps& props);

    lv_obj_t* element() const { return surface_; }

private:
    struct CachedItem {
        std::array<char, 8> label{};
        std::array<char, 8> value{};
        bool add = false;
        bool enabled = true;
    };

    void draw(lv_layer_t* layer);
    static void onDraw(lv_event_t* event);

    lv_obj_t* surface_ = nullptr;
    std::array<CachedItem, SequencerChordVoiceRailProps::MAX_ITEMS> items_{};
    uint8_t item_count_ = 0;
    uint8_t focused_item_ = 0;
    uint32_t color_ = 0;
    bool visible_ = false;
    bool rendered_ = false;
};

}  // namespace core::ui

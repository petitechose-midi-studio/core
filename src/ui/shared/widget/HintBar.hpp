#pragma once

#include <lvgl.h>

namespace UI {

enum class HintBarPosition { Left, Bottom, Right };

/**
 * @brief Container with 3 cells for positioning hint elements
 *
 * Pure layout component - receives LVGL elements and positions them.
 * Does not create or style content, only handles positioning.
 *
 * Used to display hints about physical button/encoder functions.
 *
 * Variants:
 * - Left:   3 cells vertical, aligned to left edge
 * - Bottom: 3 cells horizontal, at bottom
 * - Right:  3 cells vertical, aligned to right edge
 *
 * Cell alignments:
 * - Left/Right: cell 0=top, 1=middle, 2=bottom
 * - Bottom: cell 0=left, 1=center, 2=right
 */
class HintBar {
public:
    HintBar(lv_obj_t* parent, HintBarPosition position);
    ~HintBar();

    /**
     * @brief Position an element in a cell
     * @param index Cell index (0, 1, or 2)
     * @param element LVGL object to position (must be child of getElement())
     */
    void setCell(int index, lv_obj_t* element);

    /**
     * @brief Get number of cells (always 3)
     */
    static constexpr int getCellCount() { return 3; }

    /**
     * @brief Set fixed height (for Bottom) or width (for Left/Right)
     * @param size Size in pixels
     */
    void setSize(lv_coord_t size);

    void show();
    void hide();
    bool isVisible() const;

    lv_obj_t* getElement() {
        ensureCreated();
        return container_;
    }

private:
    static constexpr lv_coord_t DEFAULT_SIZE = 20;

    void ensureCreated();
    void applyGridLayout();

    HintBarPosition position_;
    lv_obj_t* parent_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_coord_t size_ = DEFAULT_SIZE;
};

}  // namespace UI

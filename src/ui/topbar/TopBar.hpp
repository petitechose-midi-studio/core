#pragma once

#include <lvgl.h>

namespace ui {

/**
 * @brief Props for TopBar component
 */
struct TopBarProps {
    const char* pageName = "";
};

/**
 * @brief Top bar displaying current page name (centered)
 *
 * Stateless component following Props pattern.
 * Rendered by orchestrator (StandaloneContext) when state changes.
 */
class TopBar {
public:
    explicit TopBar(lv_obj_t* parent);
    ~TopBar();

    TopBar(const TopBar&) = delete;
    TopBar& operator=(const TopBar&) = delete;

    /**
     * @brief Render with given props
     * @param props Display properties
     */
    void render(const TopBarProps& props);

private:
    lv_obj_t* container_ = nullptr;
    lv_obj_t* label_ = nullptr;
    TopBarProps currentProps_;  ///< Cached for change detection

    void createLayout(lv_obj_t* parent);
};

}  // namespace ui

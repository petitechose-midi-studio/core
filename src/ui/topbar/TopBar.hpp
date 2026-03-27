#pragma once

/**
 * @file TopBar.hpp
 * @brief Top status bar component
 */

#include <lvgl.h>
#include <oc/ui/lvgl/IComponent.hpp>

namespace core::ui {

struct TopBarProps {
    const char* pageName = "";
};

/**
 * @brief Top bar component displaying current page name (centered)
 *
 * Stateless component rendered from props.
 */
class TopBar : public oc::ui::lvgl::IComponent {
public:
    explicit TopBar(lv_obj_t* parent);
    ~TopBar() override;

    TopBar(const TopBar&) = delete;
    TopBar& operator=(const TopBar&) = delete;

    // IComponent interface
    void show() override;
    void hide() override;
    bool isVisible() const override;
    lv_obj_t* getElement() const override { return container_; }
    void render(const TopBarProps& props);

private:
    lv_obj_t* container_ = nullptr;
    lv_obj_t* label_ = nullptr;

    void createLayout(lv_obj_t* parent);
};

}  // namespace core::ui
